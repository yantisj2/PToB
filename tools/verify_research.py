#!/usr/bin/env python3
"""Verify selected research identity, exported populations and hypothetical models.

Copyright (c) 2026 Jonathan Yantis. MIT.
No hardware, network, private captures or installed target dependencies.
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'evidence/research/2026-10-16'


def require(ok, description):
    if not ok:
        raise SystemExit('FAIL: ' + description)


def main():
    manifest = json.loads((ROOT / 'papers/imports/research-2026-10-16.json').read_text())
    for entry in manifest['files']:
        require(hashlib.sha256((ROOT / entry['path']).read_bytes()).hexdigest() == entry['sha256'], entry['path'])
    delivery = list(csv.DictReader((BASE / 'usb-chrony/delivery.csv').open()))
    tracking = list(csv.DictReader((BASE / 'usb-chrony/tracking.csv').open()))
    require(len(tracking) == 323, 'tracking denominator')
    for start, total, valid, rms in [(0, 296, 220, 399.373475), (60, 236, 182, 414.351008)]:
        rows = [r for r in delivery if start <= float(r['elapsed_s']) < 300]
        values = [float(r['chrony_minus_fine_ns']) for r in rows if r['chrony_minus_fine_ns']]
        require(len(rows) == total and len(values) == valid, 'delivery population')
        observed = math.sqrt(statistics.mean(x*x for x in values))
        require(abs(observed - rms) < .00001, 'delivery RMS')
        print(f'delivery {start}–300 s: {valid} valid / {total-valid} withheld / {total} total; RMS {observed:.6f} ns')
    settled = [float(r['estimator_RMS_ns']) for r in tracking if 60 <= float(r['elapsed_s']) < 300]
    require((len(settled), min(settled), statistics.median(settled), max(settled)) == (265, 23, 129, 339), 'settled tracking population')
    require(max(float(r['estimator_RMS_ns']) for r in tracking) == 32072201, 'startup transient retained')
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory)
        shutil.copy(BASE / 'viability/derive.py', target / 'derive.py')
        subprocess.run([sys.executable, str(target / 'derive.py')], check=True, stdout=subprocess.DEVNULL)
        for name in ['hop-sensitivity.csv', 'availability-sensitivity.csv', 'holdover-sensitivity.csv', 'model-summary.json']:
            require((target / name).read_bytes() == (BASE / 'viability' / name).read_bytes(), 'model reproduction: ' + name)
    print(f"PASS: {len(manifest['files'])} imported hashes, delivery/tracking populations and four hypothetical model outputs")
    print('Not verified here: raw USB forecast reconstruction, independent UTC accuracy, continuous availability or physical hop scaling.')


if __name__ == '__main__':
    main()
