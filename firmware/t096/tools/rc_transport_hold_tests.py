#!/usr/bin/env python3
"""Actual native RC PA publisher and public codec/receiver, no hardware.
Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=app/'src/rc_dynamic_service.c';text=source.read_text()
def function(name):
 m=re.search(r'^static [^\n;]*\b'+name+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 start=m.start();end=m.end();depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
(out/'rc_transport_publication_under_test.h').write_text('\n'.join(function(n)for n in ['make_publication_path','precision_synced','publish']))
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
units=[app/'tests/rc_transport_hold_test.c',app/'src/rc_boundary.c',app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x')as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'scope':'ACTUAL_PA_PUBLISHER_CODEC_RECEIVER_NOT_RF','command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [source,Path(__file__).resolve(),*units]}},indent=2)+'\n')
print((out/'test.log').read_text())
