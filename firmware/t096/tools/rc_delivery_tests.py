#!/usr/bin/env python3
"""Sanitized production holdover/snapshot bodies; no hardware operations."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
src=app/'src/rc_dynamic_service.c';text=src.read_text()
def function(name):
 start=text.index('static void '+name+'(');end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
(out/'rc_delivery_under_test.h').write_text('\n'.join(function(n) for n in ['delivery_snapshot','holdover_poll']))
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),str(app/'tests/rc_delivery_test.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'sources':{str(f.relative_to(app)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [src,app/'tests/rc_delivery_test.c',Path(__file__).resolve()]},'command':cmd},indent=2)+'\n')
print('PASS production RC delivery transition diagnostics under ASan/UBSan')
