#!/usr/bin/env python3
"""Run the complete offline watch regression suite; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[1]
SUITES = [
    'test', 'calendar_tests', 'ble_aux_gate_tests', 'ble_clock_restart_tests',
    'ble_expiry_tests', 'ble_power_model_tests', 'ble_power_transport_tests',
    'console_input_tests', 'fine_tests', 'holdover_tests', 'imu_fifo_tests',
    'lora_shadow_tests', 'maintenance_tests', 'mobile_policy_tests',
    'radio_owner_tests', 'radio_tests', 'roaming_tests', 'servo_tests',
    'watchdog_tests', 'energy_tests', 'shadow_tests',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf', type=Path, default=os.environ.get('IDF_PATH'))
    parser.add_argument('--out', type=Path)
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    cases = [(name, APP / 'tools' / (name + '.py'), True) for name in SUITES]
    cases += [(name, APP / 'tests' / (name + '.py'), False)
              for name in ('power_capture_test', 'roaming_capture_test', 'dual_radio_test_test')]
    if args.list:
        print(json.dumps([name for name, _, _ in cases], indent=2))
        return
    if not args.idf or not args.idf.is_dir():
        parser.error('--idf or IDF_PATH must select the public ESP-IDF cache; see docs/twatch/build.md')
    if not args.out or REPO / 'artifacts' not in args.out.resolve().parents:
        parser.error('--out must be a fresh directory under ignored artifacts/')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1',
           'IDF_PATH': str(args.idf.resolve())}
    receipt = {'status': 'RUNNING', 'hardware_access': False, 'checks': []}
    try:
        for name, script, output in cases:
            command = [sys.executable, '-B', str(script)]
            if output:
                command += ['--out', str(out / name)]
            if name == 'radio_tests':
                command += ['--idf', env['IDF_PATH']]
            start = time.monotonic()
            with (out / (name + '.log')).open('w') as log:
                result = subprocess.run(command, cwd=REPO, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=360)
            receipt['checks'].append({'name': name, 'returncode': result.returncode,
                                      'seconds': round(time.monotonic() - start, 3)})
            print(name + (': PASS' if not result.returncode else ': FAIL'), flush=True)
        receipt['status'] = 'FAIL' if any(c['returncode'] for c in receipt['checks']) else 'PASS'
    finally:
        if receipt['status'] == 'RUNNING':
            receipt['status'] = 'INTERRUPTED'
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if receipt['status'] != 'PASS':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
