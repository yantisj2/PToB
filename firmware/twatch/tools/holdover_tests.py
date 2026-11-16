#!/usr/bin/env python3
"""Watch sampled P99 observer and actual candidate propagation; offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compiler = os.environ.get('CXX') or ('/opt/homebrew/opt/llvm/bin/clang++'
                 if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else shutil.which('clang++'))
    if not compiler:
        parser.error('A C++20 host compiler is required')
    includes = ['firmware/twatch/main', 'firmware/n30/main', 'firmware/w12/main',
                'firmware/w12/legacy', 'firmware/w12/legacy/reference',
                'firmware/w12/main/r2/r2-g1', 'firmware/w12/main/r2/rc7/include',
                'firmware/w12/tests/console_fakes']
    sources = [APP / 'main/watch_holdover.cpp', APP / 'tests/holdover_test.cpp',
               REPO / 'firmware/n30/main/ui_model.cpp']
    command = [compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-DPTOB_N30=1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               *['-I' + str(REPO / path) for path in includes],
               *map(str, sources), '-o', str(out / 'holdover')]
    (out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    with (out / 'compile.log').open('w') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=60)
    result = subprocess.run([str(out / 'holdover')], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30)
    (out / 'runtime.log').write_text(result.stdout)
    result.check_returncode()
    sources += [Path(__file__), APP / 'main/empirical_holdover.hpp',
                APP / 'main/watch_holdover.hpp', APP / 'main/radio_candidates.hpp',
                APP / 'main/mobile_time.hpp', APP / 'main/watch.hpp',
                REPO / 'firmware/w12/main/n30_timing_probe.hpp',
                REPO / 'firmware/w12/main/g1_ble_eligibility.hpp',
                REPO / 'firmware/w12/main/g1_clock_prediction.hpp']
    (out / 'receipt.json').write_text(json.dumps({
        'status': 'PASS', 'scope': 'SYNTHETIC_FUTURE_PHYSICAL_PAIRS_AND_PRODUCTION_ADAPTER_ASAN_UBSAN',
        'hardware_operated': False, 'servo_changed': False,
        'statistic': 'SAMPLED_PREFIX_P99_NOT_CONFIDENCE_GUARANTEE',
        'source_sha256': {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in sources}}, indent=2) + '\n')
    print(result.stdout, end='')


if __name__ == '__main__':
    main()
