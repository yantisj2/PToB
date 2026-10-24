#!/usr/bin/env python3
"""Recompute the selected W12 baseline; standard library, no hardware access.

Copyright 2026 Jonathan Yantis. MIT license.
"""
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'evidence/s3/2026-10-24-w12-utc-basis'


def metrics(values):
    if not values:
        return {'n': 0}
    absolute = sorted(map(abs, values))
    return dict(n=len(values), mean=statistics.mean(values),
                rms=math.sqrt(statistics.mean(x*x for x in values)),
                centered_std=statistics.pstdev(values), minimum=min(values),
                maximum=max(values), max_abs=max(absolute),
                p95_abs=absolute[math.ceil(.95*len(values))-1],
                p99_abs=absolute[math.ceil(.99*len(values))-1],
                ge100=sum(x >= 100 for x in absolute),
                ge200=sum(x >= 200 for x in absolute))


def summary(rows):
    eligible = [r for r in rows if r['eligible'] == '1']
    assert all(r['phase_ns'] == '' for r in rows if r not in eligible)
    return dict(logged=len(rows), eligible=len(eligible),
                unscorable=len(rows)-len(eligible),
                reasons=dict(collections.Counter(r['reason'] for r in rows if r not in eligible)),
                phase_ns=metrics([int(r['phase_ns']) for r in eligible]))


def main():
    manifest = json.loads((DATA/'manifest.json').read_text())
    assert manifest['license'] == 'CC-BY-4.0'
    for entry in manifest['files']:
        raw = (DATA/entry['path']).read_bytes()
        assert len(raw) == entry['bytes']
        assert hashlib.sha256(raw).hexdigest() == entry['sha256'], entry['path']
    result = json.loads((DATA/'results.json').read_text())
    populations = {}
    for key, name, count in [('w12', 'w12-pps.csv', 600),
                             ('downstream', 'downstream-pps.csv', 600),
                             ('sampled_tx', 'sampled-tx.csv', 120)]:
        with (DATA/name).open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        populations[key] = rows
        assert len(rows) == count
        assert summary(rows) == result[key], key
        for window in result['windows']:
            selected = [r for r in rows if int(r['window']) == window['window']]
            assert [int(r['sample']) for r in selected] == list(range(1, count//2+1))
            assert summary(selected) == window[key]
    w12 = populations['w12']
    for r in w12:
        assert int(r['eligible']) == int(all(r[k] == '1' for k in
                                            ('map_valid', 'reference', 'following', 'permission')))
        assert r['receive_adjustment_ns'] == '7671'
        if r['eligible'] == '1':
            assert r['utc_offset_fraction_ns'] == '0'
            assert r['phase_ns'] == r['utc_phase_ns']
    assert result['utc_fraction_checks_passed'] == 522
    for source in result['w12_sources']:
        assert summary([r for r in w12 if r['source'] == source['source']]) == {
            k: v for k, v in source.items() if k != 'source'}
    for r in populations['downstream']:
        assert int(r['eligible']) == int(all(r[k] == '1' for k in ('map_valid', 'reference', 'lineage')))
        assert r['receiver_adjustment_ns'] == '10543'
        if r['eligible'] == '1':
            assert r['phase_ns'] == r['utc_phase_ns']
            assert r['nearest_second_minus_nmea_ns'] == '0'
    for r in populations['sampled_tx']:
        assert r['relay_adjustment_ns'] == '4026'
        assert r['receive_adjustment_ns'] == '7671'
        assert r['eligible'] != '1' or r['matched'] == '1'
    with (DATA/'gnss-prediction.csv').open(newline='') as stream:
        predictions = list(csv.DictReader(stream))
    assert len(predictions) == 600
    for kind in ('raw', 'mapped'):
        assert metrics([int(r[kind+'_error_ns']) for r in predictions
                        if r[kind+'_eligible'] == '1']) == result['gnss_prediction'][kind]
    assert result['timing_gate'] == 'NOT_MET'
    assert result['offsets_refit'] is False and result['gm_work_started'] is False
    print('PASS: 600 W12 PPS, 600 downstream PPS, 120 selected TX, 600 GNSS predictions; all windows, sources, invalids, tails and UTC fractions retained.')


if __name__ == '__main__':
    main()
