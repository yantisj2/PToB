#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Reproduce the indoor PNT draft's analysis and edition identity offline.

No device, network, private capture, third-party Python dependency or firmware
build is used. Analytical examples are not localization or timing qualification.
"""
import csv
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'papers/indoor-pnt/main.tex'
EDITION = ROOT / 'papers/editions/indoor-pnt-2026-10-26.json'
PUBLICATION = ROOT / 'papers/editions/indoor-pnt-2026-10-26-publication.json'
C = F(299792458)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def rows(path):
    with (ROOT / path).open(newline='') as stream:
        return list(csv.DictReader(stream))


def stats(values):
    require(bool(values), 'empty eligible population')
    n = len(values)
    mean = sum(values) / n
    rms = math.sqrt(sum(v*v for v in values) / n)
    std = math.sqrt(sum((v-mean)**2 for v in values) / n)
    require(math.isclose(rms*rms, mean*mean + std*std, rel_tol=1e-12),
            'bias-inclusive RMS identity')
    return n, mean, rms, max(map(abs, values))


def evidence():
    directory = 'evidence/s3/2026-10-24-w12-utc-basis/'
    expected = [
        ('w12-pps.csv', None, 600, 522, 60.328, 180, 40.695),
        ('w12-pps.csv', '2', 300, 300, 38.312, 121, 20.737),
        ('downstream-pps.csv', None, 600, 451, 61.922, 211, -5.545),
        ('sampled-tx.csv', None, 120, 58, 108.306, 226, 13.931),
    ]
    result = []
    for name, window, total, count, rms, maximum, mean in expected:
        selected = [r for r in rows(directory+name) if window is None or r['window'] == window]
        values = [int(r['phase_ns']) for r in selected if r['eligible'] == '1']
        n, m, s, peak = stats(values)
        require(len(selected) == total and n == count, name+' population')
        require(round(s, 3) == rms and peak == maximum and round(m, 3) == mean,
                name+' statistics')
        result.append({'file': name, 'window': window, 'eligible': n,
                       'total': total, 'rms_ns': round(s, 3), 'max_ns': peak})
    fine = rows('evidence/s3/2026-10-22-fine/pps-sanitized.csv')
    for cohort, count, total, rms, peak in [
        ('fine50909_fixed660s', 657, 660, 166.590, 1591),
        ('n31_51016_transfer48', 48, 64, 150.954, 362),
    ]:
        selected = [r for r in fine if r['cohort'] == cohort]
        n, mean, s, p = stats([int(r['phase_ns']) for r in selected if r['valid'] == '1'])
        require(len(selected) == total and n == count and round(s, 3) == rms and p == peak,
                cohort+' statistics/accounting')
        result.append({'cohort': cohort, 'eligible': n, 'total': total, 'rms_ns': round(s, 3)})
    v14 = json.loads((ROOT / 'evidence/v14/2026-10-12/summary.json').read_text())
    v = v14['reference_valid_causal']
    require([v['n'], v14['invalid_reference'], v14['reference_valid_expired'],
             v14['unobserved_conservative']] == [436, 65, 3, 98], 'V14 classes')
    require(sum([436, 65, 3, 98]) == v14['nominal_boundaries'] == 602, 'V14 total')
    require(round(v['rms_including_bias_ns'], 3) == 62.504 and v['abs_max_ns'] == 191,
            'V14 eligible score')
    require(v14['all_causal']['n'] == 501 and
            round(v14['all_causal']['rms_including_bias_ns']/1e6, 3) == 3.929,
            'V14 invalid references retained separately')
    require(round(v14['max_witness_gap_s'], 3) == 59.017, 'V14 witness gap')
    return result


def rank(matrix):
    a = [[F(v) for v in row] for row in matrix]
    r = 0
    for c in range(len(a[0])):
        pivot = next((i for i in range(r, len(a)) if a[i][c]), None)
        if pivot is None:
            continue
        a[r], a[pivot] = a[pivot], a[r]
        d = a[r][c]
        a[r] = [v/d for v in a[r]]
        for i in range(len(a)):
            if i != r:
                scale = a[i][c]
                a[i] = [v-scale*w for v, w in zip(a[i], a[r])]
        r += 1
        if r == len(a):
            break
    return r


def analytical():
    # Exact unit arithmetic: one-way range difference, not half round trip.
    for seconds, motion_mm, radio_m in [
        (F(1, 10**10), F('.0000002'), F('.0299792458')),
        (F(1, 10**9), F('.000002'), F('.299792458')),
        (F(1, 10**7), F('.0002'), F('29.9792458')),
        (F(1, 10**6), F('.002'), F('299.792458')),
        (F(1, 1000), F(2), F('299792.458')),
    ]:
        require(2 * seconds * 1000 == motion_mm and C*seconds == radio_m, 'unit conversion')
    require(round(float(F('.1')/C*10**12), 3) == 333.564, '0.1 m budget ps')
    require(round(float(1/C*10**9), 3) == 3.336, '1 m budget ns')
    require(round(float(30/C*10**9), 3) == 100.069, '30 m propagation ns')
    require(round(float(C*F('60.328')/10**9), 3) == 18.086, 'illustrative range difference')
    require(round(1000*math.pi*F('180')/180*F('.001'), 3) == 3.142, 'lever arm mm')
    require(F(343)*F(100, 10**6)*1000 == F('34.3'), 'acoustic propagation mm')
    require(F(1, 2)*F(1, 10**6)*F(1, 1000)*10**9 == F('.5'), 'TWR turnaround term ns')
    require(round(float(C*F('.5')/10**9), 3) == .150, 'TWR range term m')
    # Offset cancels in elapsed intervals only after compatible rate conversion.
    distance = F(9)
    flight = distance/C
    delay = F(1, 1000)
    rate_a, rate_b = F(1000001, 1000000), F(999999, 1000000)
    measured_round = rate_a*(2*flight + delay)
    measured_delay = rate_b*delay
    require((measured_round/rate_a - measured_delay/rate_b)/2 == flight, 'TWR rate correction')
    require((measured_round-measured_delay)/2 != flight, 'unrelated clock intervals rejected')
    # Exact independent expressions of the two-stage bridge and validity inverse.
    ak, b = F(1, 20_000_000), F(5, 4)
    tk, qk, qb, ub = F(123), 11, 19, 7
    for u in (F(0), F(7), F(31, 2), F(100000000)):
        q = qb + b*(u-ub)
        direct = tk + ak*(q-qk)
        composed = tk + ak*(qb-qk + b*(u-ub))
        require(direct == composed, 'cross-clock composition')
        require(ub + ((direct-tk)/ak + qk-qb)/b == u, 'inverse bridge')
    # Transform q interval into u before intersecting; never mix counter domains.
    q_valid = (F(19), F(44))
    mapped = tuple(ub+(v-qb)/b for v in q_valid)
    bridge_valid = (F(10), F(30))
    valid = max(mapped[0], bridge_valid[0]), min(mapped[1], bridge_valid[1])
    require(valid == (10, 27) and not valid[0] <= 28 <= valid[1], 'expiry intersection')
    require(max(2, 3, 5) <= 6 and not max(2, 3, 7) <= 6, 'causal decision fence')
    terms = [F(1, 10), None, F(2)]
    total_bound = None if any(v is None for v in terms) else sum(terms)
    require(total_bound is None, 'unknown bound may not become zero')
    require(round(math.sqrt(2*60**2), 3) == 84.853, 'independent differential std')
    require(round(math.sqrt(2*60**2*(1-.9)), 3) == 26.833, 'correlated differential std')
    # Full-rank range Jacobian at origin for anchors on all three axes;
    # anchors in the xy plane at z=0 lose instantaneous vertical observability.
    planar = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0]]
    require(rank(planar) == 2 and rank(planar + [[0, 0, 1], [0, 0, -1]]) == 3,
            'geometry rank example')
    e0, rate = F('.2')/10**6, F('.5')/10**6
    require((F(10, 10**6)-e0)/rate == F('19.6'), '10 us holdover')
    require((F(1, 10**6)-e0)/rate == F('1.6'), '1 us holdover')
    require(e0 > F('.1')/C, 'fine budget already exhausted')
    require(32*100*50 == 160000 and 32*100*50*8 == 1280000, 'payload only data rate')
    require(30*60*100 == 180000 and 30*60*10 == 18000, 'pilot denominator')
    require(F('0.99')*18000 == 17820 and 18000-17820 == 180, 'availability accounting')
    require(F(180+1, 10) > F('.5'), 'clustered missingness violates gap limit')
    require(F(600)/12 == 50, 'cost break-even')
    require([600-12*n for n in (25, 50, 100, 250)] == [300, 0, -600, -2400], 'cost sensitivity')
    source = SOURCE.read_text()
    for literal in ('333.564', '3.336', '100.069', '18.086', '3.142', '34.3', '84.853',
                    '26.833', '19.6', '1.6', '17\\,820', '18\\,000', '180\\,000'):
        require(literal in source, 'computed example absent from source: '+literal)
    return {'analytical_examples': 'PASS', 'unknown_bounds_preserved': True,
            'geometry_rank': [2, 3], 'new_localization_measurements': False}


def identity():
    edition = json.loads(EDITION.read_text())
    entries = edition['files'] + edition['cited_local_sources']
    if PUBLICATION.is_file():
        publication = json.loads(PUBLICATION.read_text())
        require(publication['status'] == 'APPROVED_FOR_PUBLIC_DRAFT_REVIEW' and
                publication['prepared_edition'] == str(EDITION.relative_to(ROOT)) and
                publication['prepared_edition_sha256'] == hashlib.sha256(EDITION.read_bytes()).hexdigest(),
                'immutable preparation receipt identity or approval status')
        published = {entry['path']: entry for entry in publication['publication_files']}
        require(set(published) == {entry['path'] for entry in edition['files']},
                'publication file allowlist')
        ancillary = {'papers/indoor-pnt/README.md', 'tools/verify_indoor_pnt_paper.py'}
        for entry in edition['files']:
            if entry['path'] not in ancillary:
                require(published[entry['path']] == entry, 'reviewed artifact changed')
        entries = [published[entry['path']] for entry in edition['files']] + edition['cited_local_sources']
    for entry in entries:
        path = ROOT / entry['path']
        require(path.is_file(), 'missing edition file: '+entry['path'])
        data = path.read_bytes()
        require(len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'],
                'edition identity mismatch: '+entry['path'])
    source = SOURCE.read_text()
    items = re.findall(r'\\bibitem\{([^}]+)\}', source)
    keys = set(items)
    cited = {k for group in re.findall(r'\\cite\{([^}]+)\}', source) for k in group.split(',')}
    require(len(items) == len(keys) == 16 and cited == keys, 'bibliography keys')
    require(not re.search(r'\\(?:input|include)\s*\{', source), 'non-standalone source input')
    require(not any(x in source for x in ('.internal/', '/Users/', 'ble-clock-v2',
                                         'TODO', 'TBD', 'turn0search')), 'private path or placeholder')
    require(edition['status'] == 'AUTHOR_REVIEW_DRAFT_PUBLICATION_APPROVAL_PENDING', 'review status')
    require((ROOT / 'papers/indoor-pnt/main.pdf').read_bytes().startswith(b'%PDF-'), 'PDF signature')
    return {'sealed_files': len(entries), 'bibliography_entries': len(keys)}


def main():
    require(len(sys.argv) == 1, 'usage: python3 tools/verify_indoor_pnt_paper.py')
    print(json.dumps({'status': 'PASS_OFFLINE_ANALYSIS_AND_EDITION_IDENTITY',
                      'analysis': analytical(), 'evidence': evidence(), 'identity': identity()}, indent=2))


if __name__ == '__main__':
    main()
