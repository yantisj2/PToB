"""Fault-injection acceptance examples for draft P1 (no hardware).
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
from dataclasses import replace
import struct
import unittest
import zlib
from store_model import (Record, ERASED, FORMAT, COMMIT_OFFSET, MAX_GENERATION,
                         encode, decode, boot, transition, valid_name)

# Public synthetic fixtures, never enrollment defaults.
DEVICE = 0x0123456789abcdef
FIRST = Record(1, DEVICE, 9, 1, 1, 2, 'Roof clock', bytes(range(32)))
SECOND = replace(FIRST, generation=2, epoch=2, psk=bytes(range(32, 64)))


def page(record):
    return encode(record) + ERASED[128:COMMIT_OFFSET] + bytes(4)


class StorageTests(unittest.TestCase):
    def test_layout_and_endianness(self):
        self.assertEqual(FORMAT.size, 128)
        self.assertEqual(encode(FIRST)[20:28], bytes.fromhex('efcdab8967452301'))
        self.assertEqual(decode(page(FIRST), DEVICE), ('valid', FIRST))
        self.assertNotIn('psk=', repr(FIRST))

    def test_install_faults(self):
        for pages in transition((ERASED, ERASED), FIRST):
            state, record = boot(pages, DEVICE)
            self.assertIn(state, ('cleanup', 'quarantine', 'active'))
            if state == 'active':
                self.assertEqual(record, FIRST)
        self.assertEqual(boot(pages, DEVICE), ('active', FIRST))

    def test_rotation_never_reactivates_old_after_commit(self):
        committed = False
        for pages in transition((page(FIRST), ERASED), SECOND):
            committed |= decode(pages[1], DEVICE)[0] == 'valid'
            state, record = boot(pages, DEVICE)
            self.assertIn(state, ('cleanup', 'quarantine', 'active'))
            if committed and record:
                self.assertEqual(record, SECOND)
            if state == 'active':
                self.assertEqual(record, SECOND)
        self.assertEqual(pages[0], ERASED)
        self.assertEqual(boot(pages, DEVICE), ('active', SECOND))

    def test_erase_tombstone_never_serves(self):
        tombstone = Record(2, DEVICE)
        for pages in transition((page(FIRST), ERASED), tombstone):
            state, record = boot(pages, DEVICE)
            self.assertNotEqual(state, 'active')
            if decode(pages[1], DEVICE)[0] == 'valid' and record:
                self.assertEqual(record, tombstone)
        self.assertEqual(pages[0], ERASED)
        self.assertEqual(boot(pages, DEVICE), ('unprovisioned', tombstone))
        new = replace(FIRST, generation=3)
        final = list(transition(pages, new))[-1]
        self.assertEqual(boot(final, DEVICE), ('active', new))

    def test_committed_corruption_never_falls_back(self):
        # Every bit in the payload and commit; marker bits become invalid,
        # never a reason to activate the old page.
        for offset in list(range(128)) + list(range(COMMIT_OFFSET, 4096)):
            for bit in range(8):
                damaged = bytearray(page(SECOND)); damaged[offset] ^= 1 << bit
                self.assertEqual(boot((page(FIRST), damaged), DEVICE)[0], 'quarantine')

    def test_unknown_schema_and_identity(self):
        data = bytearray(page(SECOND))
        struct.pack_into('<H', data, 8, 2)
        struct.pack_into('<I', data, 124, zlib.crc32(data[:124]))
        self.assertEqual(boot((page(FIRST), data), DEVICE)[0], 'quarantine')
        self.assertEqual(boot((page(FIRST), ERASED), DEVICE+1)[0], 'quarantine')
        self.assertEqual(boot((page(FIRST), page(FIRST)), DEVICE)[0], 'quarantine')

    def test_no_writes_from_ambiguous_or_stale_store(self):
        cases = [((page(FIRST), ERASED), FIRST),
                 ((page(FIRST), page(FIRST)), SECOND),
                 ((page(replace(FIRST, generation=MAX_GENERATION)), ERASED), FIRST)]
        for pages, record in cases:
            with self.assertRaises(ValueError):
                list(transition(pages, record))

    def test_names_and_tombstone_validation(self):
        for name in ['Roof clock', 'é'*12, 'a'*24]:
            self.assertTrue(valid_name(name))
        for name in ['', 'é'*13, '\0', '\n', '\x7f', '\x85', '\u202e', '\u2066', '\ud800']:
            self.assertFalse(valid_name(name))
        for record in [replace(FIRST, psk=bytes(32)), replace(FIRST, role=0),
                       replace(FIRST, group_id=0), replace(FIRST, epoch=0),
                       replace(FIRST, kind=2), replace(FIRST, name='a'*25)]:
            with self.assertRaises(ValueError):
                encode(record)


if __name__ == '__main__':
    unittest.main()
