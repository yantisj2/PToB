#!/usr/bin/env python3
"""Decode saved nonce-bound Console V3 PPS responses. Never opens USB.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import framed_console as console


def decode(entries, manifest, session):
    console.base.require(bool(console.base.HEX16.fullmatch(session)) and int(session, 16), 'Exact boot required')
    records, nonces = [], set()
    previous_cursor = 0
    overwritten = 0
    empty = 0
    manifest = dict(manifest, watch_frame='PPS')
    for entry in entries:
        nonce = entry['request_nonce']
        console.base.require(nonce not in nonces, 'Repeated request nonce')
        nonces.add(nonce)
        parser = console.Parser(nonce, manifest, session)
        frames = parser.feed(bytes.fromhex(entry['response_hex']))
        parser.complete()
        console.base.require(len(frames) == 1, 'One complete nonce-bound response required')
        frame = console.base.require_coverage(frames[0])
        command = frame['rows']['W12_V12_COMMAND']
        console.base.require(command.get('accepted') == '1' and command.get('action') == 'PPS_READ', 'PPS read not accepted')
        row = frame['rows']['TWATCH_DUAL_PPS']
        console.base.require(row.get('available') in ('0', '1'), 'PPS availability missing')
        overwritten = max(overwritten, int(row['overwritten']))
        if row['available'] == '0':
            empty += 1
            continue
        cursor = int(row['cursor'])
        requested = entry['after_cursor']
        console.base.require(type(requested) is int and requested >= 0 and cursor > requested and cursor > previous_cursor, 'PPS cursor replay/order mismatch')
        console.base.require(not previous_cursor or cursor == previous_cursor + 1, 'PPS ring coverage gap; retain the raw failed capture')
        previous_cursor = cursor
        record = json.loads(row['json'])
        console.base.require(record.get('contract') == 'TWATCH_DUAL_RADIO_PPS_V1', 'Embedded PPS contract mismatch')
        start = entry['stage_start_device_us']
        console.base.require(type(start) is int and start > 0, 'Device-domain stage origin required')
        elapsed = record['pps']['seen_us'] - start
        console.base.require(elapsed >= 0, 'Drain earlier-stage records before starting a fixed window')
        record.update(run=entry['run'], stage=entry['stage'], stage_elapsed_us=elapsed,
                      boot=session, firmware=manifest['build_id'] + ':' + manifest['source_commit'])
        records.append(record)
    console.base.require(bool(records), 'No physical PPS records decoded')
    return records, dict(status='DECODED_CRC_BOUND_PPS', hardware_access=False,
                         responses=len(entries), samples=len(records), empty_polls=empty,
                         ring_overwritten=overwritten, boot=session)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--boot', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[3]
    out = a.out.resolve()
    if out.exists() or repo / 'artifacts' not in out.parents or subprocess.run(
            ['git', '-C', str(repo), 'check-ignore', '--quiet', str(out)]).returncode:
        p.error('Fresh ignored repository artifacts output required')
    raw = a.input.read_bytes()
    rows, receipt = decode([json.loads(line) for line in raw.decode().splitlines() if line.strip()],
                           json.loads(a.manifest.read_text()), a.boot)
    receipt.update(input_sha256=hashlib.sha256(raw).hexdigest(),
                   manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),
                   tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    out.mkdir(parents=True)
    (out / 'pps.jsonl').write_text(''.join(json.dumps(row, separators=(',', ':')) + '\n' for row in rows))
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('Decoded', len(rows), 'physical PPS records; no hardware access')


if __name__ == '__main__':
    main()
