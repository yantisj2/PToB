#!/usr/bin/env python3
"""MIT. Jonathan Yantis2026. Source-bound actual compiled RC check-site map.

Preprocessor stdout is parsed only in memory: never write expanded private
headers or raw CPP output. The map contains public source coordinates/reasons.
"""
import argparse, ast, hashlib, json, re, shlex, subprocess, zlib
from pathlib import Path

FILES = ('src/rc_dynamic_service.c', 'src/rc_dynamic_transport.inc',
         'include/rc_check_site.h')
ENCODING = dict(tag=32768, site_shift=3, site_bits=12, main_line_max=2559,
                transport_base=2560, transport_line_max=1535)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def calls(text, name):
    """Yield argument strings and physical invocation range; preserve strings."""
    for match in re.finditer(r'\b'+re.escape(name)+r'\s*\(', text):
        begin = i = match.end()
        depth, quoted, escaped, split, args = 1, False, False, i, []
        while depth:
            if i >= len(text):
                raise ValueError('Unclosed source call')
            c = text[i]
            if quoted:
                if escaped:
                    escaped = False
                elif c == '\\':
                    escaped = True
                elif c == '"':
                    quoted = False
            elif c == '"':
                quoted = True
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            elif c == ',' and depth == 1:
                args.append(text[split:i])
                split = i+1
            i += 1
        args.append(text[split:i-1])
        yield args, text.count('\n', 0, match.start())+1, text.count('\n', 0, i-1)+1


def literal(value):
    values = re.findall(r'"(?:[^"\\]|\\.)*"', value)
    return ''.join(ast.literal_eval(v) for v in values) if values else None


def number(value):
    value = re.sub(r'(?<=\d)[uUlL]+\b', '', value)
    expression = ast.parse(value.strip(), mode='eval').body
    def add(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return add(node.left)+add(node.right)
        raise ValueError('Unexpected compiler site expression')
    return add(expression)


def parse_sites(cpp, app):
    app = Path(app)
    spans = {}
    for file in FILES[:2]:
        spans[file] = [(start, end, literal(args[1])) for args, start, end
                       in calls((app/file).read_text(), 'check') if len(args) == 2]
    sites, seen = [], set()
    for args, _, _ in calls(cpp, 'check_at'):
        if len(args) != 3 or literal(args[1]) is None:
            continue  # Actual function definition, not an invocation.
        site, why = number(args[2]), literal(args[1])
        assert 0 < site <= 4095 and site not in seen, 'Invalid/duplicate compiled RC site'
        file, line = ((FILES[1], site-2560) if site >= 2560 else (FILES[0], site))
        matches = [(a, b, text) for a, b, text in spans[file] if a <= line <= b and text == why]
        assert len(matches) == 1, ('Compiler/source site mismatch', site, file, line)
        seen.add(site)
        sites.append(dict(site=site, file=file, line=line, why=why,
                          why_crc32=f'{zlib.crc32(why.encode()):08x}',
                          invocation_start_line=matches[0][0], invocation_end_line=matches[0][1]))
    assert sites and any(s['site'] >= 2560 for s in sites)
    return sorted(sites, key=lambda v: v['site'])


def preprocess_command(entry, app):
    command = list(entry.get('arguments') or shlex.split(entry['command']))
    original = Path(entry['file']).parents[1]
    result, skip = [], False
    for word in command:
        if skip:
            skip = False
            continue
        if word in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if word in ('-c', '-MD', '-MMD', '-MP', '-fstack-usage'):
            continue
        result.append(word.replace(str(original), str(Path(app).resolve())))
    return result+['-E', '-P', '-o', '-']


def generate(build):
    build = Path(build).resolve()
    receipt = json.loads((build/'receipt.json').read_text())
    assert receipt['gm7_native'] and receipt['profile'] in ('GM', 'RC')
    inputs = {f: sha(build/'source'/f) for f in FILES}
    assert all(receipt['sources'][f] == digest for f, digest in inputs.items())
    entries = json.loads((build/'target/compile_commands.json').read_text())
    entry = next(e for e in entries if e['file'].endswith('/src/rc_dynamic_service.c'))
    command = preprocess_command(entry, build/'source')
    process = subprocess.run(command, cwd=entry['directory'], capture_output=True,
                             text=True, timeout=60)
    if process.returncode:
        raise ValueError('RC map preprocessor failed; expanded source/error text deliberately not stored')
    sites = parse_sites(process.stdout, build/'source')
    assert all(sha(build/'source'/f) == h for f, h in inputs.items())
    return dict(schema=1, kind='RC_FIRST_CHECK_SOURCE_MAP',
                source_sha256=receipt['source_sha256'], child_commit=receipt['child_commit'],
                watchdog_binding_sha256=receipt['watchdog_binding_sha256'],
                silicon=json.loads((build/'device.json').read_text())['device_id'],
                input_sha256=inputs, compiler_command_sha256=hashlib.sha256(
                    json.dumps(command, separators=(',', ':')).encode()).hexdigest(),
                encoding=ENCODING, sites=sites, count=len(sites), hardware=False,
                limits='First failed boolean check, not original HCI/API return. Source lines validated against actual compiler macro expansion. No raw CPP/private headers retained.')


def resolve(build, binding, site):
    """Absent/unmatched map returns UNKNOWN. Never guess a reason from a line."""
    try:
        build = Path(build)
        receipt = json.loads((build/'receipt.json').read_text())
        assert receipt['status']=='BUILD_COMPLETE_PHYSICAL_UNKNOWN' and receipt['source_dirty'] is False
        path = build/'rc_check_sites.json'
        assert receipt['watchdog_binding_sha256'] == binding
        assert receipt['rc_check_site_map_sha256'] == sha(path)
        mapped = json.loads(path.read_text())
        assert mapped['schema'] == 1 and mapped['kind'] == 'RC_FIRST_CHECK_SOURCE_MAP'
        assert mapped['source_sha256'] == receipt['source_sha256']
        assert mapped['child_commit'] == receipt['child_commit']
        assert mapped['watchdog_binding_sha256'] == binding
        assert mapped['silicon'] == json.loads((build/'device.json').read_text())['device_id']
        assert mapped['encoding'] == ENCODING
        assert set(mapped['input_sha256']) == set(FILES)
        for f, digest in mapped['input_sha256'].items():
            assert receipt['sources'][f] == digest == sha(build/'source'/f)
        rows = [row for row in mapped['sites'] if row['site'] == site]
        assert len(rows) == 1
        return dict(resolved=True, status='EXACT_ORIGINAL_COMPILED_SOURCE_MAP', **rows[0])
    except (AssertionError, KeyError, ValueError, TypeError, OSError):
        return dict(resolved=False, status='SOURCE_MAP_UNAVAILABLE_OR_UNBOUND',
                    site=site, file='UNKNOWN', line=None, why='UNKNOWN')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    result = generate(a.build)
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(status='PASS_COMPILED_RC_CHECK_SOURCE_MAP', count=result['count'])))


if __name__ == '__main__':
    main()
