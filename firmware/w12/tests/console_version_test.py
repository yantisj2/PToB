#!/usr/bin/env python3
"""Current candidate normal-status compatibility, before any USB operation.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
from pathlib import Path
import re
import sys

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP / 'tools'))
from framed_console import validate_compact_discovery_manifest

version = re.search(r"^VERSION=['\"]([^'\"]+)",
                    (APP / 'tools/build.py').read_text(), re.M)[1]
contract = dict(version=version, lora_rx=True,
    lora_contract='PASSIVE_WIRE3_SCHEDULE8_LR2021_HEADER_VALID_FIXED922400_SF11_BW500_CR8_RX_ONLY_V1')
validate_compact_discovery_manifest(contract)
for changes in (dict(version='999.0.0'), dict(lora_rx=False),
                dict(lora_contract='UNKNOWN')):
    try:
        validate_compact_discovery_manifest({**contract, **changes})
    except RuntimeError:
        pass
    else:
        raise AssertionError('Invalid ordinary status image contract accepted')
print('PASS current candidate ordinary status compatibility and contract refusal; no hardware')
