#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Actual UCS cleanup, no hardware."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-half-second',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
source=app/'src/rc_unicast.c';service=app/'src/rc_dynamic_service.c'
capacity=app/'protocol/unicast_time.h'
(out/'uc_capacity_under_test.h').write_text(re.search(r'^#define UC1_CLIENTS .*$',capacity.read_text(),re.M).group()+'\n')
def function(path,name):
 s=path.read_text();m=re.search(r'^(?:static )?[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 start=m.start();end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
body='\n'.join(function(source,n) for n in ['find','disconnected','sent','r2_uc_close_begin','r2_uc_close_until','r2_uc_quiescent'])+'\n'+function(service,'cleanup_progress')
if a.legacy_half_second:
 body=body.replace('bool r2_uc_close_until(uint64_t until, void (*progress)(void)) {','bool r2_uc_close_until(uint64_t until, void (*progress)(void)) {\n until=now_us()+500000;')
s=service.read_text();begin=s.index('bool uc_closed = !transmitter || r2_uc_close_begin();');lt=s.index('if (!lt_close())',begin);wait=s.index('r2_uc_close_until(acl_until,cleanup_progress)',lt)
assert begin<lt<wait and 'if (!uc_closed)' in s[wait:wait+200]
assert 'if(status.close_deadline)acl_until=status.close_deadline;' in s
(out/'uc_cleanup_under_test.h').write_text(body)
passed=False
try:
 with (out/'test.log').open('w') as log:
  subprocess.run([(os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else 'clang')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(app/'include'),str(app/'tests/uc_cleanup_test.c'),'-o',str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps(dict(passed=passed,hardware=False,negative_old_half_second=a.legacy_half_second,production_functions=['find','disconnected','sent','r2_uc_close_begin','r2_uc_close_until','r2_uc_quiescent','cleanup_progress'],caller_order_source_checked=True,sources={str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source,service,capacity,app/'include/cleanup_budget.h']}),indent=2)+'\n')
print((out/'test.log').read_text())
