"""P1 offline design model. No device, file, serial or network I/O.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Not production firmware, a provisioning tool, or a hardware flash model.
"""
from dataclasses import dataclass, field
import struct
import zlib

PAGE_SIZE = 4096
COMMIT_OFFSET = PAGE_SIZE - 4
ERASED = bytes([255]) * PAGE_SIZE
FORMAT = struct.Struct('<8sHHQQQIBBBB24s32s24sI')
MAX_GENERATION = (1 << 64) - 1


def valid_name(value):
    try:
        encoded = value.encode('utf-8', errors='strict')
    except (UnicodeError, AttributeError):
        return False
    if not 1 <= len(encoded) <= 24:
        return False
    return all(not (ord(c) < 32 or 127 <= ord(c) <= 159 or
                    0x2028 <= ord(c) <= 0x202e or
                    0x2066 <= ord(c) <= 0x2069) for c in value)


@dataclass(frozen=True)
class Record:
    generation: int
    device_id: int
    group_id: int = 0
    epoch: int = 0
    kind: int = 2
    role: int = 0
    name: str = ''
    psk: bytes = field(default=bytes(32), repr=False)


def encode(record):
    r = record
    if not (type(r.generation) is int and 0 < r.generation <= MAX_GENERATION
            and type(r.device_id) is int and 0 < r.device_id < 1 << 64):
        raise ValueError('Invalid generation or device identity')
    if type(r.psk) is not bytes or len(r.psk) != 32:
        raise ValueError('Invalid key format')
    if type(r.kind) is not int or type(r.role) is not int or type(r.name) is not str:
        raise ValueError('Invalid field types')
    if r.kind == 1:
        if not (type(r.group_id) is int and 0 < r.group_id < 1 << 64 and
                type(r.epoch) is int and 0 < r.epoch < 1 << 32 and
                r.role in (1, 2) and valid_name(r.name) and any(r.psk)):
            raise ValueError('Invalid provisioned record')
    elif r.kind == 2:
        if r.group_id or r.epoch or r.role or r.name or any(r.psk):
            raise ValueError('Tombstone contains configuration')
    else:
        raise ValueError('Unknown record kind')
    data = FORMAT.pack(b'PTOBPRV1', 1, 128, r.generation, r.device_id,
                       r.group_id, r.epoch, r.kind, r.role,
                       len(r.name.encode('utf-8')), 0,
                       r.name.encode('utf-8').ljust(24, b'\0'),
                       r.psk, bytes(24), 0)
    return data[:124] + struct.pack('<I', zlib.crc32(data[:124]))


def decode(page, device_id):
    """Return (empty|pending|valid|invalid, record or None)."""
    if len(page) != PAGE_SIZE:
        return 'invalid', None
    if page == ERASED:
        return 'empty', None
    if page[COMMIT_OFFSET:] == b'\xff' * 4:
        return 'pending', None
    if page[COMMIT_OFFSET:] != bytes(4):
        return 'invalid', None
    try:
        magic, version, length, gen, dev, group, epoch, kind, role, n, flags, name, key, reserved, crc = FORMAT.unpack(page[:128])
        if (magic != b'PTOBPRV1' or version != 1 or length != 128 or
                dev != device_id or n > 24 or flags or reserved != bytes(24)
                or any(name[n:]) or page[128:COMMIT_OFFSET] != ERASED[128:COMMIT_OFFSET]
                or crc != zlib.crc32(page[:124])):
            return 'invalid', None
        record = Record(gen, dev, group, epoch, kind, role,
                        name[:n].decode('utf-8', errors='strict'), key)
        if encode(record) != page[:128]:
            return 'invalid', None
        return 'valid', record
    except (ValueError, UnicodeError, struct.error):
        return 'invalid', None


def boot(pages, device_id):
    """Return (state, selected record). cleanup never authorizes service."""
    if len(pages) != 2:
        return 'quarantine', None
    states = [decode(p, device_id) for p in pages]
    if any(s == 'invalid' for s, _ in states):
        return 'quarantine', None
    valid = [r for s, r in states if s == 'valid']
    if not valid:
        return ('unprovisioned' if all(s == 'empty' for s, _ in states)
                else 'cleanup'), None
    if len(valid) == 2 and valid[0].generation == valid[1].generation:
        return 'quarantine', None
    selected = max(valid, key=lambda r: r.generation)
    if sum(p != ERASED for p in pages) > 1:
        return 'cleanup', selected
    return ('active' if selected.kind == 1 else 'unprovisioned'), selected


def transition(pages, record):
    """Yield flash snapshots after each modeled operation, including erase words.

    The caller models maintenance with service already withdrawn. Erase-word
    steps are fault probes, not a claim about physical erase progression.
    """
    pages = [bytearray(p) for p in pages]
    state, old = boot(pages, record.device_id)
    if state not in ('active', 'unprovisioned'):
        raise ValueError('Store requires recovery before mutation')
    expected = old.generation + 1 if old else 1
    if record.generation != expected or expected > MAX_GENERATION:
        raise ValueError('Stale or exhausted generation')
    payload = encode(record)
    target = next(i for i, p in enumerate(pages) if p == ERASED)
    for offset in range(0, len(payload), 4):
        pages[target][offset:offset+4] = payload[offset:offset+4]
        yield tuple(bytes(p) for p in pages)
    # Probe torn commit byte boundaries conservatively.
    for n in range(1, 5):
        pages[target][COMMIT_OFFSET:COMMIT_OFFSET+n] = bytes(n)
        yield tuple(bytes(p) for p in pages)
    previous = 1 - target
    for offset in range(0, PAGE_SIZE, 4):
        pages[previous][offset:offset+4] = b'\xff' * 4
        yield tuple(bytes(p) for p in pages)
