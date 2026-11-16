#!/usr/bin/env python3
"""Decode photographed USB System rows with explicit observation layout.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse
import json


def decode(rows, layout):
    if layout not in ('V12', 'V13'):
        raise ValueError('Observation layout must be V12 or V13')
    if len(rows) != 8:
        raise ValueError('Exactly eight ordered rows are required')
    words = {}
    for expected, row in zip('FPGNCIHV', rows):
        if (len(row) != 9 or row[0].upper() != expected or
                any(c not in '0123456789abcdefABCDEF' for c in row[1:])):
            raise ValueError('Expected ' + expected + ' and eight hexadecimal digits')
        words[expected] = int(row[1:], 16)
    f = words['F']
    names = ['attached', 'driver_ready', 'VBUS_present', 'suspended_any',
             'common_enabled', 'worker_pending', 'EP0_SETUP_latched',
             'USB_RESET_latched', 'DMA_owned', 'PHY_inconsistent',
             'regulator_ready', 'peripheral_enabled', 'pullup_readback',
             'USBD_NVIC_enabled', 'hardware_LOWPOWER', 'software_bus_suspend',
             'errata199', 'errata162', 'errata211']
    return {'layout': layout, 'raw': {k: f'{v:08X}' for k, v in words.items()},
            'health': {name: bool(f & (1 << i)) for i, name in enumerate(names)},
            'lifecycle_first_failure': (f >> 19) & 7,
            'DMA_endpoint': f'{f >> 24:02X}', 'progress': words['P'],
            'EP0_generation': words['G'], 'frame_counter': words['N'],
            'EVENTCAUSE': words['C'], 'peripheral_INTEN': words['I'],
            'NVIC_pending': bool(f & (1 << 22)) if layout == 'V13' else None,
            'HFCLK_running': bool(words['H'] & 0x10000),
            'HFCLK_source': 'XTAL' if words['H'] & 1 else 'RC',
            'raw_VBUS_detected': bool(words['V'] & 1),
            'raw_USB_output_ready': bool(words['V'] & 2),
            'limits': ['A live state does not identify the preceding trigger.',
                       'Bind exact device, image, boot and layout externally.',
                       'Low-power register reads can be inaccessible.',
                       'NVIC pending is unknown in V12; a pending bit alone is not a fault.']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--layout', required=True, choices=('V12', 'V13'))
    p.add_argument('rows', nargs=8,
                   help='Fxxxxxxxx Pxxxxxxxx Gxxxxxxxx Nxxxxxxxx Cxxxxxxxx Ixxxxxxxx Hxxxxxxxx Vxxxxxxxx')
    a = p.parse_args()
    try:
        print(json.dumps(decode(a.rows, a.layout), indent=2))
    except ValueError as exc:
        p.error(str(exc))


if __name__ == '__main__':
    main()
