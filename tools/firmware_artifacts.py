#!/usr/bin/env python3
"""Bound private firmware storage without changing historical receipts or hardware.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import contextlib
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

KINDS = {'PTOB_T096_RC7', 'PTOB_T096_DC1', 'PTOB_T096_PC1', 'PTOB_W12_RC',
         'PTOB_N30_RC', 'PTOB_SS3_RC', 'CORESE_LIVE_CLIENT', 'UI_PREPARATION_ONLY'}
SUCCESS = {'BUILD_COMPLETE_PHYSICAL_UNKNOWN', 'BUILD_COMPLETE',
           'COMMISSIONING_CANDIDATE_PHYSICAL_UNKNOWN', 'DEVELOPMENT_NOT_FLASHABLE',
           'OFFLINE_NOT_FLASHABLE', 'OFFLINE_UI_BUILD_VERIFIED_NOT_INSTALLED'}
TERMINAL = SUCCESS | {'FAILED'}
SKIP = {'.git', '.internal', 'target', 'local', 'cache',
        'pycache', '__pycache__', 'node_modules', 'esp-idf', 'zephyr', 'modules',
        'dependencies', 'venv', '.venv', 'esp-tools', '.firmware-retention', '.compiler-cache'}
OBJECTS = {'.o', '.obj', '.a', '.d'}
STATE = '.firmware-retention'
BUNDLE = 'storage-bundle.json'
RETIRED = 'storage-retired.json'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temp = path.with_name(path.name + '.tmp-' + str(os.getpid()))
    with temp.open('x', encoding='utf-8') as stream:
        os.chmod(temp, 0o600)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temp, path)


def safe_path(root, path):
    """Lexical containment AND no symlink in any component, including the root."""
    if '..' in Path(path).parts:
        raise ValueError('Parent traversal is not cleanup authority: ' + str(path))
    root, path = Path(root).absolute(), Path(path).absolute()
    if not path.is_relative_to(root):
        raise ValueError('Path outside allowed root: ' + str(path))
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError('Symlink is not cleanup authority: ' + str(part))
    return path


def relative(root, name):
    p = Path(name)
    if p.is_absolute() or '..' in p.parts:
        raise ValueError('Unsafe receipt path: ' + name)
    return safe_path(root, root / p)


@contextlib.contextmanager
def locked(repo):
    repo = storage_root(repo)
    state = safe_path(repo, repo / 'artifacts' / STATE)
    state.mkdir(parents=True, exist_ok=True, mode=0o700)
    lock = safe_path(state, state / 'lock')
    with lock.open('a') as stream:
        os.chmod(lock, 0o600)
        try:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another firmware build or cleanup holds the storage lock') from None
        yield


def storage_root(repo):
    """Frozen checkouts below artifacts belong to the outer storage lifecycle."""
    repo = Path(repo).resolve()
    owner = repo
    for parent in repo.parents:
        if (parent / 'artifacts') in repo.parents and (parent / '.git').exists():
            owner = parent
    return owner


def files(root):
    pending = [root]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                # Zephyr legitimately links generated syscall headers into its
                # SDK. Never traverse or unlink these links or their targets.
                if entry.is_symlink():
                    continue
                if entry.name == '.git':
                    raise ValueError('Nested repository in build: ' + entry.path)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(Path(entry.path))
                elif entry.is_file(follow_symlinks=False):
                    yield Path(entry.path)
                else:
                    raise ValueError('Non-regular build file: ' + entry.path)


def discover(repo):
    root = safe_path(repo, repo / 'artifacts')
    found = []
    for base, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = [d for d in dirs if d not in SKIP and not (Path(base) / d).is_symlink()]
        if 'receipt.json' not in names:
            continue
        p = Path(base)
        if (p / 'receipt.json').is_symlink():
            continue
        try:
            r = read(p / 'receipt.json')
        except (OSError, ValueError):
            continue
        if isinstance(r, dict) and r.get('artifact_class', r.get('kind')) in KINDS:
            found.append(p)
            dirs[:] = []
    return sorted(found)


def grouping(root, r):
    kind = r.get('artifact_class', r.get('kind'))
    device = {}
    for name in ('device.json', 'local/device.json'):
        p = relative(root, name)
        if p.exists():
            expected = r.get('device_config_sha256') or r.get('artifacts', {}).get(name)
            if expected and digest(p) != expected:
                raise ValueError('Device input differs from receipt; cannot select retention group')
            device = read(p)
            break
    identity = device.get('device_id', r.get('device_id'))
    if identity is None and kind.startswith('PTOB_T096_'):
        # Earlier T096 receipts retained the compiler's device header, but not
        # device.json. Use only its receipt-bound exact identity, never an alias.
        name = 'target/ptob_device.h'
        p = relative(root, name)
        expected = r.get('artifacts', {}).get(name)
        if expected and p.is_file() and digest(p) == expected:
            matches = re.findall(r'^#define PTOB_DEVICE_ID "([0-9a-f]{16})"$', p.read_text(), re.M)
            if len(matches) == 1:
                identity = matches[0]
    if kind == 'UI_PREPARATION_ONLY':
        identity = 'ui-only'
    elif not isinstance(identity, str) or not re.fullmatch('[0-9a-f]{16}', identity) or int(identity, 16) == 0:
        raise ValueError('No exact device identity')
    lane = 'development' if r.get('source_dirty') or r['status'] == 'DEVELOPMENT_NOT_FLASHABLE' else 'candidate'
    if r['status'] in {'OFFLINE_NOT_FLASHABLE', 'OFFLINE_UI_BUILD_VERIFIED_NOT_INSTALLED'}:
        lane = 'offline'
    # Do not key by firmware version, source hash, calibration hash or enrollment:
    # doing so would silently create a new permanent retention bucket every run.
    options = {k: bool(r.get(k, False)) for k in ('estimated_relay', 'fine_clock',
               'lora_rx', 'rc9', 'controller_diagnostics', 'gnss_fast_map_diagnostic')}
    options['listening_percent'] = r.get('listening_percent', 100)
    options['gnss'] = bool(device.get('gnss_profile'))
    backend = r.get('clock_backend') or {}
    if isinstance(backend, dict):
        options['clock_mode'] = backend.get('mode', 'LEGACY_GPTIMER20' if kind in {'PTOB_W12_RC', 'PTOB_N30_RC'} else None)
        options['counter_hz'] = backend.get('counter_hz', 20000000 if kind in {'PTOB_W12_RC', 'PTOB_N30_RC'} else None)
    else:
        options['clock_mode'] = str(backend)
    return json.dumps([kind, identity, r.get('profile', 'RC'), lane, options], sort_keys=True)


def protected_files(root, r):
    result = set(r.get('artifacts', {}))
    result.update('source/' + name for name in r.get('sources', {}))
    result.update(k[len('generated/'):] for k in r.get('compiled_sources', {}) if k.startswith('generated/'))
    if r.get('application'):
        result.add(r['application'])
    # Validate shared ancestors once per receipt, never across plan/apply or
    # mutation boundaries. Deletion still calls safe_path immediately before
    # unlinking each file. Large receipts otherwise repeat millions of lstats.
    checked = set()
    for name in result:
        p = Path(name)
        if p.is_absolute() or '..' in p.parts:
            raise ValueError('Unsafe receipt path: ' + name)
        for part in (root / p, *(root / p).parents):
            if part in checked:
                break
            if part.is_symlink():
                raise ValueError('Symlink is not cleanup authority: ' + str(part))
            checked.add(part)
    return result


def verify_bundle(root):
    """Verify retained identities, not hardware acceptance or fresh native inspection."""
    root = Path(root)
    if (root / RETIRED).exists():
        raise ValueError('Retired build: image intentionally unavailable')
    r = read(root / 'receipt.json')
    if r.get('status') not in SUCCESS:
        raise ValueError('Not a successfully completed build')
    count = 0
    maps = [(root, r.get('artifacts', {})), (root / 'source', r.get('sources', {})),
            (root / 'dependencies/RadioLib', r.get('radiolib_sources', {}))]
    # The early CoreSE UI builder records live source hashes but no source copy.
    if r.get('kind') == 'UI_PREPARATION_ONLY':
        maps = [(root, r.get('artifacts', {}))]
    for base, entries in maps:
        for name, expected in entries.items():
            if digest(relative(base, name)) != expected:
                raise ValueError('Retained hash mismatch: ' + str(base / name))
            count += 1
    for name, expected in r.get('compiled_sources', {}).items():
        prefix, rel = name.split('/', 1)
        base = {'generated': root, 'app': root / 'source', 'repo': root / 'source',
                'radiolib': root / 'dependencies/RadioLib'}.get(prefix)
        if base is not None and digest(relative(base, rel)) != expected:
            raise ValueError('Retained compiled source mismatch: ' + name)
    app = r.get('application')
    if app is None:
        app = 't096.uf2' if r.get('artifact_class', '').startswith('PTOB_T096_') else 'target/ptob_corese_ui.bin'
    p = relative(root, app)
    if not p.is_file() or p.stat().st_size == 0:
        raise ValueError('Application missing')
    if r.get('application_sha256') and digest(p) != r['application_sha256']:
        raise ValueError('Application hash mismatch')
    if r.get('application_bytes') and p.stat().st_size != r['application_bytes']:
        raise ValueError('Application length mismatch')
    return {'scope': 'RETAINED_FILE_IDENTITIES_ONLY_NOT_HARDWARE_ACCEPTANCE',
            'receipt_sha256': digest(root / 'receipt.json'), 'checked_files': count,
            'application': app, 'application_sha256': digest(p)}


def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, v in value.items():
            # Source/SDK locations are provenance, not pins for every unrelated
            # image ever built inside that checkout's artifacts directory.
            if key not in {'source', 'source_root', 'source_path', 'repo', 'repo_root',
                           'repository', 'cwd', 'sdk', 'sdk_path', 'idf', 'deps',
                           'dependencies', 'app_path', 'radiolib_path', 'compiler_path',
                           'toolchain_path', 'command'}:
                yield from strings(v)
    elif isinstance(value, list):
        for v in value:
            yield from strings(v)


def pin_digest(name):
    if name.startswith('symlink:'):
        return hashlib.sha256(os.readlink(name[len('symlink:'):]).encode()).hexdigest()
    return digest(Path(name))


def pins(repo, builds):
    """Follow local current/recovery JSON references; never print enrollment data."""
    local = repo / 'artifacts/local'
    seeds = sorted(set(local.rglob('*current*.json')) | set(local.rglob('*recovery*.json')))
    config = local / 'firmware-retention.json'
    if config.exists():
        seeds.append(config)
    pinned, seen, inputs, unresolved = {}, set(), {}, []
    build_set = set(builds)
    descendants = {}
    for build in builds:
        for parent in build.parents:
            if parent == repo / 'artifacts':
                break
            descendants.setdefault(parent, []).append(build)
    queue = list(seeds)
    while queue:
        p = queue.pop()
        if p in seen:
            continue
        seen.add(p)
        if not p.is_relative_to(repo / 'artifacts'):
            continue
        # Following a reference is read-only and adds protection; it never
        # grants deletion authority. Old installation records use symlink aliases.
        for part in (p, *p.parents):
            if part.is_symlink():
                name = 'symlink:' + str(part)
                inputs[name] = pin_digest(name)
        p = p.resolve()
        if not p.is_relative_to(repo / 'artifacts'):
            continue
        # A file inside a build pins the entire compact bundle; don't follow
        # receipt metadata back into the SDK or live source checkout.
        owners = build_set.intersection((p, *p.parents))
        if owners:
            for b in owners:
                pinned[str(b)] = 'Referenced by current/recovery/pin records'
            continue
        if p.is_dir():
            if p != repo / 'artifacts' and p != local:
                for b in descendants.get(p, ()):
                    pinned[str(b)] = 'Within referenced artifact directory'
            continue
        if not p.exists():
            unresolved.append(str(p))
            continue
        if p.suffix != '.json':
            continue
        inputs[str(p)] = digest(p)
        data = read(p)
        for value in strings(data):
            if '\n' in value or len(value) > 4096:
                continue
            if value.startswith(str(repo / 'artifacts') + '/'):
                q = Path(value)
            elif value.startswith('artifacts/'):
                q = repo / value
            elif '/' in value and not value.startswith(('/', 'http:', 'https:')):
                q = p.parent / value
                try:
                    exists = q.exists()
                except OSError:
                    exists = False
                if not exists:
                    continue
            else:
                continue
            q = Path(os.path.abspath(q))
            if q.is_relative_to(repo / 'artifacts'):
                queue.append(q)
    return pinned, inputs, sorted(set(unresolved))


def tracked_files(repo, builds=()):
    repositories = {repo}
    for build in builds:
        for parent in (build, *build.parents):
            if parent == repo:
                break
            if (parent / '.git').exists():
                repositories.add(parent)
                break
    result = set()
    for checkout in repositories:
        raw = subprocess.check_output(['git', '-C', str(checkout), 'ls-files', '-z', '--', 'artifacts'])
        result.update(checkout / os.fsdecode(p) for p in raw.split(b'\0') if p)
    return result


def candidate_files(root, r, mode):
    protected = protected_files(root, r)
    def generated_files():
        for name in ('target', 'cache', 'pycache'):
            folder = root / name
            if folder.is_symlink():
                raise ValueError('Symlink build-output root: ' + str(folder))
            if folder.is_dir():
                yield from files(folder)
        uf2 = root / 't096.uf2'
        if uf2.is_file() and not uf2.is_symlink():
            yield uf2
    for p in generated_files():
        rel = str(p.relative_to(root))
        if rel in protected and mode == 'compact':
            continue
        first = p.relative_to(root).parts[0]
        if first in ('cache', 'pycache'):
            yield p
        elif first == 'target':
            # Preserve static stack evidence and compiler failure diagnostics,
            # including CMake's logs inside the generated tree.
            if mode == 'retire' and p.suffix not in {'.su', '.log'} and p.name != 'CMakeConfigureLog.yaml':
                yield p
            elif mode == 'compact' and p.suffix in OBJECTS:
                yield p
        elif mode == 'retire' and rel == 't096.uf2':
            yield p


def describe_candidates(root, r, mode, tracked):
    h, size, count = hashlib.sha256(), 0, 0
    for p in sorted(candidate_files(root, r, mode)):
        if p in tracked:
            raise ValueError('Tracked file in cleanup candidates: ' + str(p))
        s = p.stat()
        row = [str(p.relative_to(root)), s.st_ino, s.st_size, s.st_mtime_ns]
        h.update(json.dumps(row, separators=(',', ':')).encode())
        size += s.st_blocks * 512
        count += 1
    return {'candidate_sha256': h.hexdigest(), 'files': count, 'allocated_bytes': size}


def prune_empty_generated_directories(root):
    removed = 0
    for name in ('target', 'cache', 'pycache'):
        folder = root / name
        if not folder.exists() or folder.is_symlink():
            continue
        for base, _, _ in os.walk(folder, topdown=False, followlinks=False):
            path = Path(base)
            if path == root / 'target':
                continue
            try:
                safe_path(root, path).rmdir()
                removed += 1
            except OSError as error:
                if error.errno not in (errno.ENOTEMPTY, errno.EEXIST, errno.ENOENT):
                    raise
    return removed


def make_plan(repo, mode='retain', roots=None, min_age=3600, just_built=None, scope_group=None):
    builds = discover(repo) if roots is None else sorted(set(roots))
    pinned, inputs, unresolved = pins(repo, builds)
    if just_built and Path(just_built) in builds:
        r = read(Path(just_built) / 'receipt.json')
        if r.get('status') in SUCCESS:
            pinned[just_built] = 'Newly completed output; retained for this build invocation'
    tracked = tracked_files(repo, builds)
    rows, skipped, groups = [], [], {}
    for root in builds:
        try:
            safe_path(repo / 'artifacts', root)
            r = read(relative(root, 'receipt.json'))
            retirement = read(root / RETIRED) if (root / RETIRED).exists() else None
            if retirement and retirement.get('state') == 'RETIRED':
                continue
            if r.get('artifact_class', r.get('kind')) not in KINDS or r.get('status') not in TERMINAL:
                raise ValueError('Unknown or active/incomplete build')
            if str(root) != just_built and time.time() - (root / 'receipt.json').stat().st_mtime < min_age:
                raise ValueError('Recent build; wait for minimum age')
            group = retirement['group'] if retirement and retirement.get('group') else grouping(root, r)
            if scope_group is not None and group != scope_group:
                continue
            row = {'path': str(root), 'receipt_sha256': digest(root / 'receipt.json'),
                   'status': r['status'], 'group': group,
                   'version': [int(x) for x in re.findall(r'\d+', r.get('version', '0'))],
                   'interrupted_retirement': bool(retirement),
                   'completed_ns': (root / 'receipt.json').stat().st_mtime_ns}
            if r['status'] in SUCCESS and not retirement:
                groups.setdefault(group, []).append(row)
            rows.append(row)
        except (OSError, ValueError, TypeError, KeyError) as e:
            skipped.append({'path': str(root), 'reason': str(e)})
    latest = {}
    keep = set(pinned)
    for group, group_rows in groups.items():
        chosen = sorted(group_rows, key=lambda x: (x['version'], x['completed_ns'], x['path']), reverse=True)[:2]
        latest[group] = [r['path'] for r in chosen]
        keep.update(latest[group])
    actions = []
    for row_number, row in enumerate(rows):
        if len(rows) > 50 and row_number % 100 == 0:
            print('Storage inventory: %d/%d builds' % (row_number, len(rows)), file=sys.stderr, flush=True)
        root = Path(row['path'])
        r = read(root / 'receipt.json')
        action = 'retire' if mode == 'retain' and row['path'] not in keep else 'compact'
        if row['interrupted_retirement']:
            if row['path'] in pinned:
                skipped.append({'path': str(root), 'reason': 'Interrupted retirement now pinned; manual recovery needed'})
                continue
            action = 'retire'
        # Failed/incomplete builds never stand in for a successful image.
        try:
            summary = describe_candidates(root, r, action, tracked)
            if summary['files']:
                actions.append({**row, **summary, 'action': action,
                                'pinned': pinned.get(row['path'])})
        except (OSError, ValueError, TypeError) as e:
            skipped.append({'path': str(root), 'reason': str(e)})
    return {'schema': 1, 'repo': str(repo), 'mode': mode, 'min_age_seconds': min_age,
            'created_unix': time.time(), 'just_built': just_built, 'builds': [str(p) for p in builds],
            'pins': pinned, 'pin_inputs': inputs, 'unresolved_references': unresolved,
            'latest': latest, 'actions': actions, 'skipped': skipped,
            'allocated_candidate_bytes': sum(x['allocated_bytes'] for x in actions),
            **({'scope_group': scope_group} if scope_group is not None else {})}


def apply_plan(repo, plan, *, catalogued=False):
    if plan['repo'] != str(repo):
        raise ValueError('Plan belongs to another checkout')
    roots = [safe_path(repo / 'artifacts', Path(p)) for p in plan['builds']]
    # Replan under the same lock. Changed files, receipts, pins or new builds
    # invalidate a saved plan before any deletion, rather than racing rotation.
    fresh = make_plan(repo, plan['mode'], roots, plan['min_age_seconds'], plan.get('just_built'), plan.get('scope_group'))
    for key in ('actions', 'pins', 'pin_inputs', 'latest', 'skipped'):
        if fresh[key] != plan[key]:
            raise ValueError('Stale plan (' + key + '); generate a new plan')
    known = set(roots)
    if not catalogued and set(discover(repo)) - known:
        raise ValueError('New builds appeared; generate a new plan')
    # All retained successful bundles must verify before retiring any predecessor.
    verification, invalid = {}, {}
    for paths in plan['latest'].values():
        for name in paths:
            try:
                verification[name] = verify_bundle(Path(name))
            except (OSError, ValueError, KeyError) as e:
                invalid[name] = str(e)
    bad_groups = {g for g, paths in plan['latest'].items() if any(p in invalid for p in paths)}
    results = []
    journal = repo / 'artifacts' / STATE / ('apply-' + str(time.time_ns()) + '.jsonl')
    journal.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with journal.open('x') as log:
        os.chmod(journal, 0o600)
        for row in plan['actions']:
            root = Path(row['path'])
            try:
                if row['action'] == 'retire' and row['group'] in bad_groups:
                    raise ValueError('Replacement bundle failed identity verification')
                if row['status'] in SUCCESS and row['action'] == 'compact':
                    proof = verification.get(str(root)) or verify_bundle(root)
                else:
                    proof = None
                r = read(root / 'receipt.json')
                if digest(root / 'receipt.json') != row['receipt_sha256']:
                    raise ValueError('Receipt changed during apply')
                # Verify pin inputs again immediately before each mutation.
                for name, expected in plan['pin_inputs'].items():
                    if pin_digest(name) != expected:
                        raise ValueError('Current/recovery record changed during apply')
                actual = describe_candidates(root, r, row['action'], tracked_files(repo, [root]))
                if any(actual[k] != row[k] for k in actual):
                    raise ValueError('Build files changed during apply')
                marker = {'schema': 1, 'receipt_sha256': row['receipt_sha256'],
                          'group': row['group'],
                          'action': row['action'], 'candidate_sha256': row['candidate_sha256'],
                          'files': row['files'], 'allocated_bytes': row['allocated_bytes'],
                          'time_unix': time.time(), 'journal': str(journal),
                          'note': 'Original receipt unchanged. Retirement removes image availability, not historical evidence.'}
                # Write-ahead retirement prevents an interrupted deletion from
                # being treated as a valid image by this tool on the next run.
                if row['action'] == 'retire':
                    atomic_json(root / RETIRED, {**marker, 'state': 'REMOVING'})
                log.write(json.dumps({**marker, 'path': str(root), 'state': 'START'}) + '\n')
                log.flush()
                os.fsync(log.fileno())
                for p in list(candidate_files(root, r, row['action'])):
                    safe_path(root, p).unlink()
                marker['empty_directories_removed'] = prune_empty_generated_directories(root)
                if proof is not None:
                    if verify_bundle(root) != proof:
                        raise ValueError('Post-cleanup retained identity changed')
                    atomic_json(root / BUNDLE, {**marker, 'verification': proof})
                if row['action'] == 'retire':
                    atomic_json(root / RETIRED, {**marker, 'state': 'RETIRED'})
                result = {'path': str(root), 'state': 'DONE', **marker}
            except (OSError, ValueError, KeyError) as e:
                result = {'path': str(root), 'state': 'SKIPPED_OR_INTERRUPTED', 'reason': str(e)}
            results.append(result)
            log.write(json.dumps(result) + '\n')
            log.flush()
            print(result['state'], root.relative_to(repo), flush=True)
    latest = plan['latest']
    if plan.get('scope_group'):
        try:
            previous = read(repo / 'artifacts' / STATE / 'latest.json')
            latest = {**previous.get('latest', {}), **latest}
            invalid = {**previous.get('invalid', {}), **invalid}
        except (OSError, ValueError, TypeError):
            pass
    atomic_json(repo / 'artifacts' / STATE / 'latest.json',
                {'schema': 1, 'latest': latest, 'invalid': invalid,
                 'scope': 'BUILD_RETENTION_ONLY_NOT_INSTALLED_DEVICE_STATE'})
    atomic_json(repo / 'artifacts' / STATE / 'index.json', {'builds': plan['builds']})
    return {'journal': str(journal), 'results': results, 'invalid_latest': invalid,
            'completed': sum(r['state'] == 'DONE' for r in results),
            'skipped': sum(r['state'] != 'DONE' for r in results),
            'allocated_bytes_unlinked': sum(r.get('allocated_bytes', 0) for r in results if r['state'] == 'DONE')}


def run_builder(main, repo):
    """All public native builder entry points share this lifecycle and lock."""
    repo = storage_root(repo)
    if '--help' in sys.argv or '-h' in sys.argv:
        return main()
    out_args = argparse.ArgumentParser(add_help=False)
    out_args.add_argument('--out', type=Path, required=True)
    out = out_args.parse_known_args()[0].out.resolve()
    with locked(repo):
        free = shutil.disk_usage(repo).free
        if free < 2 * 1024**3:
            raise RuntimeError('Less than 2 GiB free: run firmware_artifacts.py plan/apply first')
        prior_receipt = digest(out / 'receipt.json') if (out / 'receipt.json').is_file() else None
        try:
            main()
        except BaseException:
            # A failed attempt may release its disposable compiler output, but
            # only after the native builder has recorded FAILED. Validation or
            # argument errors against an existing output must not mutate it.
            if (out / 'receipt.json').is_file() and digest(out / 'receipt.json') != prior_receipt:
                try:
                    if read(out / 'receipt.json').get('status') == 'FAILED':
                        cleanup_after_build(repo, out, successful=False)
                except Exception as e:
                    print('Failed-build cleanup needs attention: ' + str(e), file=sys.stderr)
            raise
        cleanup_after_build(repo, out, successful=True)


def cleanup_after_build(repo, out, successful):
    """Caller holds the shared lock; cleanup errors never change native receipts."""
    started = time.monotonic()
    try:
        safe_path(repo / 'artifacts', out)
        if successful:
            verify_bundle(out)
        # Public builders register under the shared lock. Reconcile historical
        # builders explicitly with plan/apply; uncatalogued output is never a
        # deletion candidate. A missing/invalid catalog falls back to discovery.
        roots = None
        try:
            catalog = read(repo / 'artifacts' / STATE / 'index.json')
            if not isinstance(catalog.get('builds'), list):
                raise ValueError('Invalid build catalog')
            roots = [safe_path(repo / 'artifacts', Path(p)) for p in catalog['builds']]
            roots.append(safe_path(repo / 'artifacts', out))
        except (OSError, ValueError, TypeError, AttributeError):
            roots = None
        group = grouping(out, read(out / 'receipt.json'))
        plan = make_plan(repo, roots=roots, just_built=str(out), scope_group=group)
        # The successfully built output must be retained even after copying
        # an old receipt or when clocks differ. In normal builds it is latest.
        if successful and any(r['path'] == str(out) and r['action'] != 'compact' for r in plan['actions']):
            raise ValueError('New output did not become latest; refusing automatic rotation')
        result = apply_plan(repo, plan, catalogued=roots is not None)
        atomic_json(out / 'storage-timing.json', {'seconds': round(time.monotonic() - started, 4),
                    'catalogued': roots is not None, 'scope': 'COMPLETED_RETENTION_GROUP',
                    'completed': result['completed'], 'skipped': result['skipped']})
        print('Firmware storage: reclaimed %.2f GiB; %d builds skipped' %
              (result['allocated_bytes_unlinked'] / 1024**3, result['skipped']))
    except Exception as e:
        print('Automatic cleanup needs attention: ' + str(e), file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('inventory')
    p = sub.add_parser('plan')
    p.add_argument('--mode', choices=['compact', 'retain'], default='retain')
    p.add_argument('--min-age-hours', type=float, default=1)
    p.add_argument('--out', type=Path, required=True)
    p = sub.add_parser('apply')
    p.add_argument('--plan', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p = sub.add_parser('inspect')
    p.add_argument('build', type=Path)
    args = parser.parse_args()
    repo = storage_root(args.repo)
    with locked(repo):
        if args.command == 'inventory':
            print(json.dumps([str(p) for p in discover(repo)], indent=2))
        elif args.command == 'inspect':
            print(json.dumps(verify_bundle(safe_path(repo / 'artifacts', args.build.absolute())), indent=2))
        elif args.command == 'plan':
            if args.min_age_hours < 0:
                parser.error('Minimum age cannot be negative')
            out = safe_path(repo / 'artifacts', args.out.absolute())
            plan = make_plan(repo, args.mode, min_age=args.min_age_hours * 3600)
            atomic_json(out, plan)
            print(json.dumps({'plan': str(out), 'builds': len(plan['builds']),
                  'actions': len(plan['actions']), 'pinned': len(plan['pins']),
                  'skipped': len(plan['skipped']), 'candidate_GiB': plan['allocated_candidate_bytes'] / 1024**3}, indent=2))
        else:
            out = safe_path(repo / 'artifacts', args.out.absolute())
            result = apply_plan(repo, read(args.plan))
            atomic_json(out, result)
            print(json.dumps({k: v for k, v in result.items() if k != 'results'}, indent=2))


if __name__ == '__main__':
    main()
