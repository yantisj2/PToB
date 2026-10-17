#!/usr/bin/env python3
"""Verify selected research identity, exported populations and hypothetical models.

Copyright (c) 2026 Jonathan Yantis. MIT.
No hardware, network, private captures or installed target dependencies.
"""
import csv
import hashlib
import json
import math
import re
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


def source_prose_words(source):
    source = source[source.index(r'\begin{document}'):source.index(r'\begin{thebibliography}')]
    source = re.sub(r'\\begin\{(table|figure|equation)\}.*?\\end\{\1\}', '', source, flags=re.S)
    source = re.sub(r'\\(?:[A-Za-z]+|.)\*?(?:\[[^\]]*\])?', '', source)
    return len(re.sub(r'[{}$&~_^]', ' ', source).split())


def verify_current_edition():
    edition = json.loads((ROOT / 'papers/editions/usb-macos-2026-10-17.json').read_text())
    for entry in edition['files'] + edition['evidence']:
        require(hashlib.sha256((ROOT / entry['path']).read_bytes()).hexdigest() == entry['sha256'],
                'current edition identity: ' + entry['path'])
    paths = {entry['path']: entry['sha256'] for entry in edition['files']}
    require(set(paths) == {'papers/usb-macos-time-transfer-preprint.tex',
                           'papers/usb-macos-time-transfer-preprint.pdf'}, 'current source/PDF pair')
    require(edition['review']['compiled_source_sha256'] == paths['papers/usb-macos-time-transfer-preprint.tex']
            and edition['review']['exported_pdf_sha256'] == paths['papers/usb-macos-time-transfer-preprint.pdf'],
            'compiled source and exported PDF identities')
    require(len(edition['evidence']) == 1 and edition['evidence'][0]['path'] ==
            'clients/macos/results/2026-10-17-ble-vs-direct.json', 'selected current evidence')
    summary = json.loads((ROOT / edition['evidence'][0]['path']).read_text())
    window, config = summary['comparison_window'], summary['common_configuration']
    require(int(window['end_host_monotonic_ns']) - int(window['start_host_monotonic_ns']) ==
            window['duration_s'] * 1_000_000_000 == 180_000_000_000, 'fixed common 180 s window')
    require(not window['both_extended_trials_complete'], 'incomplete extended trials retained')
    require('SHARED' in config['bus_condition'] and 'NOT CLEAN' in config['bus_condition']
            and 'PENDING' in config['clean_bus_test'], 'shared bus and pending clean-bus validation')
    require((config['requested_USB_Hz'], config['chrony_poll'], config['chrony_filter_samples'],
             config['maxsamples']) == (32, -2, 4, 64), 'matched requested configuration')
    require(not config['OS_clock_adjustment'] and not config['network_ports_enabled'], 'user clock scope')
    source = (ROOT / 'papers/usb-macos-time-transfer-preprint.tex').read_text()
    for role in ('RC', 'DC'):
        result = summary['results'][role]
        counts, rx, direct = result['sample_counts'], result['RX'], result['delivered_minus_fine']
        require(counts['sample_events'] == config['requested_USB_Hz'] * window['duration_s'] == 5760,
                role + ' expected USB replies')
        require(counts['valid_precision_records'] + counts['unavailable_precision_records'] +
                counts['missing_precision_replies'] == counts['sample_events'], role + ' reply ledger')
        require(counts['hardware_accepted'] + counts['hardware_rejected'] == counts['sample_events'],
                role + ' hardware ledger')
        require(rx['n'] + rx['warmup_observations'] + rx['unqualified_observations'] +
                rx['withheld_or_rejected_observations'] == counts['sample_events'], role + ' RX ledger')
        require(result['delivered_status_samples'] + result['withheld_status_samples'] ==
                result['status_samples'] == result['chrony_estimator_RMS']['n'] == 177, role + ' status ledger')
        require(direct['n'] <= result['delivered_status_samples'] and
                direct['p99_absolute_ns'] <= direct['maximum_absolute_ns'], role + ' comparison population')
        require(abs(rx['rmse_ns'] ** 2 - rx['mean_ns'] ** 2 - rx['sigma_ns'] ** 2) < 1e-5,
                role + ' RMS retains mean')
        require(not rx['detrended'] and counts['source_scope_count'] == 1 and
                counts['map_mismatches'] == 0, role + ' source population')
        for metric in (rx['rmse_ns'], direct['rms_ns']):
            require(f'{metric:.3f}' in source, role + ' manuscript rounded RMS claim')
        for metric in (rx['n'], rx['warmup_observations'], counts['hardware_rejected']):
            require(f'{metric:,}' in source, role + ' manuscript population claim')
        print(f"shared-bus {role}: RX {rx['n']} predictions / {rx['warmup_observations']} warm-up / "
              f"{counts['hardware_rejected']} rejects; direct {direct['n']} at {direct['rms_ns']:.6f} ns RMS; "
              f"status {result['delivered_status_samples']}/{result['status_samples']}")
    after, before = edition['length']['after'], edition['length']['before']
    require(source_prose_words(source) == after['source_prose_words'] <= before['source_prose_words'],
            'paper prose does not grow')
    require(after['pages'] <= before['pages'] and after['pdf_extracted_words'] <= before['pdf_extracted_words'],
            'recorded PDF page and visible-word limits')
    require(edition['status'] == 'RESEARCH_DRAFT_NOT_SUBMITTED_NOT_PUBLICATION', 'draft status retained')
    previous = edition['previous_edition']
    require(re.fullmatch(r'[0-9a-f]{40}', previous['revision']) is not None, 'full prior revision identity')
    try:
        subprocess.run(['git', 'cat-file', '-e', previous['revision'] + '^{commit}'], cwd=ROOT,
                       check=True, capture_output=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        raise SystemExit('FAIL: prior paper revision unavailable; use Git history including ' + previous['revision'])
    require({entry['path'] for entry in previous['files']} == set(paths), 'prior pair paths')
    return previous


def imported_bytes(entry, previous):
    current = (ROOT / entry['path']).read_bytes()
    if hashlib.sha256(current).hexdigest() == entry['sha256']:
        return current
    prior = {item['path']: item['sha256'] for item in previous['files']}
    require(prior.get(entry['path']) == entry['sha256'], 'frozen import identity: ' + entry['path'])
    try:
        return subprocess.check_output(['git', 'show', previous['revision'] + ':' + entry['path']], cwd=ROOT,
                                       stderr=subprocess.PIPE)
    except subprocess.CalledProcessError:
        raise SystemExit('FAIL: preserved prior paper bytes unavailable: ' + entry['path'])


def main():
    previous = verify_current_edition()
    manifest = json.loads((ROOT / 'papers/imports/research-2026-10-16.json').read_text())
    for entry in manifest['files']:
        require(hashlib.sha256(imported_bytes(entry, previous)).hexdigest() == entry['sha256'], entry['path'])
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
    print(f"PASS: current source/PDF/evidence identities and accounting; {len(manifest['files'])} frozen imported hashes, historical delivery/tracking populations and four hypothetical model outputs")
    print('Not verified here: raw USB forecast reconstruction, independent UTC accuracy, continuous availability or physical hop scaling.')


if __name__ == '__main__':
    main()
