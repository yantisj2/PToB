#!/usr/bin/env python3
"""Record local macOS build inputs and tools without executing a live client.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
files=sorted([*HERE.glob('*.swift'),HERE/'Info.plist',HERE/'build.sh',HERE/'ptob',HERE/'registry.swift.py',HERE/'build_receipt.py',HERE/'usb/USBReferenceProbe.m',HERE/'usb/build.sh',HERE/'.build/registry.json'])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
r=dict(schema=1,registry_validator_sha256=sha(HERE.parents[1]/'registry/registry.py'),source_files={str(p.relative_to(HERE)):sha(p) for p in files},binary_sha256=sha(HERE/'.build/ptob-receiver'),usb_helper_sha256=sha(HERE/'.build/usb/reference-probe'),swift_version=subprocess.check_output(['xcrun','swiftc','--version'],text=True).strip(),os_version=subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),hardware_access=False,chronyd_service_started=False,UTC_accuracy='UNQUALIFIED')
(HERE/'.build/build-receipt.json').write_text(json.dumps(r,indent=2,sort_keys=True)+'\n')
