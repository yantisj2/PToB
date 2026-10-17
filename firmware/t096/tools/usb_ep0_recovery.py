#!/usr/bin/env python3
"""Materialize pinned Nordic EP0 generation and restart/power corrections."""
from pathlib import Path
import hashlib,json,re,sys
APP=Path(__file__).resolve().parents[1]
def generate(deps,out):
    m=json.loads((APP/'cmake/usb-ep0-recovery.json').read_text())
    patch=(APP/'cmake/usb-ep0-recovery.patch').read_bytes()
    sha=lambda b:hashlib.sha256(b).hexdigest()
    assert sha(patch)==m['patch_sha256']
    body=(deps/m['path']).read_bytes();assert sha(body)==m['original']
    original=body.decode().splitlines(keepends=True);fixed=[];pos=0
    lines=iter(patch.decode().splitlines(keepends=True))
    for line in lines:
        if line.startswith(('---','+++')):continue
        if line.startswith('@@'):
            start=int(re.match(r'@@ -(\d+)',line)[1])-1
            assert start>=pos;fixed.extend(original[pos:start]);pos=start
        elif line.startswith((' ','-')):
            assert original[pos]==line[1:];pos+=1
            if line[0]==' ':fixed.append(line[1:])
        elif line.startswith('+'):fixed.append(line[1:])
        else:raise AssertionError('Unexpected patch line')
    fixed.extend(original[pos:]);data=''.join(fixed).encode();assert sha(data)==m['fixed']
    out.parent.mkdir(parents=True,exist_ok=True)
    if not out.is_file() or out.read_bytes()!=data:out.write_bytes(data)
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
