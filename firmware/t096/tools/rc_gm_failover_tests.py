#!/usr/bin/env python3
"""MIT. Copyright (c) 2026 Jonathan Yantis. Offline production failover tests."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
subprocess.run([sys.executable,str(app/'tools/rc_selection_tests.py'),'--out',str(out/'selector')],check=True,timeout=60)
s=(app/'src/rc_dynamic_service.c').read_text()
def function(name):
 m=re.search(r'^static [^\n]*\b'+name+r'\(',s,re.M);assert m
 brace=s.index('{',m.end());end=brace+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
decl=s[s.index('struct discovery {'):s.index('K_MSGQ_DEFINE(discovery_queue')]
(out/'gm_role_under_test.h').write_text(decl+'\n'+function('scan_data')+'\n'+function('gm_role_observed')+'\n'+'\n'.join(function(n)for n in ['parent_failover_start','parent_failover_waiting','parent_failover_attempt']))
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cc=os.environ.get('CC')or shutil.which('clang')
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}'for x in [out,out/'selector',app/'tests',app/'include',app/'protocol',tiny/'include']],str(app/'tests/rc_gm_failover_test.c'),str(app/'src/watchdog_policy.c'),*[str(app/'protocol'/n)for n in ['role_announcement.c','group_auth.c']],*[str(tiny/'source'/n)for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'test')]
passed=False
try:
 with(out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with(out/'run.log').open('x')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [app/'src/rc_dynamic_service.c',app/'src/rc_dynamic_transport.inc',app/'include/rc_gm_failover.h',app/'tests/rc_gm_failover_test.c']}},indent=2)+'\n')
print('PASS actual RC active-GM failover policy and signed ingress')
