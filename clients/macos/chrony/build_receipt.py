#!/usr/bin/env python3
"""Record hash-verified chrony source/build outputs; never starts a daemon.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json
from pathlib import Path
HERE=Path(__file__).resolve().parent;APP=HERE.parent;OUT=APP/'.build/chrony'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
lock=json.loads((APP/'dependencies.lock.json').read_text())['chrony']
archive=OUT/'chrony-4.9.tar.gz'
if sha(archive)!=lock['sha256']:raise ValueError('Upstream chrony archive identity mismatch')
r=dict(schema=1,upstream=lock,archive_sha256=sha(archive),build_script_sha256=sha(HERE/'build.sh'),receipt_tool_sha256=sha(Path(__file__)),dependency_lock_sha256=sha(APP/'dependencies.lock.json'),binaries={n:sha(OUT/'bin'/n) for n in ('chronyd','chronyc')},installed=False,daemon_service_started=False,system_clock_changed=False)
(OUT/'receipt.json').write_text(json.dumps(r,indent=2,sort_keys=True)+'\n')
