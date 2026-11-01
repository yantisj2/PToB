#!/usr/bin/env python3
"""Verify the approved Zephyr paper package and its public source identities.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
This verifies identity and selected document claims, not timing accuracy.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAPER = ROOT / 'papers/zephyr-timing'


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def matches(data, item):
    return (len(data) == item['bytes'] and
            hashlib.sha256(data).hexdigest() == item['sha256'])


def check_files(items):
    for item in items:
        path = ROOT / item['path']
        if not matches(path.read_bytes(), item):
            raise ValueError('Identity mismatch: ' + item['path'])


def verify():
    edition = read_json(PAPER / 'edition.json')
    approval = read_json(PAPER / 'publication.json')
    prepared = read_json(PAPER / 'preparation.json')
    check_files(edition['files'])
    check_files(approval['publication_files'])
    assert approval['status'] == 'APPROVED_FOR_PUBLIC_DRAFT_REVIEW'
    assert approval['reviewed_material_unchanged']
    assert not approval['draft_promoted_to_final']
    assert not approval['new_hardware_measurements']
    assert approval['prepared_edition_sha256'] == hashlib.sha256(
        (PAPER / 'preparation.json').read_bytes()).hexdigest()
    assert approval['edition_sha256'] == hashlib.sha256(
        (PAPER / 'edition.json').read_bytes()).hexdigest()
    assert prepared['review_status'] == 'DRAFT_FOR_USER_REVIEW_LIBRARY_INCLUSION_PENDING'
    assert edition['page_count'] == prepared['page_count'] == 7
    assert edition['bibliography_entries'] == prepared['bibliography_entries'] == 21
    old = {item['path']: item for item in prepared['files']}
    for public, original in edition['selected_review_files'].items():
        if original != 'edition.json':
            assert matches((ROOT / public).read_bytes(), old[original]), public

    source = (PAPER / 'main.tex').read_text(encoding='utf-8')
    bibliography = re.findall(r'\\bibitem\{([^}]+)\}', source)
    citations = {key.strip() for group in re.findall(r'\\cite\{([^}]+)\}', source)
                 for key in group.split(',')}
    assert len(bibliography) == len(set(bibliography)) == 21
    assert citations == set(bibliography)
    ledger = read_json(PAPER / 'source-ledger.json')
    assert ledger['date'] == '2026-11-01' and len(ledger['sources']) == 23
    for path in ('main.tex', 'source-identity.json', 'source-ledger.json'):
        text = (PAPER / path).read_text(encoding='utf-8')
        for forbidden in ('/Users/', '/home/', '.internal/', 'PRIVATE KEY'):
            assert forbidden not in text, (path, forbidden)

    identities = read_json(PAPER / 'source-identity.json')
    assert identities['git_revision'] == edition['implementation_revision']
    assert not identities['hardware_operated']
    assert len(identities['source_files']) == 25
    historical = 0
    for path, item in identities['source_files'].items():
        current = ROOT / path
        if current.is_file() and matches(current.read_bytes(), item):
            continue
        result = subprocess.run(
            ['git', 'show', item['git_revision'] + ':' + path],
            cwd=ROOT, capture_output=True, check=False)
        if result.returncode or not matches(result.stdout, item):
            raise ValueError('Inspected source unavailable or changed: ' + path +
                             '; include the recorded public Git history')
        historical += 1

    population = prepared['historical_population']
    assert population['nominal'] == 602
    assert (population['usable_reference_valid'], population['invalid_reference'],
            population['expired_reference_valid'], population['unobserved']) == (436, 65, 3, 98)
    assert 436 + 65 + 3 + 98 == 602
    assert not population['current_firmware_qualification']
    assert Fraction(1_000_000_000, 16_000_000) == Fraction(125, 2)
    assert Fraction(2**32, 16_000_000) == Fraction(4194304, 15625)
    print('PASS: approved seven-page package, unchanged reviewed bytes, 21 citations,')
    print('23 primary-source records and 25 inspected source identities (' +
          str(historical) + ' resolved from public Git history).')
    print('LIMIT: identity and document checks; no new experiment or timing qualification.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', action='store_true',
                        help='also replay the public historical V14 archive')
    args = parser.parse_args()
    verify()
    if args.replay:
        subprocess.run([sys.executable, str(ROOT / 'tools/verify_v14.py')],
                       cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
