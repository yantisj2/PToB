#!/usr/bin/env python3
"""Production LoRa diagnostics, framing and role changes; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

APP = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tiny = args.deps.resolve() / 'modules/crypto/tinycrypt/lib'
    sources = [APP / 'tests/lora_diagnostics_test.c', APP / 'src/core.c',
               APP / 'src/lora_passive_core.c', APP / 'src/lora_receiver_watch.c',
               APP / 'protocol/lora_passive.c', APP / 'src/lora_rc8_policy.c',
               APP / 'protocol/lora_rc8.c', APP / 'protocol/group_auth.c',
               *[tiny / 'source' / (name + '.c') for name in ('hmac', 'sha256', 'utils')]]
    inputs = [*sources, APP / 'src/main.c', APP / 'src/lora_passive_service.c',
              APP / 'src/lora_hw.c', APP / 'src/bounded_json.c',
              APP / 'tests/lora_ram_fixture.h', *sorted((APP / 'include').glob('*.h')),
              *sorted((APP / 'protocol').glob('*.h')), Path(__file__)]
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {str(path): sha(path) for path in inputs}
    owner = out / 'owner.c'
    owner.write_text(re.sub(r'^#include[^\n]*\n', '',
                           (APP / 'src/lora_passive_service.c').read_text(), flags=re.M))
    hardware = (APP / 'src/lora_hw.c').read_text()
    start = hardware.index('static struct r2_lora_hw_rejection rejected;')
    admission = out / 'admission.c'
    admission.write_text(hardware[start:hardware.index('\n#endif\nstatic int ', start)])
    main_body = (APP / 'src/main.c').read_text()
    start = main_body.index('static void __attribute__((noinline)) lora_reply(')
    reply = out / 'lora_reply.c'
    reply.write_text(main_body[start:main_body.index('\n}\n#endif', start) + 3])
    compiler = os.environ.get('CC') or (
        '/opt/homebrew/opt/llvm/bin/clang'
        if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else 'clang')
    flags = [compiler,
             '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-function', '-Wno-unused-parameter',
             '-Wno-default-const-init-var-unsafe', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', '-DR2_LORA_PASSIVE_SLOW=1',
             '-DR2_LORA_FIXED500_EXPERIMENT=1',
             '-DR2_PASSIVE_SERVICE_BODY="' + str(owner) + '"',
             '-DR2_HW_ADMISSION_BODY="' + str(admission) + '"',
             '-DR2_LORA_REPLY_BODY="' + str(reply) + '"',
             *['-I' + str(path) for path in (APP / 'include', APP / 'protocol',
                                            APP / 'tests', tiny / 'include')]]
    passed = False
    try:
        with (out / 'test.log').open('x') as log:
            subprocess.run([*flags, *map(str, sources), '-o', str(out / 'test')],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            subprocess.run([str(out / 'test')], stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=60)
        assert before == {str(path): sha(path) for path in inputs}, 'Test inputs changed'
        passed = True
    finally:
        (out / 'receipt.json').write_text(json.dumps({
            'passed': passed, 'hardware': False, 'replies': 7200, 'role_cycles': 400,
            'source_hashes': before,
            'limits': 'Production owner, JSON, core framing and main LoRa reply bodies '
                      'with synthetic capture/radio/console boundaries. Does not exercise '
                      'the USB stack, prove a physical failure cause or qualify hardware.'
        }, indent=2) + '\n')
    print('PASS actual LoRa diagnostics/role/framing stress; no hardware')


if __name__ == '__main__':
    main()
