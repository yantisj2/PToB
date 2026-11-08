#!/usr/bin/env python3
"""Verify the portable JOIN5/GR3 receiver and render W12 states without hardware."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import shlex
import subprocess

from check_01 import ROOT, png

LESSON = ROOT / '02_minimal_receiver'
RX = LESSON / 'components/ptob_rx'
CLOCK = ROOT / 'components/ref_clock'
UI = ROOT / 'components/ref_ui'
PUBLISHED = {'bootstrap', 'search', 'acquire', 'clock', 'info', 'lost', 'off'}


def fixture(path):
    """Emit independent HMAC-SHA256 records for the public wire formats."""
    key, mesh, epoch, root = bytes(range(32)), 0x123456789abcdef0, 0x01020304, 17
    context = bytes([1]) + mesh.to_bytes(8, 'little') + epoch.to_bytes(4, 'little')
    checks = []

    def sign(domain, body):
        message = domain + context + body
        mac = hmac.digest(key, message, 'sha256')
        checks.append((message, mac))
        return mac[:16]

    def array(data):
        return '{' + ','.join(str(b) for b in data) + '}'

    identity = root.to_bytes(8, 'little')
    address = bytearray(sign(b'PTOBG1K1', identity * 2)[:6])
    address[-1] |= 0xc0
    locator = b'\x18\xffG1\x01' + context[1:] + identity
    frame = bytearray(190)
    frame[:8] = b'\xbd\xff\xff\xffGR\x03\x01'
    for offset, n, value in [(8, 2, 50), (10, 4, epoch), (14, 8, root),
                             (30, 8, 9), (38, 4, 1), (42, 8, 1_000_000_000),
                             (50, 8, 2_500_000_000), (58, 8, 999_000_000),
                             (66, 8, 1_794_000_000_000_000_000), (90, 8, root),
                             (98, 8, root), (108, 8, 9), (116, 4, 3), (120, 4, 64),
                             (124, 4, 2), (128, 4, 3), (132, 4, 0xffffffff),
                             (136, 4, 63), (140, 2, 65535), (142, 1, 17),
                             (143, 8, 16_000_000), (151, 4, 3)]:
        frame[offset:offset+n] = value.to_bytes(n, 'little')
    frame[74:90] = sign(b'PTOBG3O3', frame[14:74])
    frame[174:190] = sign(b'PTOBG3R3', frame[:174])

    def join(kind, nonce=33, remaining=1_200_000):
        record = bytearray(168)
        record[:3] = bytes([14, kind, 5])
        request, proof = kind == 1, kind == 3
        outgoing = request or proof
        sender, receiver = (19, root) if outgoing else (root, 19)
        boot, peer_boot = (21, 9) if outgoing else (9, 21)
        clock, echo = {1: (3_000_000, 0), 2: (8_000_000, 3_000_000),
                       3: (3_050_000, 8_000_000), 4: (8_060_000, 3_050_000)}[kind]
        fields = [(4, 4, epoch), (8, 8, sender), (16, 8, receiver), (24, 8, boot),
                  (32, 8, 0 if request else peer_boot), (40, 8, nonce),
                  (48, 8, 0 if request else 41), (56, 8, 1 if kind <= 2 else 2),
                  (64, 8, clock), (72, 8, echo)]
        if kind == 4:
            fields += [(80, 8, root), (88, 8, boot), (96, 4, 1), (100, 4, 3),
                       (104, 8, 1_794_000_000_000_000), (112, 4, remaining),
                       (116, 1, 1), (144, 4, 40_000)]
        for offset, size, value in fields:
            record[offset:offset+size] = value.to_bytes(size, 'little')
        name = b'ESP32-REF' if outgoing else b'T096'
        record[117] = len(name)
        record[118:118+len(name)] = name
        record[151] = 0x80
        record[152:] = sign(b'PTOBTZJ5', record[:152])
        return record

    records = [('REQUEST', join(1)), ('CHALLENGE', join(2)), ('PROOF', join(3)),
               ('ACCEPT', join(4)), ('OLD_ACCEPT', join(4, nonce=31)),
               ('EXPIRING_ACCEPT', join(4, remaining=80_000))]
    rows = [f'#define TEST_EPOCH {epoch}u', f'#define TEST_MESH {mesh}ULL', f'#define TEST_ROOT {root}u']
    for name, value in [('TEST_KEY', key), ('ADDRESS', address), ('LOCATOR', locator), ('FRAME', frame), *records]:
        rows.append(f'static const uint8_t {name}[] = {array(value)};')
    rows.append('static const struct { size_t len; uint8_t input[195], mac[32]; } MACS[] = {')
    rows.extend(f'{{{len(body)}, {array(body)}, {array(mac)}}},' for body, mac in checks)
    path.write_text('\n'.join([*rows, '};', '']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--verify-images', action='store_true')
    args = parser.parse_args()
    os.umask(0o077)
    args.out.mkdir(parents=True)
    out = args.out.resolve()
    cc = shlex.split(os.environ.get('CC', 'cc'))
    flags = ['-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-Wpedantic']
    if args.sanitize:
        flags += ['-fsanitize=undefined', '-fno-sanitize-recover=all']
    includes = [item for p in (RX, CLOCK, UI, LESSON / 'main', out) for item in ('-I', p)]
    sources = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in sorted(ROOT.rglob('*')) if p.is_file() and '__pycache__' not in p.parts}
    fixture(out / 'fixture.h')
    for rate in (20000000, 80000000):
        exe = out / f'receiver-{rate}'
        subprocess.run([*cc, *flags, *includes, f'-DREF_CLOCK_HZ={rate}', RX / 'direct_wire.c',
                        RX / 'receiver_clock.c', CLOCK / 'ref_clock.c', LESSON / 'tests/receiver.c',
                        '-o', exe], check=True)
        subprocess.run([exe], check=True)
    exe = out / 'preview'
    subprocess.run([*cc, *flags, *includes, UI / 'ref_display.c', LESSON / 'main/screen.c',
                    LESSON / 'tests/preview.c', '-o', exe], check=True)
    subprocess.run([exe, out], check=True)
    frames = sorted(out.glob('*.fb'))
    for frame in frames:
        name = frame.stem.split('-', 1)[1]
        path = out / f'w12-{name}.png'
        png(path, [frame.read_bytes()])
        if args.verify_images and name in PUBLISHED:
            assert hashlib.sha256(path.read_bytes()).digest() == hashlib.sha256(
                (LESSON / 'images' / path.name).read_bytes()).digest(), name
    png(out / 'w12-preview.png', [p.read_bytes() for p in frames])
    artifacts = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'receipt.json').write_text(json.dumps({
        'schema': 1, 'lesson': 2, 'status': 'HOST_CHECKS_PASSED',
        'hardware_access': False, 'sources': sources, 'artifacts': artifacts,
        'illustrative_frames': len(frames), 'published_images_verified': args.verify_images,
    }, indent=2, sort_keys=True) + '\n')
    print(f'Rendered {len(frames)} W12 states in {out}')


if __name__ == '__main__':
    main()
