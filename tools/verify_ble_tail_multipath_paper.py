#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Yantis.
"""Check the BLE tail/multipath draft's arithmetic, evidence and edition offline.

Python 3.9+ standard library and the recorded Git checkout. No hardware, daemon, network, calibration
fit or firmware modification. --write-analysis regenerates the explicitly
analytical CC BY 4.0 supplement; it never updates the sealed edition receipt.
"""
import argparse
import csv
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

import verify_s3_fine_paper as fine
import verify_w12_utc_results as w12

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'papers/ble-tail-multipath'
EDITION = ROOT / 'papers/editions/ble-tail-multipath-2026-10-26.json'
PUBLICATION = ROOT / 'papers/editions/ble-tail-multipath-2026-10-26-publication.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rows(path):
    with (ROOT / path).open(newline='') as stream:
        return list(csv.DictReader(stream))


def git_source(row):
    revision, path = row['revision'], row['path']
    require(re.fullmatch(r'[0-9a-f]{40}', revision), 'full Git revision required')
    require(ROOT in (ROOT / path).resolve().parents, 'Git source path escapes repository')
    if PUBLICATION.is_file():
        publication = json.loads(PUBLICATION.read_text())
        matches = [entry for entry in publication['reviewed_source_snapshots']
                   if entry['path'] == path and entry['revision'] == revision]
        require(len(matches) == 1, 'unique reviewed source snapshot required: '+path)
        snapshot = (ROOT / matches[0]['snapshot_path']).resolve()
        require((PROJECT / 'reviewed-sources').resolve() in snapshot.parents,
                'reviewed source snapshot escapes its collection')
        data = snapshot.read_bytes()
        require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                'reviewed source snapshot identity mismatch: '+path)
        return data
    return subprocess.check_output(['git', 'show', '--no-ext-diff', '--no-textconv',
                                    revision+':'+path], cwd=ROOT)


def reviewed_source(path):
    if EDITION.is_file():
        receipt = json.loads(EDITION.read_text())
        for row in receipt.get('cited_git_sources', []):
            if row['path'] == path:
                return git_source(row).decode()
    return (ROOT / path).read_text()


def slope(points, within=False):
    groups = {}
    for x, y, channel in points:
        groups.setdefault(channel if within else 'all', []).append((F(x), F(y)))
    xy = xx = F(0)
    for group in groups.values():
        mx = sum(x for x, _ in group) / len(group)
        my = sum(y for _, y in group) / len(group)
        xy += sum((x-mx)*(y-my) for x, y in group)
        xx += sum((x-mx)**2 for x, _ in group)
    return xy / xx if xx else None


