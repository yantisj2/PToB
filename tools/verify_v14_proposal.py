#!/usr/bin/env python3
"""Verify the public V14 edition, archived manuscripts and coarse-wire fixtures.

Copyright (c) 2026 Jonathan Yantis. MIT.
Standard library only. No network, private inputs, devices or firmware execution.
"""
import hashlib
import hmac
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def verified_bytes(entry, revision=None):
    path = (ROOT/entry['path']).resolve()
    require(path.is_relative_to(ROOT), 'Path escapes repository')
    if revision is None:
        data = path.read_bytes()
    else:
        require(re.fullmatch(r'[0-9a-f]{40}', revision), 'Full source revision required')
        try:
            data = subprocess.check_output(['git', 'show', revision+':'+entry['path']],
                                           cwd=ROOT, stderr=subprocess.PIPE)
        except (FileNotFoundError, subprocess.CalledProcessError) as exc:
            raise ValueError('Archived source unavailable; include Git history '+revision) from exc
    require(len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'],
            'Edition identity: '+entry['path'])
    return data


def main():
    publication = json.loads((ROOT/'papers/editions/v14-rc-2026-11-06-publication.json').read_text())
    require(publication['status'] == 'APPROVED_FOR_PUBLIC_DRAFT_REVIEW', 'Review status')
    require(publication['review']['all_pages_visually_inspected'], 'Visual review incomplete')
    prepared = publication['author_review']
    receipt = verified_bytes(prepared['manifest'])
    require(receipt == verified_bytes(prepared['manifest'], prepared['revision']),
            'Archived preparation manifest')
    edition = json.loads(receipt)
    require(publication['page_count'] == edition['page_count'], 'Page budget')
    require(publication['title'] == edition['title'], 'Approved title')
    for entry in edition['files']:
        verified_bytes(entry, prepared['revision'])
    for entry in edition['cited_local_sources']:
        verified_bytes(entry, edition['base_repository_commit'])

    previous = edition['previous_edition']
    receipt = verified_bytes(previous['manifest'])
    require(receipt == verified_bytes(previous['manifest'], previous['revision']), 'Archived October manifest')
    october = json.loads(receipt)
    for entry in october['files']:
        verified_bytes(entry, previous['revision'])
    for entry in october['cited_local_sources']:
        verified_bytes(entry, october['base_repository_commit'])

    published = {entry['path']: entry for entry in publication['files']}
    source = 'papers/ptob-v14-sig-proposal.tex'
    pdf = 'papers/ptob-v14-sig-proposal.pdf'
    require(set(published) == {source, pdf}, 'Public manuscript paths')
    tex = verified_bytes(published[source]).decode()
    require(verified_bytes(published[pdf]).startswith(b'%PDF-'), 'PDF signature')
    require('Public review draft' in tex and 'Pending approval' not in tex, 'Public cover')
    cites = {key for group in re.findall(r'\\cite\{([^}]+)\}', tex) for key in group.split(',')}
    require(cites == set(re.findall(r'\\bibitem\{([^}]+)\}', tex)), 'Unresolved citation')
    require(not re.search(r'/Users/|/dev/cu\.|/Volumes/|\.internal/|(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',
                          tex, re.IGNORECASE), 'Private path or factory identifier')
    require(r'\input{' not in tex and r'\include{' not in tex, 'Standalone source required')

    fixtures = json.loads((ROOT/'protocol/time-zone-v1/vectors.json').read_text())
    key = bytes.fromhex(fixtures['key_hex'])
    context = struct.pack('<BQI', 1, int(fixtures['group_id'], 16), fixtures['key_epoch'])
    expected = {
        'gm-announce-cdt': (186, 4, 166, 162, 'PTOBTZ04'),
        'join-request-cdt': (168, 0, 152, 148, 'PTOBTZJ5'),
        'rc-capability-cdt': (148, 0, 132, 128, 'PTOBTZC8'),
        'rc-control-cdt': (216, 0, 200, 196, 'PTOBTZR8'),
    }
    require({v['name'] for v in fixtures['vectors']} == set(expected), 'Fixture population')
    for row in fixtures['vectors']:
        size, start, tag, tz, domain = expected[row['name']]
        wire = bytes.fromhex(row['hex'])
        body = wire[start:]
        require((len(wire), row['body_offset'], row['tag_offset'], row['tz_offset'], row['domain'])
                == (size, start, tag, tz, domain), row['name']+' layout')
        require(struct.unpack('<Hh', body[tz:tz+4]) == (2, -300), row['name']+' signed TZ')
        actual = hmac.new(key, domain.encode()+context+body[:tag], hashlib.sha256).digest()[:16]
        require(hmac.compare_digest(actual, body[tag:]), row['name']+' HMAC')
        if start:
            require(wire[:4] == bytes([size-1, 255, 255, 255]), 'GM AD wrapper')
            require(body[:6] == b'G7V4\x04\x01' and
                    int.from_bytes(body[6:8], 'big') == 182, 'GM big-endian header')
        elif row['name'].startswith('rc-'):
            require(wire[6] == 8 and wire[8] == 5, 'RC coarse revision/profile')
        else:
            require(wire[0] == 14 and wire[2] == 5, 'JOIN discriminator/revision')
    print('PASS: public V14 source and PDF, archived editions and source pins, '
          'citations, and four independent coarse-wire HMAC/TZ known answers.')


if __name__ == '__main__':
    main()
