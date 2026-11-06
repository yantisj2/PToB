#!/usr/bin/env python3
"""Source-bound offline receiver state-machine gate. Never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, os, subprocess, sys, time
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
REPO=APP.parents[1]
def sources(repo=REPO):
 result={}
 for name in ('protocol/time-zone-v1/vectors.json',):
  result[name]=hashlib.sha256((repo/name).read_bytes()).hexdigest()
 for target in ('n30','w12'):
  for directory in ('main','tests','tools','contracts','legacy'):
   for p in sorted((repo/'firmware'/target/directory).rglob('*')):
    if p.is_file() and p.suffix in ('.c','.cpp','.h','.hpp','.inc','.py','.json') and '__pycache__' not in p.parts:
     result[str(p.relative_to(repo))]=hashlib.sha256(p.read_bytes()).hexdigest()
 return result
def validate_receipt(path,repo=REPO):
 r=json.loads(Path(path).read_text())
 if r.get('status')!='PASS' or r.get('hardware_access') is not False or r.get('sources')!=sources(repo):
  raise ValueError('Receiver state-machine gate is missing, failed or stale for these sources')
 expected={'source-ui-sequences','native-n30-coupling','native-w12-recovery','w12-production-holdover'}
 if {x['name'] for x in r['checks'] if x['returncode']==0}!=expected:
  raise ValueError('Incomplete coupled receiver state-machine gate')
 return r
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--idf',type=Path,default=os.environ.get('IDF_PATH'));a=p.parse_args()
 if not a.idf:p.error('--idf or IDF_PATH is required; see the public dependency guide')
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 env={**os.environ,'IDF_PATH':str(a.idf.resolve()),'PYTHONDONTWRITEBYTECODE':'1'}
 r=dict(status='RUNNING',hardware_access=False,precision='UNQUALIFIED',population='SYNTHETIC_STATE_AND_NATIVE_CONSUMER_SEQUENCES',checks=[],sources=sources())
 try:
  w12=APP.parent/'w12/tools'
  for name,script,extra in (
   ('source-ui-sequences',APP/'tools/state_machine_tests.py',[]),
   ('native-n30-coupling',APP/'tools/gm_successor_tests.py',[]),
   ('native-w12-recovery',w12/'ble_recovery_tests.py',[]),
   ('w12-production-holdover',w12/'g1_host_tests.py',['--selection','holdover'])):
   start=time.monotonic()
   with (out/(name+'.log')).open('w') as log:
    completed=subprocess.run([sys.executable,'-B',str(script),'--out',str(out/name),*extra],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
   r['checks'].append(dict(name=name,returncode=completed.returncode,seconds=round(time.monotonic()-start,3)))
   print(name+(': PASS' if completed.returncode==0 else ': FAIL'),flush=True)
   if completed.returncode:raise RuntimeError(name+' failed; preserve '+str(out))
  if r['sources']!=sources():raise RuntimeError('Sources changed during the gate; rerun against frozen sources')
  r['status']='PASS'
 except BaseException as error:
  r.update(status='FAIL',error=repr(error));raise
 finally:(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':main()
