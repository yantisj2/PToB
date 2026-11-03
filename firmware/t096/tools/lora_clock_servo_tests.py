#!/usr/bin/env python3
"""Offline recursive candidate: actual C, independent matrix oracle and noise.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
The candidate is not the delivered clock in firmware0.15.275.
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 binary=out/'vectors';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 command=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),str(APP/'tests/lora_clock_servo_test.c'),'-o',str(binary)]
 subprocess.run(command,check=True)
 checks=0
 for rate in(-100000,-99999,-123,0,123,99999,100000):
  points=[(8000000000000+i*480000000,1793724000000000000+i*30000000000+i*30*rate)for i in range(80)]
  run=subprocess.run([str(binary)],input=''.join(f'{t} {n}\n'for t,n in points),capture_output=True,text=True);run.check_returncode()
  states=[list(map(float,line.split()))for line in run.stdout.splitlines()];assert len(states)==80
  phase=frequency=0.;P=[[4200.**2,0.],[0.,1e10]]
  for index,state in enumerate(states):
   if index:
    # Independent two-by-two matrix prediction/Joseph update.
    F=[[1.,30.],[0.,1.]];Q=[[.01*30**3/3,.01*30**2/2],[.01*30**2/2,.01*30]]
    prior=[[sum(F[i][k]*P[k][l]*F[j][l]for k in range(2)for l in range(2))+Q[i][j]for j in range(2)]for i in range(2)]
    innovation=30*rate-phase-30*frequency;K=[prior[i][0]/(prior[0][0]+4200.**2)for i in range(2)]
    phase=phase+30*frequency+K[0]*innovation-30*rate;frequency+=K[1]*innovation
    frequency=max(-100000.,min(100000.,frequency))
    A=[[float(i==j)-K[i]*float(j==0)for j in range(2)]for i in range(2)]
    P=[[sum(A[i][k]*prior[k][l]*A[j][l]for k in range(2)for l in range(2))+K[i]*K[j]*4200.**2 for j in range(2)]for i in range(2)]
   expected=[phase,frequency,P[0][0],P[0][1],P[1][1]]
   for actual,want in zip(state,expected):assert abs(actual-want)<=max(1e-5,abs(want)*1e-8),(rate,index,state,expected)
   assert state[2]*state[4]-state[3]**2>=-1e-3
   checks+=1
 subprocess.run([sys.executable,str(APP/'tools/lora_servo_noise_tests.py'),'--binary',str(binary),'--out',str(out/'noise-model.json')],check=True)
 sources=[APP/'include/lora_clock_servo.h',APP/'tests/lora_clock_servo_test.c',APP/'tools/lora_servo_noise_tests.py',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS_OFFLINE_CANDIDATE_NOT_DEPLOYED',hardware=False,matrix_states=checks,command=command,sources={str(p.relative_to(APP)):hashlib.sha256(p.read_bytes()).hexdigest()for p in sources}),indent=2)+'\n')
 print('PASS recursive candidate:',checks,'matrix states, both signs, full100ppm domain and centered integer UTC')
if __name__=='__main__':main()
