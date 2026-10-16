#!/usr/bin/env python3
"""Check retained imported-file identities after curated migration.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
m=json.loads((APP/'imports/2026-10-16.json').read_text())
for row in m['files']:
 p=APP/row['destination']
 if hashlib.sha256(p.read_bytes()).hexdigest()!=row['imported_sha256']:raise ValueError('Imported source changed; update the public manifest: '+row['destination'])
print('PASS',len(m['files']),'selected imported identities')
