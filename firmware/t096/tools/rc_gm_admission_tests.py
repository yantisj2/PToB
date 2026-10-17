#!/usr/bin/env python3
"""Sanitized cold GM admission plus causal diagnostics; no radio or hardware."""
import argparse,hashlib,json,subprocess
from pathlib import Path
app=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(exist_ok=False)
s=(app/'src/rc_dynamic_transport.inc').read_text()
def function(name):
 start=s.index(name);start=s.rfind('static ',0,start);brace=s.index('{',start);end=brace+1;depth=1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[start:end]
(out/'rc_gm_admission_under_test.h').write_text('\n'.join(function(x) for x in ['gm_admission_record(','gm_refusal(','gm_accept(','gm_apply(']))
tiny=a.deps/'modules/crypto/tinycrypt/lib'
units=[app/'tests/rc_gm_admission_test.c',app/'protocol/group_join.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
cmd=[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with (out/'compile.log').open('x') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'run.log').open('x') as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'scope':'ACTUAL_INITIAL_GM_ADMISSION_NO_RETAINED_CONTEXT','command':cmd,'sources':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in [app/'src/rc_dynamic_transport.inc',*units]}},indent=2)+'\n')
print('PASS actual cold GM admission diagnostics with ASan/UBSan')
