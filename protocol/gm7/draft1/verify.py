"""Read-only freeze verifier. Copyright (c) 2026 Jonathan Yantis. MIT."""
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parent.parent
manifest = json.loads((root/'draft1/manifest.json').read_text())
if manifest['revision'] != 'GM7-draft1' or manifest['status'] != 'REVIEW CANDIDATE / NOT FIRMWARE CLEARED':
    raise SystemExit('wrong manifest revision/status')
expected = manifest['files']
actual = {p.relative_to(root).as_posix() for p in root.rglob('*')
          if p.is_file() and '__pycache__' not in p.parts
          and p != root/'draft1/manifest.json'}
if actual != set(expected):
    raise SystemExit(f'file set mismatch: missing={set(expected)-actual}, extra={actual-set(expected)}')
for rel, entry in expected.items():
    data=(root/rel).read_bytes()
    if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise SystemExit(f'freeze mismatch: {rel}')
print(f'GM7-draft1 manifest: {len(expected)} files PASS',flush=True)
suite=unittest.defaultTestLoader.discover(str(root/'draft1'),pattern='test_*.py')
result=unittest.TextTestRunner(verbosity=2).run(suite)
raise SystemExit(0 if result.wasSuccessful() else 1)
