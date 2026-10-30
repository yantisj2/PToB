#!/usr/bin/env python3
"""Verify the current V14 proposal edition and public coarse-wire known answers.

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


def main():
    edition = json.loads((ROOT/'papers/editions/v14-sig-2026-10-30.json').read_text())
    for entry in edition['files'] + edition['cited_local_sources']:
        path = (ROOT/entry['path']).resolve()
        require(path.is_relative_to(ROOT), 'Path escapes repository')
        data = path.read_bytes()
        # A later documentation/code edit does not revise this reviewed snapshot.
        # Current manuscript/PDF outputs must always match in place.
        if entry in edition['cited_local_sources'] and hashlib.sha256(data).hexdigest() != entry['sha256']:
            revision = edition['base_repository_commit']
            require(re.fullmatch(r'[0-9a-f]{40}', revision), 'Full source revision required')
            try:
                data = subprocess.check_output(['git', 'show', revision+':'+entry['path']],
                                               cwd=ROOT, stderr=subprocess.PIPE)
            except (FileNotFoundError, subprocess.CalledProcessError) as exc:
                raise ValueError('Cited snapshot unavailable; include Git history '+revision) from exc
        require(len(data) == entry['bytes'] and
                hashlib.sha256(data).hexdigest() == entry['sha256'],
                'Edition identity: '+entry['path'])
    require(edition['status'] == 'AUTHOR_REVIEW_DRAFT_NOT_PUBLISHED', 'Review status')
    require(edition['page_count'] <= edition['previous_edition']['page_count'], 'Page budget')
    require(edition['review']['all_pages_visually_inspected'], 'Visual review incomplete')
    tex = (ROOT/'papers/ptob-v14-sig-proposal.tex').read_text()
    cites = {key for group in re.findall(r'\\cite\{([^}]+)\}', tex) for key in group.split(',')}
    require(cites <= set(re.findall(r'\\bibitem\{([^}]+)\}', tex)), 'Unresolved citation')
    require(not re.search(r'\b(previous|historical|superseded|legacy|RC1|RC5|GC17)\b', tex,
                          re.IGNORECASE), 'Design history belongs outside the proposal')
    require(not re.search(r'/Users/|/dev/cu\.|/Volumes/|(?:[0-9a-f]{2}:){5}[0-9a-f]{2}',
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
    print('PASS: current V14 source/PDF/source pins; four independent coarse-wire HMAC/TZ known answers; '
          'citations, standalone source, current-design scope and named privacy checks. '
          'No hardware, failover or accuracy qualification.')


if __name__ == '__main__':
    main()
