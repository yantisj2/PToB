#!/usr/bin/env python3
"""Verify selected USB observations without hardware or private captures.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'evidence/usb/2026-10-17'


def require(ok, description):
    if not ok:
        raise SystemExit('FAIL: ' + description)


def close(observed, expected, description):
    require(math.isfinite(observed) and abs(observed - expected) < 1e-8, description)


def rms(values):
    return math.sqrt(math.fsum(x * x for x in values) / len(values))


def main():
    manifest = json.loads((BASE / 'manifest.json').read_text())
    require({x['file'] for x in manifest['files']} == {'rx.csv', 'status.csv'}, 'selected CSV files')
    rows = {}
    for entry in manifest['files']:
        path = BASE / entry['file']
        require(hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], entry['file'] + ' identity')
        rows[entry['file']] = list(csv.DictReader(path.open(newline='')))
        require(len(rows[entry['file']]) == entry['rows'], entry['file'] + ' denominator')
    summary_path = ROOT / manifest['summary']['path']
    require(hashlib.sha256(summary_path.read_bytes()).hexdigest() == manifest['summary']['sha256'],
            'comparison summary identity')
    summary = json.loads(summary_path.read_text())
    duration = summary['comparison_window']['duration_s'] * 1_000_000_000
    require(duration == 180_000_000_000, 'fixed window duration')
    require(all(row['profile'] in ('RC', 'DC') for group in rows.values() for row in group), 'profile names')
    for role in ('RC', 'DC'):
        reference = summary['results'][role]
        rx = [x for x in rows['rx.csv'] if x['profile'] == role]
        status = [x for x in rows['status.csv'] if x['profile'] == role]
        for group in (rx, status):
            require([int(x['index']) for x in group] == list(range(1, len(group) + 1)), role + ' row order')
            ticks = [int(x['elapsed_ns']) for x in group]
            require(all(0 <= x < duration for x in ticks) and
                    all(a < b for a, b in zip(ticks, ticks[1:])), role + ' elapsed coordinates')
        flags = ('valid_reply', 'hardware_accepted', 'hardware_rejected', 'rx_warmup',
                 'rx_unqualified', 'rx_rejected')
        require(all(x[k] in ('0', '1') for x in rx for k in flags), role + ' USB flags')
        require(all(x['user_clock_delivered'] in ('0', '1') and x['synchronized'] == '1'
                    for x in status), role + ' status flags')
        require(all(int(x['hardware_accepted']) + int(x['hardware_rejected']) == 1 and
                    int(x['rx_warmup']) + int(x['rx_unqualified']) + int(x['rx_rejected']) +
                    bool(x['rx_error_ns']) == 1 for x in rx), role + ' per-row accounting')
        counts = reference['sample_counts']
        require(len(rx) == counts['sample_events'] == 5760, role + ' sample events')
        for column, key in [('valid_reply', 'valid_precision_records'),
                            ('hardware_accepted', 'hardware_accepted'),
                            ('hardware_rejected', 'hardware_rejected')]:
            require(sum(int(x[column]) for x in rx) == counts[key], role + ' ' + key)
        require(counts['unavailable_precision_records'] == counts['missing_precision_replies'] == 0,
                role + ' no unavailable/missing replies')
        expected = reference['RX']
        for column, key in [('rx_warmup', 'warmup_observations'),
                            ('rx_unqualified', 'unqualified_observations'),
                            ('rx_rejected', 'withheld_or_rejected_observations')]:
            require(sum(int(x[column]) for x in rx) == expected[key], role + ' ' + key)
        errors = [float(x['rx_error_ns']) for x in rx if x['rx_error_ns']]
        require(len(errors) == expected['n'], role + ' RX predictions')
        close(rms(errors), expected['rmse_ns'], role + ' RX RMS')
        close(statistics.mean(errors), expected['mean_ns'], role + ' RX mean')
        close(statistics.pstdev(errors), expected['sigma_ns'], role + ' RX sigma')
        require(len(status) == reference['status_samples'] == 177, role + ' status denominator')
        delivered = sum(int(x['user_clock_delivered']) for x in status)
        require(delivered == reference['delivered_status_samples'] and
                len(status) - delivered == reference['withheld_status_samples'], role + ' availability')
        direct = [int(x['chrony_minus_fine_ns']) for x in status if x['chrony_minus_fine_ns']]
        expected = reference['delivered_minus_fine']
        require(len(direct) == expected['n'], role + ' direct comparisons')
        close(rms(direct), expected['rms_ns'], role + ' direct RMS')
        close(statistics.mean(direct), expected['mean_ns'], role + ' direct mean')
        absolute = sorted(abs(x) for x in direct)
        require(absolute[math.ceil(.99 * len(absolute)) - 1] == expected['p99_absolute_ns'] and
                absolute[-1] == expected['maximum_absolute_ns'], role + ' direct tails')
        tracking = [float(x['estimator_rms_ns']) for x in status]
        expected = reference['chrony_estimator_RMS']
        require(len(tracking) == expected['n'], role + ' tracking readouts')
        for observed, key in [(min(tracking), 'minimum_ns'), (statistics.median(tracking), 'median_ns'),
                              (max(tracking), 'maximum_ns')]:
            close(observed, expected[key], role + ' tracking ' + key)
        print(f'{role}: {len(errors)} causal RX predictions, {len(direct)} direct comparisons, '
              f'{delivered}/{len(status)} delivered status points; statistics match')
    print('PASS: sanitized CSV identities, populations and published summary statistics')
    print('Not verified: private raw extraction, independent UTC, continuous output or clean-bus performance.')


if __name__ == '__main__':
    main()
