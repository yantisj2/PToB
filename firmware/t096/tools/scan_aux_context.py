#!/usr/bin/env python3
"""Hash-pinned periodic AUX context correction in a generated copy only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Generated Zephyr source and patch retain Apache-2.0.
"""
import hashlib,json,re,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def generate(zephyr,out):
    m=json.loads((APP/'cmake/scan-aux-context.json').read_text())
    p=(APP/'cmake/scan-aux-context.patch').read_bytes()
    assert m['schema']==1 and sha(p)==m['patch_sha256']
    s=(zephyr/m['file']).read_text()
    assert sha(s.encode())==m['input_sha256']
    before=[];after=[];start=None
    for line in p.decode().splitlines(keepends=True):
        if line.startswith('@@'):
            assert start is None
            start=int(re.fullmatch(r'@@ -(\d+),\d+ \+\d+,\d+ @@\n',line)[1])-1
            continue
        if line.startswith(('---','+++')):continue
        if line.startswith((' ','-')):before.append(line[1:])
        if line.startswith((' ','+')):after.append(line[1:])
    a,b=''.join(before),''.join(after)
    lines=s.splitlines(keepends=True)
    assert a and start is not None and ''.join(lines[start:start+len(before)])==a
    s=''.join(lines[:start])+b+''.join(lines[start+len(before):])
    assert sha(s.encode())==m['output_sha256']
    out.mkdir(parents=True,exist_ok=True)
    (out/'ull_scan_aux.c').write_text(s)
    (out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
    return m
if __name__=='__main__':
    assert len(sys.argv)==3
    generate(Path(sys.argv[1]),Path(sys.argv[2]))
