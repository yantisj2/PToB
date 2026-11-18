"""Build-bound shared-clock calibration; never operates a device.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib
import json
import re
from pathlib import Path


def profile(device, fine, path=None):
    value = dict(schema=1, device=device['device_id'], enabled=False,
                 fine_profile_sha256=hashlib.sha256(json.dumps(fine, sort_keys=True).encode()).hexdigest(),
                 ble_existing_ns=0, ble_incremental_ns=0, lora_correction_ns=0,
                 ble_root='0000000000000000', ble_parent='0000000000000000',
                 ble_depth=0, lora_emitter='0000000000000000', lora_root='0000000000000000',
                 ble_validation_sha256=None, lora_validation_sha256=None, review_sha256=None,
                 utc_alignment_confirmed=False, study_fixed_priors=False)
    if path is None:
        return value
    supplied = json.loads(Path(path).read_text())
    # Existing reviewed profiles retain exactly their former behavior.
    supplied.setdefault('study_fixed_priors', False)
    if set(supplied) != set(value):
        raise ValueError('Exact shared-clock profile schema required')
    for key in ('schema', 'device', 'fine_profile_sha256'):
        if supplied[key] != value[key]:
            raise ValueError('Shared-clock profile binding: ' + key)
    for key in ('enabled', 'utc_alignment_confirmed', 'study_fixed_priors'):
        if type(supplied[key]) is not bool:
            raise ValueError('Boolean profile field required: ' + key)
    for key in ('ble_existing_ns', 'ble_incremental_ns', 'lora_correction_ns'):
        # LoRa's uncalibrated event convention can leave millisecond bias.
        # PPS phase alone cannot resolve an integer-second shift, so keep its
        # correction strictly inside the signed half-second phase interval.
        limit = 499999999 if key == 'lora_correction_ns' else 1000000
        if type(supplied[key]) is not int or abs(supplied[key]) > limit:
            raise ValueError('Invalid calibration: ' + key)
    if type(supplied['ble_depth']) is not int or not 0 <= supplied['ble_depth'] <= 2:
        raise ValueError('Invalid BLE path depth')
    for key in ('ble_root', 'ble_parent', 'lora_emitter', 'lora_root'):
        if not isinstance(supplied[key], str) or not re.fullmatch('[0-9a-f]{16}', supplied[key]):
            raise ValueError('Exact source identity required: ' + key)
    if supplied['enabled']:
        if fine.get('mode') != 'TWATCH_ULTRA_RX17_RELATIVE_FINE' or not supplied['utc_alignment_confirmed']:
            raise ValueError('Confirmed common UTC alignment and continuous fine counter required')
        for key in ('ble_root', 'lora_emitter', 'lora_root'):
            if not int(supplied[key], 16):
                raise ValueError('Active profile needs source binding: ' + key)
        if bool(int(supplied['ble_parent'], 16)) != bool(supplied['ble_depth']):
            raise ValueError('BLE direct/relay calibration applicability mismatch')
        for key in ('ble_validation_sha256', 'lora_validation_sha256', 'review_sha256'):
            digest = supplied[key]
            if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest) or not int(digest, 16):
                raise ValueError('Held-out validation and candidate review required: ' + key)
    elif supplied['study_fixed_priors']:
        raise ValueError('Study priors require an enabled reviewed profile')
    return supplied


def header(value):
    keys = ('ble_root', 'ble_parent', 'lora_emitter', 'lora_root')
    values = ['true' if value['enabled'] else 'false']
    values += ['UINT64_C(0x' + value[key] + ')' for key in keys]
    values += [str(value[key]) for key in ('ble_depth', 'ble_incremental_ns', 'lora_correction_ns', 'ble_existing_ns')]
    values += ['true' if value.get('study_fixed_priors', False) else 'false']
    return ('#pragma once\n#include "dual_profile.hpp"\nnamespace watchdual {\n'
            'static constexpr Profile profile{' + ','.join(values) + '};\n}\n')
