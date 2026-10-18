#!/usr/bin/env python3
"""Production RC7 retry body and recorded-duration policy replay, no hardware.
Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-renewal',action='store_true');p.add_argument('--legacy-retry',action='store_true');p.add_argument('--legacy-recovery',action='store_true');a=p.parse_args()
if sum([a.legacy_renewal,a.legacy_retry,a.legacy_recovery])>1:p.error('Select one negative control')
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
transport=app/'src/rc_dynamic_transport.inc';s=transport.read_text();start=s.index('static void lt_retry_after(');brace=s.index('{',start);end=brace+1;depth=1
while depth:
 if s[end]=='{':depth+=1
 elif s[end]=='}':depth-=1
 end+=1
service=app/'src/rc_dynamic_service.c';body=service.read_text()
def function(name):
 m=re.search(r'^static [^\n;]*\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 stop=m.end();depth=1
 while depth:depth+=(body[stop]=='{')-(body[stop]=='}');stop+=1
 return body[m.start():stop]
recovery='\n'.join(function(n)for n in ['parent_rebind_request','parent_recovery_poll'])
if a.legacy_recovery:
 needle='  if(lt_attempts||lt_budget_until||lt_attempt_ready)return;';assert recovery.count(needle)==1
 recovery=recovery.replace(needle,'  /* negative: continuing loss resets pending retry */')
(out/'rc_relay_retry_under_test.h').write_text(recovery+'\n'+s[start:end])
assert 'bound ? rc_admission_relay_renew_ticks() : 16000000' in s
policy=app/'include/rc_admission_policy.h';extra=[]
if a.legacy_renewal or a.legacy_retry:
 old=policy.read_text()
 if a.legacy_renewal:
  needle='{return UINT64_C(240000000)*16;}'
  assert old.count(needle)==1;old=old.replace(needle,'{return UINT64_C(300000000)*16;}')
 else:
  needle='return delay<latest?delay:latest;';assert old.count(needle)==1;old=old.replace(needle,'(void)latest;return delay;')
 folder=out/'legacy-include';folder.mkdir();policy=folder/policy.name;policy.write_text(old);extra=['-I'+str(folder)]
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';units=[app/'tests/rc_relay_renewal_test.c',app/'src/rc_boundary.c',app/'protocol/rc_dynamic.c',app/'protocol/group_auth.c',*[tiny/'source'/n for n in ('hmac.c','sha256.c','utils.c')]]
cmd=[os.environ.get('CC')or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*extra,'-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,units),'-o',str(out/'test')]
passed=False
try:
 with (out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'run.log').open('x')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'legacy_renewal':a.legacy_renewal,'legacy_retry':a.legacy_retry,'legacy_recovery':a.legacy_recovery,'scope':'ACTUAL_RECOVERY_RETRY_AND_COMBINED_RECORDED_DURATION_START_POLICY_NOT_RF','command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [transport,service,policy,*units]}},indent=2)+'\n')
print('PASS RC7 relay renewal/retry combined policy')
