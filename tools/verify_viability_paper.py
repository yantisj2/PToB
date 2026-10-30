#!/usr/bin/env python3
"""Verify the viability edition, public anchors and claim/summary correspondence.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Use --replay to rerun supporting public numerical analyses (several minutes).
No hardware, network, private checkout, credentials or daemon is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EDITION = ROOT / 'papers/editions/viability-2026-10-30.json'


def require(condition, description):
    if not condition:
        raise SystemExit('FAIL: ' + description)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def load(path):
    return json.loads((ROOT / path).read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', action='store_true')
    args = parser.parse_args()
    edition = json.loads(EDITION.read_text())
    require(edition['status'] == 'AUTHOR_REVIEW_DRAFT_NOT_SUBMITTED', 'author-review status')
    require(edition['author'] == 'Jonathan Yantis' and edition['paper_license'] == 'CC-BY-4.0',
            'author and paper license')
    for item in edition['files'] + edition['inputs']:
        path = (ROOT / item['path']).resolve()
        require(path.is_relative_to(ROOT), 'repository-contained input')
        raw = path.read_bytes()
        require(len(raw) == item['bytes'] and digest(raw) == item['sha256'], item['path'])
    previous = edition['previous_edition']
    for item in previous['files']:
        raw = subprocess.check_output(['git', 'show', previous['revision'] + ':' + item['path']], cwd=ROOT)
        require(digest(raw) == item['sha256'], 'preserved previous edition: ' + item['path'])
    source = (ROOT / 'papers/ptob-viability-preprint.tex').read_text()
    audit = load(edition['verification']['final_source_evidence_link_audit'])
    require(audit['source_sha256'] == digest((ROOT / 'papers/ptob-viability-preprint.tex').read_bytes())
            and audit['pdf_sha256'] == digest((ROOT / 'papers/ptob-viability-preprint.pdf').read_bytes()),
            'audit binds the final source/PDF')
    require(not audit['unresolved_broken_links'] and not audit['internal_pdf_destinations']['missing']
            and all(item['matches_cited_revision'] for item in audit['public_source_bytes']),
            'completed source and link audit')
    require(edition['verification']['all_pages_visually_inspected'] and
            audit['all_pages_visually_inspected'], 'completed visual review')
    require('30 October 2026' in source, 'draft date')
    require(r'\documentclass[11pt,letterpaper]{article}' in source and
            r'\usepackage[margin=0.85in]{geometry}' in source, 'baseline type size and margins')
    require(not re.search(r'\\(?:input|include|includegraphics|bibliography)\s*\{', source),
            'self-contained source')
    require(not any(x in source for x in ('/Users/', '.internal/', '../ble-clock-v2', 'TODO', 'TBD')),
            'no private paths or placeholders')
    cited = {key for group in re.findall(r'\\cite\{([^}]+)\}', source) for key in group.split(',')}
    defined = re.findall(r'\\bibitem\{([^}]+)\}', source)
    require(len(defined) == len(set(defined)) and cited == set(defined), 'all references defined and used')
    require(r'\label{app:reproduce}' in source, 'reproducibility appendix')
    anchors = re.findall(r'\\repo\{([^}]+)\}', source)
    revision = edition['base_repository_commit']
    require('blob/' + revision + '/#1' in source, 'pinned public citation revision')
    for path in anchors:
        subprocess.run(['git', 'cat-file', '-e', revision + ':' + path], cwd=ROOT, check=True)
    for path in re.findall(r'\\path\{([^}]+)\}', source):
        require((ROOT / path).exists(), 'public appendix path: ' + path)

    w12 = load('evidence/s3/2026-10-24-w12-utc-basis/results.json')
    for key in ('w12', 'downstream', 'sampled_tx'):
        require(f"{w12[key]['phase_ns']['rms']:.3f}" in source, key + ' RMS claim')
        require(str(w12[key]['phase_ns']['max_abs']) in source, key + ' maximum claim')
    require(w12['timing_gate'] == 'NOT_MET' and w12['offsets_refit'] is False,
            'failed timing gate and frozen calibration retained')
    s3 = load('evidence/s3/2026-10-22-fine/results.json')
    for key in ('fine50909_fixed660s', 'n31_51016_transfer48'):
        require(f"{s3['cohorts'][key]['rms_ns']:.3f}" in source, key + ' RMS claim')
    usb = load('clients/macos/results/2026-10-17-ble-vs-direct.json')
    for key in ('RC', 'DC'):
        result = usb['results'][key]
        for value in (result['RX']['rmse_ns'], result['delivered_minus_fine']['rms_ns']):
            require(f'{value:.3f}' in source, key + ' USB metric')
    require(all(phrase in source for phrase in (
        '64 s on 128 prefixes', '75 s, P99 983 ns', '76 s, P99 1028 ns',
        'durations are 24 s', 'Unknown expiry counts against passing',
        'temperature steering remains disabled', 'Four excursions', '1591',
        'unobserved errors remain unknown')), 'holdover/tail/unknown scope retained')
    length = edition['length']
    require(length['before_pages'] == 15 and length['maximum_additional_pages'] == 2 and
            length['after_pages'] <= 17, 'two-page expansion limit')
    if shutil.which('pdfinfo'):
        info = subprocess.check_output(['pdfinfo', str(ROOT / 'papers/ptob-viability-preprint.pdf')], text=True)
        pages = int(re.search(r'^Pages:\s+(\d+)', info, re.M)[1])
        require(pages == length['after_pages'], 'actual PDF page count')
    else:
        print('NOTE: pdfinfo unavailable; recorded page count is bound by the sealed PDF hash.')
    print(f"PASS: source/PDF/input identities; {len(anchors)} pinned public anchors; "
          f"{len(defined)} references; claim summaries; 15 -> {length['after_pages']} pages")
    if args.replay:
        for command in edition['verification']['replay_commands']:
            subprocess.run([sys.executable, str(ROOT / command)], cwd=ROOT, check=True)
        print('PASS: all supporting offline numerical verifiers')
    print('Not established: raw physical extraction, calibration transfer, independent UTC/output, '
          'continuous availability, measured sleep energy or arXiv acceptance.')


if __name__ == '__main__':
    main()
