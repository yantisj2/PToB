#!/usr/bin/env python3
"""Exact public Zephyr/ZMK controller fixes in generated copies only.
MIT generator, Copyright(c)2026 Jonathan Yantis; generated sources retain Apache-2.0.
"""
import hashlib,json,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def hunks(patch):
    result={};name=None;before=[];after=[]
    def finish():
        if before or after:result.setdefault(name,[]).append((''.join(before),''.join(after)))
    for line in patch.decode().splitlines(keepends=True):
        if line.startswith('diff --git '):
            finish();name=line.split(' b/',1)[1].strip();before=[];after=[]
        elif line.startswith('@@'):
            finish();before=[];after=[]
        elif name and line.startswith(('--- ','+++ ','index ')):continue
        elif name:
            if line.startswith(' '):before.append(line[1:]);after.append(line[1:])
            elif line.startswith('-'):before.append(line[1:])
            elif line.startswith('+'):after.append(line[1:])
            else:break
    finish();return result

def transform(zephyr,manifest):
    bodies={name:(zephyr/name).read_text() for name in manifest['files']}
    for name,s in bodies.items():assert sha(s.encode())==manifest['files'][name]['input_sha256'],name
    for upstream in manifest['upstream']:
        patch=(APP/'cmake'/upstream['patch']).read_bytes();assert sha(patch)==upstream['patch_sha256']
        assert patch.startswith(('From '+upstream['commit']+' ').encode())
        for name,edits in hunks(patch).items():
            assert name in bodies
            for before,after in edits:
                assert before and bodies[name].count(before)==1,(name,before)
                bodies[name]=bodies[name].replace(before,after,1)
    # Literal executable changes above; update the two now-stale comments only.
    name='subsys/bluetooth/controller/ll_sw/nordic/lll/lll_conn.c'
    for role in ['central','peripheral']:
        old=('\t/* Do not be aborted by same event if a single central trx has not been\n\t * exchanged.\n\t */' if role=='central' else '\t/* Do not be aborted by same event if a single peripheral trx has not\n\t * been exchanged.\n\t */')
        assert bodies[name].count(old)==1
        bodies[name]=bodies[name].replace(old,'\t/* ZMK10ba6d0: cancel a repeated zero-exchange event so the prepare\n\t * pipeline can advance after the asynchronous radio completion.\n\t */',1)
    return bodies

def generate(zephyr,out):
    manifest=json.loads((APP/'cmake/controller-radio-fixes.json').read_text())
    assert manifest['schema']==1 and len(manifest['upstream'])==2 and len(manifest['files'])==2
    assert sha((APP/'cmake/controller-radio-fixes.patch').read_bytes())==manifest['patch_sha256']
    bodies=transform(zephyr,manifest)
    out.mkdir(parents=True,exist_ok=True)
    for name,s in bodies.items():
        assert sha(s.encode())==manifest['files'][name]['output_sha256'],name
        p=out/Path(name).name
        if not p.exists()or p.read_text()!=s:p.write_text(s)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':
    assert len(sys.argv)==3
    generate(Path(sys.argv[1]),Path(sys.argv[2]))
