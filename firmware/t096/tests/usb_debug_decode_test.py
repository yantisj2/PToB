#!/usr/bin/env python3
"""Observation layout/boundary checks, no device.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from usb_debug_decode import decode

rows = ['F82453C17', 'P12345678', 'G90ABCDEF', 'N000003FF',
        'C01020304', 'I87654321', 'H00010001', 'V00000003']
v13 = decode(rows, 'V13')
assert v13['NVIC_pending'] is True
assert v13['health']['USBD_NVIC_enabled']
assert v13['lifecycle_first_failure'] == 0 and v13['DMA_endpoint'] == '82'
assert decode(rows, 'V12')['NVIC_pending'] is None
assert decode([x.lower() for x in rows], 'V13') == v13
for f, enabled, pending in [('F82051C17', False, False),
                            ('F82451C17', False, True)]:
    result = decode([f] + rows[1:], 'V13')
    assert result['NVIC_pending'] is pending
    assert result['health']['USBD_NVIC_enabled'] is enabled
bad_rows = [rows[:7], rows + ['V00000003'], ['F+0000001'] + rows[1:],
            ['F-0000001'] + rows[1:], ['F0000000Z'] + rows[1:],
            ['F123'] + rows[1:], ['P12345678'] + rows[1:]]
for bad, layout in [(x, 'V13') for x in bad_rows] + [(rows, 'unknown')]:
    try:
        decode(bad, layout)
    except ValueError:
        pass
    else:
        raise AssertionError('Malformed observation was accepted')
print('PASS explicit layouts/old pending unknown; independent IRQ bits; malformed, signed and extra rows rejected')
