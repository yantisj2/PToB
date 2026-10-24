#!/usr/bin/env python3
"""Read-only A1 verifier. Copyright2026 Jonathan Yantis. MIT."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
m=json.loads((root/'manifest.json').read_text())
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
actual={p.name for p in root.iterdir() if p.is_file() and p.name!='manifest.json'}
if actual!=set(m['files']):raise SystemExit('inventory mismatch')
for name,digest in m['files'].items():
 if h(root/name)!=digest:raise SystemExit('digest mismatch: '+name)
if h(root.parent/'gm7_v2/manifest.json')!=m['implementation2_manifest_sha256']:raise SystemExit('implementation2 identity mismatch')
subprocess.run([sys.executable,'-B','-m','unittest','discover','-s',str(root),'-p','test_*.py','-v'],check=True)
print('A1 manifest verified; implementation2 identity unchanged.')
