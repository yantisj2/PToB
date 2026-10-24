#!/usr/bin/env python3
"""Offline GD2/GD1 production codecs, GNSS diagnostics and Python HMAC checks.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--deps', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    root = app.parents[1]
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tiny = args.deps.resolve() / 'modules/crypto/tinycrypt/lib'
    sources = [app / 'tests/gnss_diagnostics_test.c',
               app / 'protocol/gnss_diagnostics.h',
               app / 'protocol/gnss_diagnostics.c',
               app / 'protocol/gm_role_status.h',
               app / 'protocol/group_auth.h', app / 'protocol/group_auth.c',
               app / 'tests/gnss_hdop_test.c', app / 'include/clock.h',
               app / 'src/clock.c', app / 'src/nmea.c',
               Path(__file__).resolve()]
    hashes = lambda: {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in sources}
    before = hashes()
    homebrew = Path('/opt/homebrew/opt/llvm/bin/clang')
    cc = os.environ.get('CC') or (str(homebrew) if homebrew.exists() else shutil.which('clang'))
    command = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
               '-Wno-default-const-init-var-unsafe',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I' + str(app / 'protocol'), '-I' + str(app / 'include'), '-I' + str(tiny / 'include'),
               str(app / 'tests/gnss_diagnostics_test.c'),
               str(app / 'protocol/gnss_diagnostics.c'), str(app / 'protocol/group_auth.c'),
               *[str(tiny / 'source' / name) for name in ['hmac.c', 'sha256.c', 'utils.c']],
               '-o', str(out / 'gnss_diagnostics')]
    passed = False
    parser_command = []
    try:
        with (out / 'compile.log').open('x') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        with (out / 'run.log').open('x') as log:
            subprocess.run([str(out / 'gnss_diagnostics')], stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=30)
        emitted = {line.split()[0]: line.split()[1] for line in (out / 'run.log').read_text().splitlines()
                   if line.startswith(('GD1_VECTOR ', 'GD2_VECTOR '))}
        assert len(emitted) == 2
        expected = bytearray(74)
        expected[:8] = bytes([89, 255, 255, 255, ord('G'), ord('D'), 2, 0])
        fields = [(8, 8, 0x1122334455667788), (16, 4, 7),
                  (20, 8, 0x0102030405060708), (28, 8, 0x1020304050607080),
                  (36, 4, 9), (40, 4, 3), (44, 8, 1000000), (52, 4, 30000000),
                  (60, 2, 1), (62, 2, 2), (64, 2, 3), (70, 2, 65535)]
        for offset, size, value in fields:
            expected[offset:offset + size] = value.to_bytes(size, 'little')
        expected[56:60] = bytes([63, 7, 12, 1])
        expected[66:70] = bytes([128, 248, 254, 128])
        expected[72:74] = (123).to_bytes(2, 'little')
        context = bytes([1]) + (0x1122334455667788).to_bytes(8, 'little') + (7).to_bytes(4, 'little')
        public_key = bytes([1]) + bytes(31)
        for version in [2, 1]:
            body = expected.copy() if version == 2 else expected[:72]
            if version == 1:
                body[0] = 87; body[6] = 1; body[56] = 31
            domain = f'R2GNDG0{version}'
            tag = hmac.new(public_key, domain.encode() + context + body, hashlib.sha256).digest()[:16]
            wire = bytes.fromhex(emitted[f'GD{version}_VECTOR'])
            assert wire == body + tag
            vector = {'status': 'PASS_SYNTHETIC_KNOWN_ANSWER', 'hardware': False,
                      'key_is_public_synthetic': True, 'key_hex': public_key.hex(),
                      'domain': domain, 'context_hex': context.hex(), 'wire_hex': wire.hex()}
            (out / f'vector-v{version}.json').write_text(json.dumps(vector, indent=2) + '\n')
        parser_command = command[:command.index(str(app / 'tests/gnss_diagnostics_test.c'))] + [
            str(app / 'tests/gnss_hdop_test.c'), str(app / 'src/clock.c'),
            str(app / 'src/nmea.c'), '-o', str(out / 'gnss_hdop')]
        with (out / 'hdop-compile.log').open('x') as log:
            subprocess.run(parser_command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        with (out / 'hdop-run.log').open('x') as log:
            subprocess.run([str(out / 'gnss_hdop')], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
        assert before == hashes(), 'Tested sources changed while running'
        passed = True
    finally:
        (out / 'receipt.json').write_text(json.dumps({
            'passed': passed, 'hardware': False, 'source_hashes': before,
            'sources_unchanged': before == hashes(), 'command': command,
            'parser_command': parser_command,
            'limits': ['Codec/semantic checks use real TinyCrypt with a public synthetic key.',
                       'Receiver replay/receipt expiry, scheduler, native resources and actual RF require separate checks.',
                       'Diagnostic HDOP parsing is tested separately from unchanged GGA acceptance.',
                       'Clock qualification, failover and timing noninterference require separate checks.']
        }, indent=2) + '\n')
    print((out / 'run.log').read_text(), end='')
    print((out / 'hdop-run.log').read_text(), end='')
    print('PASS independent Python GD2/GD1 body/context/HMAC known-answer checks')


if __name__ == '__main__':
    main()
