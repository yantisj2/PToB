#!/usr/bin/env python3
"""Read-only B1 verification. MIT Jonathan Yantis2026."""
import hashlib,json,subprocess,sys
from pathlib import Path
p=Path(__file__).resolve().parent
m=json.loads((p/'manifest.json').read_text());h=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
if {f.name for f in p.iterdir() if f.is_file() and f.name!='manifest.json'}!=set(m['files']):raise SystemExit('inventory mismatch')
for name,digest in m['files'].items():
 if h(p/name)!=digest:raise SystemExit('artifact mismatch: '+name)
for name,digest in m['bindings'].items():
 if h(p.parent/name/'manifest.json')!=digest:raise SystemExit('binding mismatch: '+name)
subprocess.run([sys.executable,'-B','-m','unittest','discover','-s',str(p),'-p','test_*.py','-v'],check=True)
print('B1 artifacts and implementation2/A1 identities verified.')
