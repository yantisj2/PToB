"""Check shared coarse-wire ABI selection in actual native compiler commands.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import shlex


def require_tz_compile_mode(commands):
    required = ('gm7_controller.c', 'gm7_owner.c', 'gm7_codec.c',
                'gm7_service.c', 'rc_dynamic_service.c')
    seen = {name: 0 for name in required}
    checked = 0
    for row in commands:
        args = row.get('arguments') or shlex.split(row['command'])
        defines = [a[2:] for a in args if a.startswith('-D')]
        leaf = row['file'].rsplit('/', 1)[-1]
        if leaf in seen:
            seen[leaf] += 1
        if 'R2_GM7_NATIVE=1' not in defines and leaf not in seen:
            continue
        modes = [a for a in defines if a.split('=', 1)[0] == 'R2_TZ_WIRE']
        if modes != ['R2_TZ_WIRE=1']:
            raise ValueError('Mixed coarse-wire ABI: ' + row['file'])
        checked += 1
    if any(count != 1 for count in seen.values()):
        raise ValueError('Missing/duplicate coarse-wire application/controller units')
    return checked
