#!/usr/bin/env python3
"""Pack or verify the T096 application as nRF52840 UF2 blocks."""
import argparse
from pathlib import Path
import struct

MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30
FAMILY = 0xADA52840
PAYLOAD_SIZE = 256


def pack(source: Path, destination: Path, address: int) -> None:
    data = source.read_bytes()
    count = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    blocks = bytearray()
    for index in range(count):
        chunk = data[index * PAYLOAD_SIZE:(index + 1) * PAYLOAD_SIZE]
        block = bytearray(512)
        struct.pack_into('<8I', block, 0, MAGIC0, MAGIC1, 0x2000,
                         address + index * PAYLOAD_SIZE, PAYLOAD_SIZE,
                         index, count, FAMILY)
        block[32:32 + len(chunk)] = chunk
        struct.pack_into('<I', block, 508, MAGIC_END)
        blocks.extend(block)
    destination.write_bytes(blocks)
    print(f'{destination}: {len(data)} application bytes, {count} UF2 blocks')


def verify(source: Path, address: int) -> None:
    data = source.read_bytes()
    if not data or len(data) % 512:
        raise ValueError('UF2 size must be a nonzero multiple of 512')
    count = len(data) // 512
    for index in range(count):
        block = data[index * 512:(index + 1) * 512]
        fields = struct.unpack_from('<8I', block)
        if fields != (MAGIC0, MAGIC1, 0x2000,
                      address + index * PAYLOAD_SIZE, PAYLOAD_SIZE,
                      index, count, FAMILY):
            raise ValueError(f'UF2 block {index} header mismatch')
        if struct.unpack_from('<I', block, 508)[0] != MAGIC_END:
            raise ValueError(f'UF2 block {index} trailer mismatch')
    initial_sp, reset_vector = struct.unpack_from('<II', data, 32)
    if not (0x20002000 <= initial_sp <= 0x20040000 and
            address <= (reset_vector & ~1) < address + count * PAYLOAD_SIZE and
            reset_vector & 1):
        raise ValueError('application vector table is invalid')
    print(f'{source}: valid {count}-block nRF52840 UF2, SP={initial_sp:#x}, reset={reset_vector:#x}')


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['pack', 'verify'])
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path, nargs='?')
    parser.add_argument('--address', type=lambda s: int(s, 0), required=True)
    args = parser.parse_args()
    if args.command == 'pack':
        if args.destination is None:
            parser.error('pack requires destination')
        pack(args.source, args.destination, args.address)
    else:
        verify(args.source, args.address)


if __name__ == '__main__':
    main()
