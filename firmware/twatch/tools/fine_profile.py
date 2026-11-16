"""Exact watch RX-only shadow or explicitly reviewed relative fine profile.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import hashlib
import json
from pathlib import Path
import re

SCHEMATIC_SHA256 = 'cae987e0d19f87890df668b3313df491907cb9f6e4f5905dc285942c6219ae51'
SHADOW = 'TWATCH_ULTRA_RX17_RAW_SHADOW'
RELATIVE = 'TWATCH_ULTRA_RX17_RELATIVE_FINE'
ENDPOINT_CLASS = dict(phy=0, pdu_length=191, ext_header=0, extra_us=4)


def digest(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) and int(value, 16)


def profile(device, path=None):
    value = {'schema': 1, 'mode': 'CURRENT_ONLY', 'factory': device['device_id'],
             'RX_GPIO': -1, 'TX_GPIO': -1}
    if path is None:
        return value
    value = json.loads(Path(path).read_text())
    required = {'schema', 'mode', 'factory', 'RX_GPIO', 'TX_GPIO',
                'board', 'radio', 'schematic_sha256', 'physical_review_sha256'}
    production = value.get('mode') == RELATIVE
    if production:
        required |= {'phase80', 'endpoint_class', 'phase_evidence_sha256'}
    if set(value) != required or not (
            value['schema'] == 1 and value['mode'] in (SHADOW, RELATIVE)
            and value['factory'] == device['device_id']
            and value['board'] == device['board'] == 'lilygo_twatch_ultra_v1'
            and value['radio'] == device['radio'] == 'sx1262'
            and value['RX_GPIO'] == 17 and value['TX_GPIO'] == -1
            and value['schematic_sha256'] == SCHEMATIC_SHA256
            and digest(value['physical_review_sha256'])
            and (not production or (
                type(value['phase80']) is int and 1 <= value['phase80'] <= 248000
                and value['phase80'] % 4 == 0
                and value['endpoint_class'] == ENDPOINT_CLASS
                and digest(value['phase_evidence_sha256'])))):
        raise ValueError('Exact reviewed Ultra V1 SX1262 RX17-only profile required')
    return value


def header(value):
    enabled = value['mode'] in (SHADOW, RELATIVE)
    production = value['mode'] == RELATIVE
    digest = hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()
    return ('#pragma once\n#include <cstdint>\n'
            '#define PTOB_N30_FINE_FACTORY UINT64_C(0x' + value['factory'] + ')\n'
            '#define PTOB_N30_FINE_PINS ' + str(int(enabled)) + '\n'
            '#define PTOB_N30_FINE_RX_GPIO ' + str(value['RX_GPIO']) + '\n'
            '#define PTOB_N30_FINE_TX_GPIO -1\n'
            '#define PTOB_N30_FINE_RW ' + str(int(enabled)) + '\n'
            '#define PTOB_N30_FINE_BACKEND ' + str(int(production)) + '\n'
            '#define PTOB_N30_FINE_TRANSLATION80 UINT64_C(' + str(value.get('phase80', 0)) + ')\n'
            '/* watch_rx17_profile_sha256=' + digest + ' */\n')
