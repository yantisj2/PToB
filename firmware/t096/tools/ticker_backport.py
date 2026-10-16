#!/usr/bin/env python3
"""Apply only upstream Zephyr PR91430 to exact pinned, generated controller files."""
from pathlib import Path
import hashlib,json,sys
APP=Path(__file__).resolve().parents[1]
def generate(deps,out):
    manifest=json.loads((APP/'cmake/ticker-backport.json').read_text())
    patch=(APP/'cmake/zephyr-91430.patch').read_bytes()
    assert hashlib.sha256(patch).hexdigest()==manifest['patch_sha256']
    edits={};name=None;before=[];after=[]
    for line in patch.decode().splitlines(keepends=True):
        if line.startswith('diff --git '):
            if name and before:edits[name]=(''.join(before),''.join(after))
            name=line.split(' b/',1)[1].strip();before=[];after=[]
        elif line.startswith('@@'):continue
        elif name and not line.startswith(('index ','--- ','+++ ')):
            if line.startswith(' '):before.append(line[1:]);after.append(line[1:])
            elif line.startswith('-'):before.append(line[1:])
            elif line.startswith('+'):after.append(line[1:])
    if name and before:edits[name]=(''.join(before),''.join(after))
    assert set(edits)==set(manifest['files'])
    for name,(old,new) in edits.items():
        body=(deps/name).read_text();assert hashlib.sha256(body.encode()).hexdigest()==manifest['files'][name]['original'],name
        assert body.count(old)==1,name
        fixed=body.replace(old,new);assert hashlib.sha256(fixed.encode()).hexdigest()==manifest['files'][name]['fixed'],name
        dest=out/Path(name).relative_to('subsys/bluetooth/controller');dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.is_file() or dest.read_text()!=fixed:dest.write_text(fixed)
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
