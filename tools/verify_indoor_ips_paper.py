#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Offline analytical supplement and edition check for the PToB-IPS draft.

Standard library only. No hardware, network, private captures, firmware changes,
or empirical positioning claims. --write-analysis writes the synthetic analytical
JSON to a requested output path; default mode verifies the preserved supplement.
"""
import argparse
import csv
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'papers/indoor-ips/main.tex'
EDITION = ROOT / 'papers/editions/indoor-ips-2026-10-26.json'
ANALYSIS = ROOT / 'papers/indoor-ips/analysis.json'
PUBLICATION = ROOT / 'papers/editions/indoor-ips-2026-10-26-publication.json'
C = 299792458


def require(ok, message):
    if not ok:
        raise ValueError(message)


def inverse(matrix, tolerance=1e-12):
    n = len(matrix)
    a = [list(map(float, row)) + [float(i == j) for j in range(n)]
         for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda i: abs(a[i][col]))
        require(abs(a[pivot][col]) > tolerance, 'singular geometry/normal matrix')
        a[col], a[pivot] = a[pivot], a[col]
        scale = a[col][col]
        a[col] = [v/scale for v in a[col]]
        for i in range(n):
            if i != col:
                scale = a[i][col]
                a[i] = [v-scale*w for v, w in zip(a[i], a[col])]
    return [row[n:] for row in a]


def rank(matrix, tolerance=1e-10):
    a = [list(map(float, row)) for row in matrix]
    r = 0
    for col in range(len(a[0])):
        if r == len(a):
            break
        pivot = max(range(r, len(a)), key=lambda i: abs(a[i][col]))
        if abs(a[pivot][col]) <= tolerance:
            continue
        a[r], a[pivot] = a[pivot], a[r]
        scale = a[r][col]
        a[r] = [v/scale for v in a[r]]
        for i in range(r+1, len(a)):
            scale = a[i][col]
            a[i] = [v-scale*w for v, w in zip(a[i], a[r])]
        r += 1
    return r


def geometry(position, anchors):
    result = []
    for anchor in anchors:
        delta = [p-a for p, a in zip(position, anchor)]
        distance = math.sqrt(sum(v*v for v in delta))
        require(distance > 0, 'receiver coincides with anchor')
        result.append([v/distance for v in delta] + [1.0])
    return result


def normal_matrix(g):
    return [[sum(row[i]*row[j] for row in g) for j in range(len(g[0]))]
            for i in range(len(g[0]))]


def pseudoranges(position, clock_m, anchors):
    return [math.dist(position, anchor)+clock_m for anchor in anchors]


def solve_position(observations, anchors):
    """Noiseless/constructed example: local unweighted Gauss-Newton solve."""
    state = [5.0, 4.0, 1.5, 0.0]
    for _ in range(40):
        predicted = pseudoranges(state[:3], state[3], anchors)
        residual = [z-p for z, p in zip(observations, predicted)]
        g = geometry(state[:3], anchors)
        p = inverse(normal_matrix(g))
        rhs = [sum(row[j]*v for row, v in zip(g, residual)) for j in range(4)]
        step = [sum(row[j]*rhs[j] for j in range(4)) for row in p]
        state = [v+d for v, d in zip(state, step)]
        if max(map(abs, step)) < 1e-10:
            return state
    raise ValueError('constructed solver example did not converge')


# (identity, period multiplier, base-frame phase, offset milliseconds).
BEACONS = [('A1', 1, 0, 0), ('A2', 1, 0, 1), ('A3', 1, 0, 2),
           ('A4', 1, 0, 3), ('A5', 2, 0, 4), ('A6', 2, 1, 4),
           ('A7', 4, 0, 5), ('A8', 4, 2, 5)]


def events(beacons):
    hyper_frames = 1
    for _, m, _, _ in beacons:
        hyper_frames = math.lcm(hyper_frames, m)
    result = []
    for frame in range(hyper_frames):
        for name, m, phase, offset in beacons:
            require(m > 0 and 0 <= phase < m and 0 <= offset < F('62.5'), 'schedule fields')
            if frame % m == phase:
                result.append((frame*F('62.5')+offset, name, frame))
    return sorted(result), hyper_frames*F('62.5')


def feasible(beacons, airtime=F('.6'), guard=F('.1'), retune=F('.2')):
    schedule, hyper_ms = events(beacons)
    cyclic = schedule + [(schedule[0][0]+hyper_ms, schedule[0][1], schedule[0][2])]
    gaps = [b[0]-a[0] for a, b in zip(cyclic, cyclic[1:])]
    return all(gap >= airtime+2*guard+retune for gap in gaps), gaps, schedule, hyper_ms


def calculate():
    # All emitted numbers are model output, never hardware evidence.
    quant = []
    for mhz in (16, 80, 160, 240, 1000):
        quant.append(dict(capture_mhz=mhz, tick_ns=round(1000/mhz, 6),
                          one_tick_range_m=round(C/(mhz*1e6), 3),
                          two_stamp_range_std_m=round(C/(mhz*1e6*math.sqrt(6)), 3)))
    require([r['two_stamp_range_std_m'] for r in quant] == [7.649, 1.530, .765, .510, .122],
            'quantization example')
    anchors = [(x, y, z) for x in (0, 10) for y in (0, 8) for z in (0, 3)]
    centre = [5, 4, 1.5]
    g = geometry(centre, anchors)
    covariance = inverse(normal_matrix(g))
    h = math.sqrt(covariance[0][0]+covariance[1][1])
    p = math.sqrt(sum(covariance[i][i] for i in range(3)))
    require(round(h, 6) == .744406 and round(p, 6) == 1.719569, 'room dilution')
    require(rank(g) == 4, '3D+clock room geometry rank')
    ceiling_rank = rank(geometry(centre, [a for a in anchors if a[2] == 3]))
    require(ceiling_rank == 3, 'symmetric ceiling height-clock degeneracy')
    # Independent closed-form calculation checks the numerical inverse.
    r2 = 43.25
    require(math.isclose(h*h, r2/(8*25)+r2/(8*16), rel_tol=1e-12), 'horizontal inverse check')
    require(math.isclose(p*p, h*h+r2/(8*2.25), rel_tol=1e-12), '3D inverse check')
    averages = []
    for n in (1, 16, 256):
        averages.append(dict(epochs=n, collection_seconds=n/16,
            independent_horizontal_rms_m=round(h*C*30e-9/math.sqrt(n), 3),
            persistent_floor_horizontal_rms_m=round(h*C*1e-9*math.sqrt(900/n+25), 3)))
    require([r['independent_horizontal_rms_m'] for r in averages] == [6.695, 1.674, .418],
            'noise-only averaging')
    require([r['persistent_floor_horizontal_rms_m'] for r in averages] == [6.787, 2.012, 1.192],
            'persistent-floor averaging')
    require(round(h*C*5e-9, 3) == 1.116, 'infinite-window floor')
    # Joint recovery of receiver position and clock offset in constructed data.
    for pos, b in [([3, 2, 1], 37), ([7, 6, 2], -100), (centre, 500)]:
        z = pseudoranges(pos, b, anchors)
        found = solve_position(z, anchors)
        require(max(abs(a-b) for a, b in zip(found, list(pos)+[b])) < 1e-7, 'noiseless solution')
        shifted = solve_position([v+123 for v in z], anchors)
        require(math.dist(found[:3], shifted[:3]) < 1e-7 and
                abs(shifted[3]-found[3]-123) < 1e-7, 'common clock-offset gauge')
    biased = pseudoranges(centre, 37, anchors)
    biased[0] += C*5e-9  # Persistent error on one anchor; no residual-based rejection.
    first = solve_position(biased, anchors)
    averaged = [sum([v]*256)/256 for v in biased]
    later = solve_position(averaged, anchors)
    require(math.dist(first[:3], centre) > .1 and math.dist(first[:3], later[:3]) < 1e-8,
            'constant anchor bias does not average away')
    ensemble_ind = 30/math.sqrt(8)
    ensemble_corr = 30*math.sqrt(.5+.5/8)
    require(round(ensemble_ind, 3) == 10.607 and ensemble_corr == 22.5, 'ensemble correlation')
    # Perfect temporal correlation leaves the sample-mean variance unchanged.
    n = 16
    factor = (1+2*sum((1-k/n) for k in range(1, n)))/n
    require(math.isclose(factor, 1), 'correlated averaging floor')
    ok, gaps, plan, hyper = feasible(BEACONS)
    require(ok and hyper == 250 and len(plan) == 22, 'multi-rate schedule')
    counts = [sum(event[2] == f for event in plan) for f in range(4)]
    require(counts == [6, 5, 6, 5] and min(gaps) == 1, 'multi-rate frame occupancy')
    require(not feasible(BEACONS+[('conflict', 1, 0, 3)])[0], 'receiver conflict rejected')
    # Too-long airtime/retune must fail even if train frequencies differ.
    require(not feasible(BEACONS, retune=F('.21'))[0], 'missing retune margin rejected')
    require(not feasible(BEACONS, airtime=F('.61'))[0], 'missing airtime margin rejected')
    # Same offset across disjoint phases is legal; the wrap boundary is checked.
    require(feasible([('x', 2, 0, 0), ('y', 2, 1, 0)])[0], 'phase-disjoint reuse')
    require(not feasible([('x', 1, 0, F('.1')), ('y', 1, 0, F('62.4'))])[0],
            'cyclic wrap conflict rejected')
    require(round(C*1e-6*.007, 3) == 2.099 and 5*.007*1000 == 35, 'uniform burst dynamics')
    require(5*.250 == 1.25 and 5*16 == 80, 'slow observation age and motion')
    require(round(1/C*1e9, 3) == 3.336 and round(.1/C*1e12, 3) == 333.564, 'range budgets')
    return dict(schema_version=1, author='Jonathan Yantis', license='CC-BY-4.0',
        evidence_type='ANALYTICAL_MODEL_OUTPUT_NOT_HARDWARE_OR_LOCALIZATION_EVIDENCE',
        c_m_per_s=C,
        assumptions=['Perfect surveyed eight-corner 10x8x3 m room; receiver at centre.',
                     'Local first-order covariance; equal independent scalar measurement noise.',
                     '30 ns per-epoch zero-mean pseudorange noise is hypothetical, not measured PToB PPS RMS.',
                     '5 ns independent per-anchor offset is fixed over each averaging window.',
                     'No multipath, receiver motion, clock-frequency noise, missingness or nonlinear ambiguity in the covariance curve.',
                     'Aligned multi-rate calendar uses hypothetical .6 ms occupancy, .1 ms early/late guards and .2 ms transitions; not current controller acceptance.'],
        quantization=quant,
        room=dict(dimensions_m=[10,8,3], receiver_m=centre, anchors_m=anchors,
                  horizontal_dilution=round(h, 6), position_dilution=round(p, 6),
                  full_rank=4, symmetric_ceiling_only_rank=ceiling_rank,
                  persistent_floor_horizontal_rms_m=round(h*C*5e-9, 3)),
        averaging=averages,
        ensemble=dict(members=8, individual_std_ns=30, independent_mean_std_ns=round(ensemble_ind, 3),
                      correlation_half_mean_std_ns=ensemble_corr),
        schedule=dict(base_period_ms=62.5, hyperperiod_ms=int(hyper), events_per_hyperperiod=len(plan),
                      frame_counts=counts, beacon_plan=[dict(id=b,period_multiplier=m,frame_phase=r,
                      offset_ms=o,rate_hz=16/m) for b,m,r,o in BEACONS],
                      admission_required_separation_ms=1,
                      tested_rejections=['same-slot conflict','retune margin exceeded','airtime exceeded','cyclic wrap conflict']),
        constructed_solver_checks=dict(noiseless_recovery=True, common_offset_gauge=True,
                                       persistent_bias_survives_averaging=True),
        independent_clock_or_position_qualification=False)


def evidence():
    directory = ROOT / 'evidence/s3/2026-10-24-w12-utc-basis'
    for name, window, count, total, rms, peak in [
        ('w12-pps.csv', None, 522, 600, 60.328, 180),
        ('w12-pps.csv', '2', 300, 300, 38.312, 121),
        ('downstream-pps.csv', None, 451, 600, 61.922, 211),
        ('sampled-tx.csv', None, 58, 120, 108.306, 226),
    ]:
        with (directory/name).open(newline='') as stream:
            records = [r for r in csv.DictReader(stream) if window is None or r['window'] == window]
        values = [int(r['phase_ns']) for r in records if r['eligible'] == '1']
        require(len(records) == total and len(values) == count, 'evidence denominator')
        require(round(math.sqrt(sum(v*v for v in values)/count), 3) == rms and
                max(map(abs, values)) == peak, 'evidence summary')
    with (ROOT/'evidence/s3/2026-10-22-fine/pps-sanitized.csv').open(newline='') as stream:
        fine = list(csv.DictReader(stream))
    for cohort, count, total, rms, peak in [('fine50909_fixed660s',657,660,166.590,1591),
        ('n31_51016_transfer48',48,64,150.954,362)]:
        records = [r for r in fine if r['cohort'] == cohort]
        values = [int(r['phase_ns']) for r in records if r['valid'] == '1']
        require(len(records) == total and len(values) == count and
                round(math.sqrt(sum(v*v for v in values)/count), 3) == rms and
                max(map(abs, values)) == peak, 'distinct N30 population')


def identity():
    edition = json.loads(EDITION.read_text())
    entries = edition['files'] + edition['cited_local_sources']
    if PUBLICATION.is_file():
        publication = json.loads(PUBLICATION.read_text())
        require(publication['status'] == 'APPROVED_FOR_PUBLIC_DRAFT_REVIEW',
                'publication approval status')
        require(publication['prepared_edition'] == str(EDITION.relative_to(ROOT)) and
                publication['prepared_edition_sha256'] == hashlib.sha256(EDITION.read_bytes()).hexdigest(),
                'immutable preparation receipt identity')
        require(publication['unpublished_preparation_dependencies'] == edition['preserved_previous_draft'],
                'unpublished predecessor dependency record')
        published = {entry['path']: entry for entry in publication['publication_files']}
        require(set(published) == {entry['path'] for entry in edition['files']},
                'publication file allowlist')
        # Public approval preserves the reviewed manuscript, PDF and model bytes.
        # Only publication README text and this identity adapter may differ.
        ancillary = {'papers/indoor-ips/README.md', 'tools/verify_indoor_ips_paper.py'}
        for entry in edition['files']:
            if entry['path'] not in ancillary:
                require(published[entry['path']] == entry, 'reviewed artifact changed')
        entries = [published.get(entry['path'], entry) for entry in entries]
        # The earlier unapproved draft stays local. Its preparation seals remain
        # provenance metadata, not a dependency of public IPS reconstruction.
    else:
        entries += edition['preserved_previous_draft']
    for entry in entries:
        data = (ROOT/entry['path']).read_bytes()
        require(len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'],
                'identity mismatch: '+entry['path'])
    source = SOURCE.read_text()
    keys = re.findall(r'\\bibitem\{([^}]+)\}', source)
    cited = {key for group in re.findall(r'\\cite\{([^}]+)\}', source) for key in group.split(',')}
    require(len(keys) == len(set(keys)) == 14 and set(keys) == cited, 'bibliography consistency')
    require(not re.search(r'\\(?:input|include)\s*\{', source), 'source not standalone')
    require(not any(s in source for s in ('.internal/', '/Users/', 'ble-clock-v2', 'TODO', 'TBD', 'turn12search')),
            'private path or placeholder')
    for literal in ('0.744406','1.719569','6.695','1.674','0.418','6.787','2.012','1.192',
                    '1.116','10.607','22.5','333.564','3.336','2.099','1.25'):
        require(literal in source, 'analytical value missing from manuscript: '+literal)
    for name, m, phase, offset in BEACONS:
        require(f'{name}&{16//m}&{m}&{phase}&{offset}' in source,
                'multi-rate plan differs from manuscript table: '+name)
    require(edition['status'] == 'AUTHOR_REVIEW_THEORETICAL_DRAFT_PUBLICATION_APPROVAL_PENDING', 'draft status')
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-analysis', type=Path,
                        help='Write model output to this path instead of verifying the edition.')
    args = parser.parse_args()
    analysis = calculate()
    if args.write_analysis:
        args.write_analysis.parent.mkdir(parents=True, exist_ok=True)
        args.write_analysis.write_text(json.dumps(analysis, indent=2)+'\n')
        print('Wrote explicitly analytical model output:', args.write_analysis)
        return
    # JSON turns tuple coordinates into lists; normalize the fresh calculation.
    require(json.loads(ANALYSIS.read_text()) == json.loads(json.dumps(analysis)), 'analytical JSON mismatch')
    evidence()
    sealed = identity()
    print(json.dumps(dict(status='PASS_OFFLINE_IPS_ANALYSIS_AND_EDITION_IDENTITY',
        room=analysis['room'], schedule=analysis['schedule'],
        constructed_solver_checks=analysis['constructed_solver_checks'],
        evidence_populations_kept_separate=True, sealed_files=sealed,
        hardware_or_position_qualification=False), indent=2))


if __name__ == '__main__':
    main()
