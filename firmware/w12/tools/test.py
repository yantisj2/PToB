#!/usr/bin/env python3
"""Select offline W12 checks by change, core behavior or complete suite.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]
AREAS = {
    'holdover': ['shadow_holdover_tests', 'g1_holdover'],
    'fine': ['fine_tests'],
    'power': ['power_tests'],
    'host': ['host_task_tests'],
    'watchdog': ['sync_watchdog_tests'],
    'display': ['shadow_holdover_tests', 'display_timing_tests', 'dual_display_tests'],
    'console': ['rc9_console_tests'],
    'core': ['g1_host_tests', 'fine_tests', 'shadow_holdover_tests', 'host_task_tests', 'sync_watchdog_tests', 'power_tests'],
}
FULL = AREAS['core'] + ['rc7_permission_e2e', 'lora_tests', 'rc9_tests', 'rc9_native_tests', 'rc9_console_tests', 'display_timing_tests', 'dual_display_tests', 'time_zone_tests']


def select(paths):
    selected = []
    for name in paths:
        p = Path(name)
        if p.suffix == '.md':
            continue
        if name.startswith(('registry/', 'protocol/', 'tools/')):
            return FULL
        if not name.startswith('firmware/w12/'):
            continue
        if p.name.startswith(('w12_holdover_', 'w12_shadow_', 'w12_thermal_', 'holdover_', 'shadow_')):
            selected += AREAS['holdover']
        elif p.name.startswith(('fine_', 'w12_fine_')):
            selected += AREAS['fine']
        elif p.name.startswith(('power_', 'w12_button')):
            selected += AREAS['power']
        elif p.name.startswith(('w12_host_task', 'host_task')):
            selected += AREAS['host']
        elif p.name.startswith(('sync_watchdog', 'fault_watchdog')):
            selected += AREAS['watchdog']
        elif p.name.startswith(('display', 'native_dashboard')):
            selected += AREAS['display']
        elif p.name.startswith(('framed_console', 'bounded_log', 'console_protocol', 'rc9_console', 'status_budget')):
            selected += AREAS['console']
        else:
            return FULL  # Unknown/shared dependencies fail toward broad checks.
    return list(dict.fromkeys(selected))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=('changed', 'core', 'full'), default='changed')
    p.add_argument('--area', choices=tuple(AREAS), action='append')
    p.add_argument('--base', default='HEAD', help='Compare worktree and index against this revision')
    p.add_argument('--idf', type=Path, default=os.environ.get('IDF_PATH'))
    p.add_argument('--out', type=Path)
    p.add_argument('--compiler-cache', type=Path, help='Optional ccache executable; local 512 MiB cache')
    p.add_argument('--list', action='store_true')
    a = p.parse_args()
    paths = []
    if a.area:
        suites = [n for area in a.area for n in AREAS[area]]
    elif a.mode == 'full':
        suites = FULL
    elif a.mode == 'core':
        suites = AREAS['core']
    else:
        paths = subprocess.check_output(['git', 'diff', '--name-only', a.base, '--'], cwd=REPO, text=True).splitlines()
        paths += subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=REPO, text=True).splitlines()
        suites = select(paths)
    suites = list(dict.fromkeys(suites))
    if a.list:
        print(json.dumps({'suites': suites, 'changed': paths}, indent=2))
        return
    if a.out is None:
        p.error('--out is required for execution')
    out = a.out.resolve()
    if REPO / 'artifacts' not in out.parents:
        p.error('--out must be under ignored artifacts/')
    out.mkdir(parents=True, exist_ok=False)
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    if a.idf:
        env['IDF_PATH'] = str(a.idf.resolve())
    cc = shutil.which('clang')
    cxx = shutil.which('clang++')
    preferred = Path('/opt/homebrew/opt/llvm/bin')
    if (preferred / 'clang').is_file():
        cc, cxx = str(preferred / 'clang'), str(preferred / 'clang++')
    ccache = None
    if a.compiler_cache:
        sys.path.insert(0, str(REPO / 'tools'))
        from compiler_cache import environment, launcher, finish
        ccache, cache_env = environment(REPO, 'host', a.compiler_cache)
        env.update(cache_env)
        cc, cxx = launcher(REPO, ccache, cc), launcher(REPO, ccache, cxx)
    env.update(CC=cc, CXX=cxx)
    result = {'status': 'RUNNING', 'hardware_access': False, 'selected': suites, 'changed': paths, 'checks': []}
    start = time.monotonic()
    def run(name, args):
        at = time.monotonic()
        with (out / (name + '.log')).open('w') as log:
            r = subprocess.run([sys.executable, '-B', *map(str, args)], env=env, cwd=REPO, stdout=log, stderr=subprocess.STDOUT)
        result['checks'].append({'name': name, 'seconds': round(time.monotonic() - at, 4), 'returncode': r.returncode})
        print(name + ': ' + ('PASS' if r.returncode == 0 else 'FAIL'), flush=True)
        if r.returncode:
            raise RuntimeError(name + ' failed; see ' + str(out / (name + '.log')))
    try:
        if suites:
            run('size_policy', [APP / 'tools/size_policy_tests.py'])
            run('stack_policy', [APP / 'tests/main_stack_budget_test.py'])
        for suite in suites:
            script = APP / 'tools' / (suite + '.py')
            args = [script, '--out', out / suite]
            if suite == 'g1_holdover':
                args[0] = APP / 'tools/g1_host_tests.py'
                args += ['--selection', 'holdover']
            if suite == 'time_zone_tests':
                args[0] = APP.parent / 'n30/tools/time_zone_tests.py'
            if suite in ('lora_tests', 'rc9_tests'):
                if not a.idf:
                    raise ValueError('--idf or IDF_PATH is required for ' + suite)
                args += ['--idf', a.idf.resolve()]
            if suite in ('rc9_native_tests', 'time_zone_tests'):
                args += ['--objects', out / 'g1_host_tests']
                if suite == 'time_zone_tests':
                    shutil.copyfile(out / 'g1_host_tests/precision-priority/gm7_codec.o', out / 'g1_host_tests/gm7_codec.o')
            if suite in ('rc7_permission_e2e', 'rc9_console_tests', 'rc9_native_tests'):
                args += ['--tz']
            run(suite, args)
        result['status'] = 'PASS'
    finally:
        if ccache:
            finish(ccache, env)
        result['seconds'] = round(time.monotonic() - start, 4)
        if result['status'] == 'RUNNING':
            result['status'] = 'FAILED'
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Offline checks complete in %.2fs' % result['seconds'])


if __name__ == '__main__':
    main()
