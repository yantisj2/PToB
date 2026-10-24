#!/usr/bin/env python3
"""Shared display registry validation and T096 header generation.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import json
import re
import uuid
from pathlib import Path


def load(path):
    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError('Duplicate registry key: ' + key)
            out[key] = value
        return out
    data = json.loads(Path(path).read_text(), object_pairs_hook=unique)
    if set(data) != {'schema', 'devices'} or data['schema'] != 1 or not isinstance(data['devices'], dict):
        raise ValueError('Expected registry schema 1')
    if len(data['devices']) > 128:
        raise ValueError('Registry exceeds 128 display entries')
    uuids = set()
    for identity, entry in data['devices'].items():
        if not re.fullmatch('[0-9a-f]{16}', identity) or not int(identity, 16):
            raise ValueError('Expected nonzero 64-bit device ID')
        if not isinstance(entry, dict) or set(entry) - {'alias', 'target', 'uuid'} or not {'alias','target'} <= set(entry):
            raise ValueError('Unexpected device metadata')
        # Deliberately narrower than protocol names: portable embedded display text.
        if not isinstance(entry['alias'], str) or not re.fullmatch('[A-Za-z0-9 _.-]{1,24}', entry['alias']) or not entry['alias'].strip():
            raise ValueError('Alias must be 1-24 printable ASCII display characters')
        if entry['target'] not in ('t096','w12','ss3','macos','n30','corese'):
            raise ValueError('Unknown target family')
        if 'uuid' in entry:
            value = str(uuid.UUID(entry['uuid']))
            if value != entry['uuid'] or value in uuids or int(uuid.UUID(value)) == 0:
                raise ValueError('UUID must be canonical, nonzero and unique')
            uuids.add(value)
    return data


def header(data):
    lines = ['/* Generated display metadata; never membership or authority. */',
             '#include <stdint.h>', '#include <stddef.h>',
             'static inline const char *ptob_registry_name(uint64_t id) {', ' switch (id) {']
    for identity, entry in sorted(data['devices'].items()):
        lines.append(' case UINT64_C(0x' + identity + '): return ' + json.dumps(entry['alias']) + ';')
    return '\n'.join(lines + [' default: return NULL;', ' }', '}', ''])