def analytical():
    c = F(299792458)
    codec = reviewed_source('firmware/t096/protocol/precision_codec.h')
    units = int(re.search(r'R2_PRECISION_INTERVAL_UNITS (\d+)u', codec)[1])
    interval_s = F(units * 1250, 1_000_000)
    require(interval_s == F(1, 16), 'current RC7 interval differs from manuscript')
    points = [(0, 0, 'a'), (1, 20, 'a'), (2, 140, 'b'), (3, 160, 'b')]
    require(slope(points) == 60 and slope(points, True) == 20, 'channel confounding')
    # Arbitrary fixed channel offsets cannot change the within-channel slope.
    shifted = [(x, y + (357 if k == 'a' else -901), k) for x, y, k in points]
    require(slope(shifted, True) == 20, 'channel offset invariance')
    require(slope([(0, 0, 'a'), (1, 20, 'b')], True) is None,
            'unidentifiable rate must not be returned as zero')
    # Time-varying per-channel delay remains confounded with rate.
    require(slope([(x, y + 7*x, k) for x, y, k in points], True) == 27,
            'time-varying channel bias is not removed')
    envelope = lambda age: F(100) + 20*age + age*age
    require([envelope(F(v)) for v in (1, 5, 10)] == [121, 225, 400], 'age envelope')
    require(F(100) + F(500)*5 == 2600, '0.5 ppm illustration')
    # Ten failed event opportunities followed by success; distinguish outage
    # duration from last-success age just before the next event.
    require(10*interval_s == F(5, 8) and 11*interval_s == F(11, 16), 'loss age')
    p = F(1, 10)
    cdf = lambda k: 1-p**(k+1)
    require(cdf(0) < F(99, 100) <= cdf(1), 'ideal post-event age P99')
    alpha, threshold = .05, .0001
    n = math.ceil(math.log(alpha)/math.log1p(-threshold))
    require(n == 29956 and (1-threshold)**n <= alpha < (1-threshold)**(n-1),
            'exact zero-exceedance trial count')
    require(round(n / 16 / 60, 3) == 31.204, 'independent sample duration')
    require(round(-math.expm1(math.log(alpha)/600)*100, 3) == .498, '600-trial upper limit')
    require(round(float(1/c*10**9), 3) == 3.336, 'one metre propagation')
    require(round(float(30/c*10**9), 3) == 100.069, '30 metre propagation')
    require(round(float(c/F(2440000000)*100), 3) == 12.287, 'wavelength')
    require(round(float(c/F(2440000000)*50), 3) == 6.143, 'half wavelength')
    require(F(10**9, 80_000_000) == F('12.5') and F(10**9, 20_000_000) == 50,
            'counter resolution')
    require(F(8+32, 1_000_000)*10**6 == 40, 'nominal LE1M reference conversion')
    # A four-timestamp estimator retains directional asymmetry. Use exact
    # clock coordinates, independent of any proposed PToB implementation.
    offset, forward, reverse = F(37), F(120), F(80)
    a, b = forward+offset, reverse-offset
    estimate = (a-b)/2
    require(estimate-offset == (forward-reverse)/2 == 20, 'two-way asymmetry')
    # Repeated one-way observations alone cannot distinguish fixed offset
    # from an admissible fixed path/detector delay. The manuscript invokes
    # the affine-clock literature, not a stronger PToB impossibility claim.
    for delta in (F(-10), F(0), F(20)):
        require(forward-delta >= 0 and
                offset+forward == (offset+delta)+(forward-delta), 'offset/delay ambiguity')
    source = (PROJECT / 'main.tex').read_text()
    for literal in ['60.328', '61.922', '108.306', '166.590', '1591', '3.336',
                    '100.069', '12.287', '6.143', '687.5', '62.5', '31.204',
                    '29\\,956', '0.498', '2600']:
        require(literal in source, 'computed claim missing from manuscript: '+literal)
    return dict(kind='analytical_examples_not_hardware_evidence', interval_ms=62.5,
                frequency_hz=16, pooled_rate_ns_s=60, within_channel_rate_ns_s=20,
                hypothetical_envelope_ns={str(t): int(envelope(F(t))) for t in (1, 5, 10)},
                zero_exceedance_independent_trials=n, independent_duration_min=round(n/16/60, 6),
                upper_95_percent_at_600=round(-math.expm1(math.log(alpha)/600)*100, 6),
                hypothetical_age_curve=[dict(age_s=t, envelope_ns=int(envelope(F(t))),
                                             alternate_ns=100+500*t) for t in range(11)])


def evidence():
    result = []
    base = 'evidence/s3/2026-10-24-w12-utc-basis/'
    for name, total, eligible, rms, maximum in [
            ('w12-pps.csv', 600, 522, 60.328, 180),
            ('downstream-pps.csv', 600, 451, 61.922, 211),
            ('sampled-tx.csv', 120, 58, 108.306, 226)]:
        selected = rows(base+name)
        score = w12.summary(selected)
        require((score['logged'], score['eligible']) == (total, eligible), name+' denominator')
        require(round(score['phase_ns']['rms'], 3) == rms and
                score['phase_ns']['max_abs'] == maximum, name+' residual statistics')
        result.append(dict(path=base+name, total=total, eligible=eligible, rms_ns=rms,
                           max_abs_ns=maximum, reasons=score['reasons']))
    allfine = rows('evidence/s3/2026-10-22-fine/pps-sanitized.csv')
    for cohort, total, eligible, rms, maximum in [
            ('fine50909_fixed660s', 660, 657, 166.590, 1591),
            ('n31_51016_transfer48', 64, 48, 150.954, 362)]:
        selected = [r for r in allfine if r['cohort'] == cohort]
        values = [int(r['phase_ns']) for r in selected if r['valid'] == '1']
        score = fine.metrics(values)
        require(len(selected) == total and len(values) == eligible, cohort+' denominator')
        require(round(score['rms_ns'], 3) == rms and score['max_abs_ns'] == maximum,
                cohort+' residual statistics')
        above_us = sum(abs(v) > 1000 for v in values)
        if cohort == 'fine50909_fixed660s':
            require(above_us == 4, 'all four N30 tails retained')
            require([int(r['ordinal']) for r in selected] == list(range(1, 661)), 'N30 order')
        result.append(dict(cohort=cohort, total=total, eligible=eligible, rms_ns=rms,
                           max_abs_ns=maximum, above_1us=above_us))
    return result


