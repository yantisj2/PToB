#!/usr/bin/env python3
"""Actual dual-radio projections/controller/runtime, ASan/UBSan; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cxx = os.environ.get('CXX', '/opt/homebrew/opt/llvm/bin/clang++')
    cc = cxx.replace('clang++', 'clang')
    flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    includes = ['twatch/tests/dual_fakes', 'twatch/tests/shadow_fakes', 'twatch/main', 'n30/main', 'w12/main', 'w12/legacy', 'w12/main/r2/r2-g1', 'w12/main/r2/rc7/include']
    inc = ['-I' + str(APP.parent / d) for d in includes]
    sources = [APP / 'tools/dual_clock_tests.py', APP / 'main/dual_clock.hpp', APP / 'main/dual_radio.cpp', APP / 'main/dual_radio.hpp', APP / 'main/dual_profile.hpp', REPO / 'firmware/n30/main/lora_thermal.c']
    with (out / 'compile.log').open('w') as log:
        subprocess.run([cc, '-std=c11', *flags, *inc, '-c', str(sources[-1]), '-o', str(out / 'thermal.o')], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        for case in ('dual_clock_test', 'dual_runtime_test', 'dual_tracking_test'):
            source = APP / 'tests' / (case + '.cpp')
            sources.append(source)
            subprocess.run([cxx, '-std=c++20', *flags, *inc, '-DPTOB_TWATCH=1', '-DPTOB_N30=1', '-DV12_R2_ENABLED=1', str(source), str(out / 'thermal.o'), '-o', str(out / case)], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
            with (out / (case + '.log')).open('w') as output:
                subprocess.run([str(out / case)], stdout=output, stderr=subprocess.STDOUT, check=True, timeout=20)
    runtime_logs = ['dual_runtime_test.log']
    for label, correction in [('active', 3600000), ('active-negative', -3600000)]:
        active = out / label
        active.mkdir()
        (active / 'twatch_dual_clock_config.h').write_text('#pragma once\n#include "dual_profile.hpp"\nnamespace watchdual {static constexpr Profile profile{true,10,0,11,12,0,200,' + str(correction) + ',2900};}\n')
        with (out / (label + '-compile.log')).open('w') as log:
            subprocess.run([cxx, '-std=c++20', *flags, '-I' + str(active), *inc, '-DPTOB_TWATCH=1', '-DPTOB_N30=1', '-DV12_R2_ENABLED=1', '-DPTOB_DUAL_TEST_ENABLED=1', str(APP / 'tests/dual_runtime_test.cpp'), str(out / 'thermal.o'), '-o', str(out / (label + '-runtime'))], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        with (out / (label + '-runtime.log')).open('w') as log:
            subprocess.run([str(out / (label + '-runtime'))], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=20)
        runtime_logs.append(label + '-runtime.log')
    # Reproduce failover against the actual adapter with an enabled profile.
    failover = APP / 'tests/dual_failover_test.cpp'
    sources.append(failover)
    with (out / 'failover-compile.log').open('w') as log:
        subprocess.run([cxx, '-std=c++20', *flags, '-I' + str(out / 'active'), *inc,
                        '-DPTOB_TWATCH=1', '-DPTOB_N30=1', '-DV12_R2_ENABLED=1',
                        str(failover), str(out / 'thermal.o'), '-o', str(out / 'failover')],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    with (out / 'failover.log').open('w') as log:
        subprocess.run([str(out / 'failover')], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=20)
    study = out / 'study'
    study.mkdir()
    (study / 'twatch_dual_clock_config.h').write_text('#pragma once\n#include "dual_profile.hpp"\nnamespace watchdual {static constexpr Profile profile{true,10,0,11,12,0,200,3599390,2900,true};}\n')
    with (out / 'study-compile.log').open('w') as log:
        subprocess.run([cxx, '-std=c++20', *flags, '-I' + str(study), *inc,
                        '-DPTOB_TWATCH=1', '-DPTOB_N30=1', '-DV12_R2_ENABLED=1',
                        str(failover), str(out / 'thermal.o'), '-o', str(out / 'study-runtime')],
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
    with (out / 'study-runtime.log').open('w') as log:
        subprocess.run([str(out / 'study-runtime')], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=20)
    sys.path.insert(0, str(APP / 'tests'))
    import dual_wire_test as wire
    import dual_radio_test as analysis
    for logname in ('failover.log', 'study-runtime.log'):
        line = next(x for x in (out / logname).read_text().splitlines() if x.startswith('TWATCH_CLOCK_SNAPSHOT '))
        manifest = dict(wire.MANIFEST, watch_frame='CLOCK', dual_radio_test_contracts=['TWATCH_GNSS_FREE_SNAPSHOT_V1'])
        raw = wire.response(radio_row=line)
        parser = wire.decoder.console.Parser(wire.NONCE, manifest, wire.BOOT)
        frames = []
        for i in range(0, len(raw), 31):
            frames += parser.feed(raw[i:i+31])
        assert len(frames) == 1
        sample = json.loads(frames[0]['rows']['TWATCH_CLOCK_SNAPSHOT']['json'])
        assert not sample['gnss_witness'] and 'pps' not in sample
        assert sample['tick'] == sample['ble']['edge_tick'] == sample['lora']['edge_tick']
        assert sample['tick'] == sample['clock']['counter_tick']
        assert not sample['clock']['calibrated']
        assert sample['lora']['current_correction_ns'] == (3599390 if logname.startswith('study') else 0)
        try:
            wire.decoder.console.Parser(wire.NONCE, manifest, wire.BOOT).feed(raw.replace(b'gnss_witness', b'gnss_witnezs'))
        except RuntimeError:
            pass
        else:
            raise AssertionError('Snapshot CRC corruption accepted')
    for logname in runtime_logs:
        line = next(line for line in (out / logname).read_text().splitlines() if line.startswith('TWATCH_DUAL_PPS '))
        entry = wire.entry()
        entry['response_hex'] = wire.response(radio_row=line).hex()
        rows, decoded = wire.decoder.decode([entry], wire.MANIFEST, wire.BOOT)
        result = analysis.analyze(rows)
        assert decoded['samples'] == 1 and sum(group['samples'] for group in result['statistics']) == 2
        assert rows[0]['ble']['utc_ns'] > 0 and rows[0]['lora']['utc_ns'] > 0
        assert abs(rows[0]['lora']['utc_ns'] - rows[0]['ble']['utc_ns']) < 4000000
        (out / (logname + '.analysis.json')).write_text(json.dumps(result, indent=2) + '\n')
    receipt = dict(status='PASS', hardware_access=False, suites=['ACTUAL_CONTROLLER', 'ACTUAL_PARALLEL_PPS_RUNTIME'], sources={str(q.relative_to(REPO)): hashlib.sha256(q.read_bytes()).hexdigest() for q in sources})
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS actual dual clock and parallel PPS runtime (ASan/UBSan)')


if __name__ == '__main__':
    main()
