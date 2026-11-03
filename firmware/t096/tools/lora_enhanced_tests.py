#!/usr/bin/env python3
"""P30/D120/M300 production core,48h budget and independent sparse HOLD oracle.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No hardware or private credentials.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path

def trunc(n,d):return n//d if n>=0 else -((-n)//d)
def sparse(points,seconds):
 selected=[p for p in points if points[-1][0]-p[0]<=seconds*16000000]
 if len(selected)<4 or (selected[-1][0]-selected[0][0])//16000<seconds*850:return 0,4294967295
 rates=[]
 for i,(tick,ns) in enumerate(selected):
  for other,value in selected[i+1:]:
   if other-tick<60*16000000:continue
   nominal=(other-tick)*125//2;remote=value-ns
   if not remote or abs(nominal-remote)>nominal//10000:continue
   rate=trunc((nominal-remote)*1000000000,remote)
   if abs(rate)<=100000:rates.append(rate)
 if len(rates)<8:return 0,4294967295
 rates.sort();rate=rates[(len(rates)-1)//2]
 spread=sorted(abs(v-rate)for v in rates)[(len(rates)-1)//2]
 return rate,3*spread+1

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 sources=[app/'tests/lora_enhanced_test.c',app/'src/lora_passive_core.c',app/'protocol/lora_passive.c',app/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 cmd=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-DR2_LORA_PASSIVE_SLOW=1','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(tiny/'include'),*map(str,sources),'-o',str(out/'core')]
 subprocess.run(cmd,check=True)
 logs=[];points=[];checks=0;models={}
 for mode in (1,2):
  r=subprocess.run([str(out/'core'),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'test.log').write_text(''.join(logs));r.check_returncode()
  for line in r.stdout.splitlines():
   if line.startswith('MODEL '):
    index,tick,ns,rate,phase,offset=map(int,line.split()[1:]);models[index]=(tick,ns,rate,phase,offset);continue
   if not line.startswith('PAIR '):continue
   index,tick,ns,fast,chosen,short,sg,long,lg,selected,rms=map(int,line.split()[1:]);points.append((tick,ns));points=[v for v in points if tick-v[0]<=300*16000000][-16:]
   if index>=6:
    assert (short,sg)==sparse(points,180),(index,short,sg,sparse(points,180))
    assert (long,lg)==sparse(points,300),(index,long,lg,sparse(points,300))
    mt,mn,mr,mp,offset=models[index]
    residuals=sorted(value+(tick-at)*125//2+trunc((tick-at)*chosen,16000000)-ns for at,value in points)
    assert offset==residuals[(len(residuals)-1)//2]
    target=ns+offset+(mt-tick)*125//2+trunc((mt-tick)*chosen,16000000)
    assert mn+mp==target
    checks+=1
 print('PASS actual48h scheduler budgets, sparse HOLD and independent Python pair-slope oracle:',checks,'fits')
 subprocess.run([__import__('sys').executable,str(app/'tests/lora_holdover_analyze_test.py')],check=True)
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware=False,oracle_checks=checks,commands=cmd,sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [*sources,app/'include/holdover.h',app/'include/lora_passive_core.h',app/'protocol/lora_passive_schedule.h',app/'tools/lora_holdover_analyze.py',app/'tests/lora_holdover_analyze_test.py',Path(__file__)]}),indent=2)+'\n')
if __name__=='__main__':main()
