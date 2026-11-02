#!/usr/bin/env python3
"""Bounded NTP wire probe for the enrolled Pi; network RTT is not UTC accuracy.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import time


def stamp(data):
    seconds, fraction = struct.unpack('!II', data)
    return seconds - 2208988800 + fraction / 2**32


def probe(host):
    rows = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        sock.connect((host, 123))
        for _ in range(4):
            token = secrets.token_bytes(8)
            packet = bytearray(48)
            packet[0] = 0x23  # NTPv4 client.
            packet[40:48] = token
            sent = time.time()
            try:
                sock.send(packet)
                reply = sock.recv(4096)
                received = time.time()
                if len(reply) < 48 or reply[24:32] != token or reply[0] & 7 != 4:
                    raise ValueError('bad NTP reply/origin association')
                rows.append({'valid_response': reply[0] >> 6 != 3 and 0 < reply[1] < 16,
                             'stratum': reply[1], 'leap': reply[0] >> 6,
                             'reference_id_hex': reply[12:16].hex(), 'client_round_trip_s': received - sent,
                             'server_receive_utc': stamp(reply[32:40]),
                             'server_transmit_utc': stamp(reply[40:48]),
                             'server_transmit_minus_client_receive_s': stamp(reply[40:48]) - received})
            except (OSError, ValueError) as error:
                rows.append({'valid_response': False, 'error': str(error)})
    return {'schema': 1, 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'samples': rows, 'independent_utc_accuracy_measured': False,
            'ptob_phase_proved_by_ntp_alone': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--connection', type=Path, default=Path.home() / '.config/ptob/pi5.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.out.resolve()
    if not out.is_relative_to(root / 'artifacts') or out.exists():
        raise SystemExit('use a new ignored artifacts/ report')
    os.umask(0o077)
    host = json.loads(args.connection.read_text())['host'].split('@')[1]
    report = probe(host)
    with out.open('x') as f:
        json.dump(report, f, indent=2)
        f.write('\n')
    passed = sum(row['valid_response'] for row in report['samples'])
    print(json.dumps({'valid_replies': passed, 'attempts': len(report['samples']), 'report': str(out)}))
    raise SystemExit(0 if passed == len(report['samples']) else 2)
