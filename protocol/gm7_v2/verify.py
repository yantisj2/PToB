#!/usr/bin/env python3
"""Read-only implementation2 freeze verification. MIT Jonathan Yantis 2026."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
manifest=json.loads((root/'manifest.json').read_text())
expected=set(manifest['files'])
actual={p.name for p in root.iterdir() if p.is_file() and p.name!='manifest.json'}
if actual!=expected:raise SystemExit('artifact inventory mismatch')
for name,digest in manifest['files'].items():
 if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:raise SystemExit('digest mismatch: '+name)
report=root.parent.parent/'docs/t096/gm7-firmware-clearance.md'
if hashlib.sha256(report.read_bytes()).hexdigest()!=manifest['clearance_report_sha256']:raise SystemExit('clearance report mismatch')
old=root.parent/'gm7/draft1/manifest.json'
if hashlib.sha256(old.read_bytes()).hexdigest()!=manifest['draft1_manifest_sha256']:raise SystemExit('draft1 identity mismatch')
subprocess.run([sys.executable,'-B','-m','unittest','discover','-s',str(root),'-p','test_*.py','-v'],check=True)
print('Verified',len(expected),'revision2 artifacts and clearance report; draft1 identity preserved.')