def identity():
    receipt = json.loads(EDITION.read_text())
    require(receipt['status'] == 'AUTHOR_REVIEW_DRAFT_PUBLICATION_APPROVAL_PENDING', 'draft status')
    require(receipt['page_count'] == 9 and receipt['paper_license'] == 'CC-BY-4.0', 'paper identity')
    published = {}
    if PUBLICATION.is_file():
        publication = json.loads(PUBLICATION.read_text())
        require(publication['status'] == 'APPROVED_FOR_PUBLIC_DRAFT_REVIEW' and
                publication['prepared_edition'] == str(EDITION.relative_to(ROOT)) and
                publication['prepared_edition_sha256'] == hashlib.sha256(EDITION.read_bytes()).hexdigest(),
                'immutable preparation receipt identity or approval status')
        published = {row['path']: row for row in publication['publication_files']}
        require(set(published) == {row['path'] for row in receipt['files']},
                'publication file allowlist')
        ancillary = {'papers/ble-tail-multipath/README.md', 'tools/verify_ble_tail_multipath_paper.py'}
        for row in receipt['files']:
            if row['path'] not in ancillary:
                require(published[row['path']] == row, 'reviewed artifact changed')
        origins = [{key: row[key] for key in ('path', 'revision', 'bytes', 'sha256')}
                   for row in publication['reviewed_source_snapshots']]
        require(origins == receipt['cited_git_sources'], 'reviewed source selection changed')
    for group in ('files', 'cited_local_sources'):
        for row in receipt[group]:
            row = published.get(row['path'], row) if group == 'files' else row
            path = (ROOT / row['path']).resolve()
            require(ROOT in path.parents, 'receipt path escapes repository')
            data = path.read_bytes()
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'identity mismatch: '+row['path'])
    for row in receipt.get('cited_git_sources', []):
        data = git_source(row)
        require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                'Git source identity mismatch: '+row['path'])
    pdf = (PROJECT / 'main.pdf').read_bytes()
    require(pdf.startswith(b'%PDF-') and b'%%EOF' in pdf[-2048:], 'complete PDF required')
    source = (PROJECT / 'main.tex').read_text()
    require(len(re.findall(r'\\bibitem\{', source)) == 16, 'bibliography count')
    require(source.count('\\newpage') == 8, 'intended page boundaries')
    for marker in ('TODO', 'TBD', 'PLACEHOLDER', '.internal/'):
        require(marker not in source, 'unfinished/private marker: '+marker)
    # If installed, check the real PDF page tree, not source page breaks.
    try:
        from pypdf import PdfReader
    except ImportError:
        pass
    else:
        require(len(PdfReader(PROJECT / 'main.pdf').pages) == 9, 'PDF page tree count')
    return (len(receipt['files']),
            len(receipt['cited_local_sources']) + len(receipt.get('cited_git_sources', [])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write-analysis', action='store_true')
    args = parser.parse_args()
    supplement = dict(license='CC-BY-4.0', copyright='2026 Jonathan Yantis',
                      analytical=analytical(), separately_derived_historical_evidence=evidence(),
                      new_hardware_experiment=False, multipath_cause_established=False)
    analysis = PROJECT / 'analysis.json'
    if args.write_analysis:
        analysis.write_text(json.dumps(supplement, indent=2)+'\n')
        print('Wrote analytical supplement; edition receipt must be separately reviewed/sealed.')
        return
    require(json.loads(analysis.read_text()) == supplement, 'analysis supplement differs')
    files, sources = identity()
    print(f'PASS: analytical examples, channel confounding, loss/age and confidence assumptions; '
          f'5 separate evidence populations; {files} edition files and {sources} local/Git sources. '
          'No hardware action or accuracy qualification.')


if __name__ == '__main__':
    main()
