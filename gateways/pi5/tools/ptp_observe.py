#!/usr/bin/env python3
"""Observe PTPv2 domain-24 packets from the enrolled Pi over local Wi-Fi.

Copyright (c) 2026 Jonathan Yantis. MIT. Never adjusts a clock.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import select
import secrets
import socket
import struct
import time


def observe(host, duration=12):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((host, 123))
        local = route.getsockname()[0]
    sockets = []
    rows = []
    identity = secrets.token_bytes(8) + b'\x00\x01'
    def request(kind, length, sequence):
        data = bytearray(length)
        data[0], data[1], data[4], data[6] = kind, 2, 24, 4
        struct.pack_into('!H', data, 2, length)
        data[20:30] = identity
        struct.pack_into('!H', data, 30, sequence)
        data[32], data[33] = (1 if kind == 1 else 4), 127
        return data
    try:
        for port in (319, 320):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sockets.append(sock)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(('', port))
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                            socket.inet_aton('224.0.1.129') + socket.inet_aton(local))
        until = time.monotonic() + duration
        next_request = 0
        sequence = 0
        while time.monotonic() < until:
            if time.monotonic() >= next_request:
                sequence += 1
                # Client requests also establish both UDP flows through a firewall.
                sockets[0].sendto(request(1, 44, sequence), (host, 319))
                management = request(13, 54, sequence)
                management[34:44] = b'\xff' * 10
                struct.pack_into('!HHH', management, 48, 1, 2, 0x2001)
                sockets[1].sendto(management, (host, 320))
                next_request = time.monotonic() + 2
            ready, _, _ = select.select(sockets, [], [], min(1, until - time.monotonic()))
            for sock in ready:
                data, peer = sock.recvfrom(4096)
                if peer[0] != host or len(data) < 34 or data[1] & 15 != 2 or data[4] != 24:
                    continue
                size = struct.unpack_from('!H', data, 2)[0]
                if size > len(data) or size < 34:
                    continue
                row = {'message_type': data[0] & 15, 'sequence': struct.unpack_from('!H', data, 30)[0],
                       'source_port_identity': data[20:30].hex(), 'domain': data[4],
                       'received_unix': time.time(), 'bytes': size}
                if row['message_type'] in (0, 8) and len(data) >= 44:
                    row['origin_seconds'] = int.from_bytes(data[34:40], 'big')
                    row['origin_ns'] = int.from_bytes(data[40:44], 'big')
                rows.append(row)
    finally:
        for sock in sockets:
            sock.close()
    pairs = {(r['source_port_identity'], r['sequence']) for r in rows if r['message_type'] == 0} & {
        (r['source_port_identity'], r['sequence']) for r in rows if r['message_type'] == 8}
    return {'schema': 1, 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'duration_s': duration,
            'packets': rows, 'sync_follow_up_pairs': len(pairs),
            'announce_packets': sum(r['message_type'] == 11 for r in rows),
            'timestamping': 'software on Wi-Fi', 'clock_adjusted': False,
            'independent_output_accuracy_measured': False}


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
    report = observe(host)
    with out.open('x') as f:
        json.dump(report, f, indent=2)
        f.write('\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'packets'}))
    raise SystemExit(0 if report['sync_follow_up_pairs'] and report['announce_packets'] else 2)
