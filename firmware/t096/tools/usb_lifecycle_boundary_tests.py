#!/usr/bin/env python3
"""Exercise exact generated USB lifecycle/endpoint bodies with DMA sentinels.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from usb_ep0_recovery import generate

APP = Path(__file__).resolve().parents[1]
NAMES = ('ep_ctx_reset', 'eps_ctx_init', 'usb_dc_ep_flush',
         'usb_dc_attach', 'usb_dc_detach', 'usb_dc_reset')


def function(body, name):
    match = re.search(r'^.*?\b' + name + r'\([^;]*?\)\s*\{', body, re.M)
    assert match, name
    at = body.index('{', match.start())
    depth, end = 1, at + 1
    while depth:
        depth += (body[end] == '{') - (body[end] == '}')
        end += 1
    return body[match.start():end]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    generate(args.deps / 'zephyr', out / 'driver.c')
    body = (out / 'driver.c').read_text()
    manifest = json.loads((APP / 'cmake/usb-ep0-recovery.json').read_text())
    upstream = (args.deps / 'zephyr' / manifest['path']).read_text()
    assert sha(upstream.encode()) == manifest['original']
    fixture_path = APP / 'tests/usb_lifecycle_boundary_fixture.c'
    fixture = fixture_path.read_text()
    marker = '/* PTOB_USB_DRIVER_BODIES */'
    assert fixture.count(marker) == 1
    bodies = {name: function(body, name) for name in NAMES}
    # The pinned upstream reset publishes pointer/length changes before abort.
    # Use that exact old body as a negative, keeping the other current bodies.
    old_reset = function(upstream, 'ep_ctx_reset')
    results = []
    cc = os.environ.get('CC', '/opt/homebrew/opt/llvm/bin/clang')
    for variant, modes in [('fixed', range(12)), ('old', range(4))]:
        selected = dict(bodies)
        if variant == 'old':
            selected['ep_ctx_reset'] = old_reset
        code = fixture.replace(marker, '\n\n'.join(selected.values()))
        source, binary = out / (variant + '.c'), out / variant
        source.write_text(code)
        command = [cc, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
                   '-fno-sanitize-recover=all']
        if variant == 'old':
            command.append('-DOLD_NEGATIVE=1')
        command += [str(source), '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (out / (variant + '-build.log')).write_text(built.stdout + built.stderr)
        built.check_returncode()
        for mode in modes:
            run = subprocess.run([str(binary), str(mode)], capture_output=True,
                                 text=True, timeout=5)
            (out / f'{variant}-{mode}.log').write_text(run.stdout + run.stderr)
            expected = 42 if variant == 'old' else 0
            assert run.returncode == expected, (variant, mode, run.returncode, run.stderr)
            results.append(dict(variant=variant, mode=mode,
                                exit_code=run.returncode, expected=expected))
    hashes = {name: sha((APP / name).read_bytes()) for name in
              ('cmake/usb-ep0-recovery.patch', 'cmake/usb-ep0-recovery.json',
               'tests/usb_lifecycle_boundary_fixture.c',
               'tools/usb_lifecycle_boundary_tests.py', 'tools/usb_ep0_recovery.py')}
    receipt = dict(status='PASS_DRIVER_OWNERSHIP_SENTINELS_OFFLINE', hardware=False,
                   driver_sha256=sha(body.encode()), upstream_sha256=manifest['original'],
                   function_sha256={name: sha(text.encode()) for name, text in bodies.items()},
                   old_reset_sha256=sha(old_reset.encode()), source_hashes=hashes,
                   cases=results,
                   limits=['Synthetic DMA completion, queue and clock boundaries; no physical recovery claim.',
                           'Common peripheral/W1C transitions require the separate common-boundary suite.'])
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS USB driver ownership sentinels:', len(results), 'cases')


if __name__ == '__main__':
    main()
