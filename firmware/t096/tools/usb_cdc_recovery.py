#!/usr/bin/env python3
"""Generate pinned USB recovery units without changing the dependency checkout.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Generated Zephyr sources and patch retain Apache-2.0.
"""
import hashlib,json,re,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def apply(source,patch):
    lines=source.splitlines(keepends=True);out=[];position=0;i=0
    while i<len(patch):
        match=re.fullmatch(r'@@ -(\d+),(\d+) \+(\d+),(\d+) @@\n',patch[i])
        assert match,patch[i]
        start,old_count,_,new_count=map(int,match.groups());start-=1;i+=1
        before=[];after=[]
        while i<len(patch) and not patch[i].startswith('@@'):
            line=patch[i];assert line[0] in ' +-'
            if line[0] in ' -':before.append(line[1:])
            if line[0] in ' +':after.append(line[1:])
            i+=1
        assert len(before)==old_count and len(after)==new_count
        assert start>=position and lines[start:start+old_count]==before
        out.extend(lines[position:start]);out.extend(after);position=start+old_count
    return ''.join(out+lines[position:])
def generate(zephyr,out):
    manifest=json.loads((APP/'cmake/usb-cdc-recovery.json').read_text())
    raw=(APP/'cmake/usb-cdc-recovery.patch').read_bytes()
    assert manifest['schema']==1 and sha(raw)==manifest['patch_sha256']
    sections={};lines=raw.decode().splitlines(keepends=True);i=0
    while i<len(lines):
        assert lines[i].startswith('--- a/');name=lines[i][6:].strip();i+=1
        assert lines[i]=='+++ b/'+name+'\n';i+=1;body=[]
        while i<len(lines) and not lines[i].startswith('--- a/'):
            body.append(lines[i]);i+=1
        assert name not in sections;sections[name]=body
    assert set(sections)=={f['file'] for f in manifest['files']}
    generated={}
    for entry in manifest['files']:
        source=(zephyr/entry['file']).read_bytes();assert sha(source)==entry['input_sha256']
        fixed=apply(source.decode(),sections[entry['file']]).encode()
        assert sha(fixed)==entry['output_sha256'] and entry['leaf'] not in generated
        generated[entry['leaf']]=fixed
    out.mkdir(parents=True,exist_ok=True)
    for leaf,data in generated.items():(out/leaf).write_bytes(data)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest
if __name__=='__main__':
    assert len(sys.argv)==3;generate(Path(sys.argv[1]),Path(sys.argv[2]))
