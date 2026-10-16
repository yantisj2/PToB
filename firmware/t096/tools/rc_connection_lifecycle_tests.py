#!/usr/bin/env python3
"""Sanitized production RC retirement functions; no hardware access."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];source=app/'src/rc_dynamic_transport.inc';s=source.read_text();out=a.out.resolve();out.mkdir(exist_ok=False)
def function(name):
 m=re.search(r'^static [^\n]*\b'+name+r'\(',s,re.M);assert m,name
 start=m.start();brace=s.index('{',m.end());depth=1;i=brace+1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
start=s.index('struct lt_retired_connection {');end=s.index('static bool lt_retirement_error;',start)+len('static bool lt_retirement_error;')
(out/'rc_connection_lifecycle_under_test.h').write_text(s[start:end]+'\n'+'\n'.join(function(n) for n in ['lt_disconnect','lt_closed_poll','lt_apply_connected','lt_retired_find','lt_retired_take','lt_retire_events','lt_close','lt_reset'])+'\n')
flags=[(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run([*flags,str(app/'tests/rc_connection_lifecycle_test.c'),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'actual_production_functions':['lt_disconnect','lt_closed_poll','lt_apply_connected','lt_retired_find','lt_retired_take','lt_retire_events','lt_close','lt_reset'],'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'physical':'UNKNOWN','original_local_deadline_ms':500,'tested_pool_capacity':6,'cycles':12},indent=2)+'\n')
print((out/'test.log').read_text())
