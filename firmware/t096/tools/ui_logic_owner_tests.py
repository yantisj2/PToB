#!/usr/bin/env python3
"""Actual main logical binding/pending overlay and optional write contract.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT"""
import argparse,hashlib,json,re,subprocess
import os
from pathlib import Path
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
A=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False);(o/'zephyr').mkdir();(o/'zephyr/kernel.h').write_text('/* Host header boundary. */\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def function(text,name):
 start=text.index(name);start=text.rfind('\n',0,start)+1;brace=text.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
main=(A/'src/main.c').read_text();body=function(main,'gm7_ui_logic(');assert 'r2_ui_model_requested'not in body and 'r2_ui_work_allowed'not in body
(o/'main_logic_under_test.h').write_text(body)
(o/'time_ui_guard_under_test.h').write_text(function((A/'src/time_service.c').read_text(),'r2_time_ui_guard('))
optional={}
allowed={'gm7_peer_valid','gm7_peer_current','gm7_peer_fresh','gm7_peer_eligible_claim','gm7_peer_id','gm7_peer_role','gm7_peer_known','gm7_peer_hot_ms','gm7_peer_active_ms','gm7_peer_pps_ms','root_name','upstream_name','coordinator_name','pps_precision'}
for n in ['gm7_service.c','rc_dynamic_service.c']:
 text=function((A/'src'/n).read_text(),'r2_service_ui_details(')
 fields=set(re.findall(r'm->([a-zA-Z_0-9]+)',text));assert fields<=allowed|{'sync_attempt'},(n,fields-allowed)
 assert not re.search(r'm->sync_attempt\s*=',text)
 optional[n]=dict(fields=sorted(fields),body_sha256=hashlib.sha256(text.encode()).hexdigest())
cmd=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(A/'include'),'-I'+str(A/'protocol'),'-I'+str(o),str(A/'tests/ui_logic_owner_test.c'),str(A/'src/clock.c'),str(A/'src/nmea.c'),'-o',str(o/'test')]
for n,c in [('compile',cmd),('run',[str(o/'test')])]:
 r=subprocess.run(c,text=True,capture_output=True,timeout=45);(o/(n+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
files=['src/main.c','src/time_service.c','src/clock.c','src/nmea.c','src/gm7_service.c','src/rc_dynamic_service.c','src/rc_profile_service.c','include/ui.h','include/ble_service.h','include/clock.h','include/time_service.h','tests/ui_logic_owner_test.c','tools/ui_logic_owner_tests.py']
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_MAIN_LOGICAL_BINDING_AND_OPTIONAL_WRITE_CONTRACT',hardware=False,inputs={n:sha(A/n)for n in files},optional_details=optional),indent=2)+'\n')
print('PASS actual main logical binding and optional detail write contract')
