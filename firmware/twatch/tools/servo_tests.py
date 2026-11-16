#!/usr/bin/env python3
"""Offline watch/N30 software servo parity; no native builder or hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # Keep the production selection under test: a future accidental removal
    # must fail before this harness explicitly supplies the selected definition.
    cmake = APP / 'main/CMakeLists.txt'
    definitions = re.findall(r'target_compile_definitions\(\$\{COMPONENT_LIB\}\s+PRIVATE\s+([^)]*)\)', cmake.read_text())
    assert 'PTOB_N30=1' in ' '.join(definitions).split()
    compiler = os.environ.get('CXX') or ('/opt/homebrew/opt/llvm/bin/clang++'
                 if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else shutil.which('clang++') or shutil.which('c++'))
    if not compiler:
        parser.error('A C++20 host compiler is required')
    flags = [compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             *['-I' + str(REPO / path) for path in
               ['firmware/w12/main', 'firmware/w12/main/r2/r2-g1',
                'firmware/w12/main/r2/rc7/include', 'firmware/w12/legacy',
                'firmware/w12/legacy/reference']]]
    join = REPO / 'firmware/w12/main/r2/r2-g1/group_join.c'
    obj = out / 'join.o'
    cc = os.environ.get('CC') or str(Path(compiler).with_name('clang'))
    common = [x for x in flags[1:] if x != '-std=c++20']
    subprocess.run([cc, '-std=c11', *common, '-ffunction-sections', '-fdata-sections',
                    '-c', str(join), '-o', str(obj)], check=True, timeout=60)
    link = [str(obj), '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections']
    cases = [APP / 'tests/servo_test.cpp',
             REPO / 'firmware/n30/tests/clock_prediction_test.cpp',
             REPO / 'firmware/n30/tests/clock_handoff_test.cpp']
    for source in cases:
        exe = out / source.stem
        subprocess.run([*flags, '-DPTOB_N30=1', str(source), *link, '-o', str(exe)],
                       check=True, timeout=60)
        result = subprocess.run([str(exe)], check=True, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (out / (source.stem + '.log')).write_text(result.stdout)
        print(result.stdout, end='')
    # The same watch regression must reject the legacy decimated/faster-slew
    # branch. Retain its actual failed assertion as an explicit negative control.
    legacy = out / 'legacy_negative'
    subprocess.run([*flags, str(cases[0]), *link, '-o', str(legacy)], check=True, timeout=60)
    result = subprocess.run([str(legacy)], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30)
    (out / 'legacy_negative.log').write_text(result.stdout)
    assert result.returncode == 3 and 'LEGACY_FINE_SLEW' in result.stdout
    inputs = [Path(__file__), cmake, join, *cases]
    inputs += sorted((REPO / 'firmware/w12/main').glob('*.hpp'))
    inputs += sorted((REPO / 'firmware/w12/legacy/reference').glob('*.hpp'))
    receipt = {'status': 'PASS', 'scope': 'SYNTHETIC_SOFTWARE_PARITY_ONLY',
               'hardware_operated': False, 'native_builder_run': False,
               'calibration_transferred': False, 'watch_counter': 'GPTIMER20',
               'servo': 'SHARED_N30_DENSE_CHANNEL_AWARE',
               'legacy_negative_returncode': result.returncode,
               'sources': {str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in inputs}}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS legacy negative control; software parity does not qualify RF/output/UTC accuracy')


if __name__ == '__main__':
    main()
