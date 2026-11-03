#!/usr/bin/env python3
"""Actual48B drift-state C, independent3x3 matrix oracle and declared noise.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
PPS never seeds or trains this candidate. This is offline model verification.
"""
import argparse,hashlib,json,math,os,struct,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def f32(x):return struct.unpack('f',struct.pack('f',x))[0]
class DriftOracle:
 """Independent dense matrix operations; the C implementation is scalarized."""
 def __init__(self):
  self.x=[0.,0.,0.];self.P=[[4200.**2,0.,0.],[0.,1e10,0.],[0.,0.,100.]];self.point=None
 def observe(self,tick,ns):
  if self.point is not None:
   old_tick,old_ns=self.point;t=(tick-old_tick)/16000000.;z=(ns-old_ns)-(tick-old_tick)*125//2
   F=[[1.,t,t*t/2],[0.,1.,t],[0.,0.,1.]];qr=.01;qj=1e-8;R=4200.**2
   Q=[[qr*t**3/3+qj*t**5/20,qr*t*t/2+qj*t**4/8,qj*t**3/6],
      [qr*t*t/2+qj*t**4/8,qr*t+qj*t**3/3,qj*t*t/2],[qj*t**3/6,qj*t*t/2,qj*t]]
   prediction=[sum(F[j][k]*self.x[k]for k in range(3))for j in range(3)]
   prior=[[sum(F[j][k]*self.P[k][l]*F[h][l]for k in range(3)for l in range(3))+Q[j][h]for h in range(3)]for j in range(3)]
   K=[prior[j][0]/(prior[0][0]+R)for j in range(3)];innovation=z-prediction[0]
   self.x=[prediction[j]+K[j]*innovation for j in range(3)];self.x[0]-=z
   A=[[float(j==k)-K[j]*float(k==0)for k in range(3)]for j in range(3)]
   self.P=[[f32(sum(A[j][k]*prior[k][l]*A[h][l]for k in range(3)for l in range(3))+K[j]*K[h]*R)for h in range(3)]for j in range(3)]
  self.point=(tick,ns)
  return self.x+[self.P[0][0],self.P[0][1],self.P[0][2],self.P[1][1],self.P[1][2],self.P[2][2]]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 binary=out/'vectors';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 command=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),str(APP/'tests/lora_clock_drift_test.c'),'-o',str(binary)];subprocess.run(command,check=True)
 checks=0
 for rate in(-80000,-123,0,123,80000):
  for ramp in(-.1,0.,.1):
   points=[]
   for i in range(100):
    sec=i*30;noise=round(2100*math.sin(i*.7));ns=1793724000000000000+sec*1000000000+round(sec*rate+.5*sec*sec*ramp)+noise
    points.append((8000000000000+sec*16000000,ns))
   run=subprocess.run([str(binary)],input=''.join(f'{t} {n}\n'for t,n in points),capture_output=True,text=True);(out/'last-vectors.log').write_text(run.stdout+run.stderr);run.check_returncode()
   states=[list(map(float,line.split()))for line in run.stdout.splitlines()];assert len(states)==100
   x=[0.,0.,0.];P=[[4200.**2,0.,0.],[0.,1e10,0.],[0.,0.,100.]];R=4200.**2
   F=[[1.,30.,450.],[0.,1.,30.],[0.,0.,1.]]
   Q=[[.01*30**3/3+1e-8*30**5/20,.01*30**2/2+1e-8*30**4/8,1e-8*30**3/6],
      [.01*30**2/2+1e-8*30**4/8,.01*30+1e-8*30**3/3,1e-8*30**2/2],
      [1e-8*30**3/6,1e-8*30**2/2,1e-8*30]]
   for i,state in enumerate(states):
    if i:
     z=(points[i][1]-points[i-1][1])-30000000000
     prediction=[sum(F[j][k]*x[k]for k in range(3))for j in range(3)]
     prior=[[sum(F[j][k]*P[k][l]*F[h][l]for k in range(3)for l in range(3))+Q[j][h]for h in range(3)]for j in range(3)]
     K=[prior[j][0]/(prior[0][0]+R)for j in range(3)];innovation=z-prediction[0]
     x=[prediction[j]+K[j]*innovation for j in range(3)];x[0]-=z
     A=[[float(j==k)-K[j]*float(k==0)for k in range(3)]for j in range(3)]
     P=[[f32(sum(A[j][k]*prior[k][l]*A[h][l]for k in range(3)for l in range(3))+K[j]*K[h]*R)for h in range(3)]for j in range(3)]
    expected=x+[P[0][0],P[0][1],P[0][2],P[1][1],P[1][2],P[2][2]]
    for index,(actual,want)in enumerate(zip(state,expected)):
     tolerance=(.01 if index==0 else 1e-5 if index==1 else 1e-7 if index==2 else max(1e-5,abs(want)*1e-5))
     assert abs(actual-want)<=tolerance,(rate,ramp,i,index,actual,want)
    checks+=1
 subprocess.run([sys.executable,str(APP/'tools/lora_servo_noise_tests.py'),'--binary',str(binary),'--drift-state','--out',str(out/'noise-model.json')],check=True)
 sources=[APP/'include/lora_clock_drift.h',APP/'tests/lora_clock_drift_test.c',APP/'tools/lora_servo_noise_tests.py',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS_OFFLINE_DRIFT_CANDIDATE_NOT_DEPLOYED',hardware=False,state_bytes=48,matrix_states=checks,command=command,sources={str(p.relative_to(APP)):hashlib.sha256(p.read_bytes()).hexdigest()for p in sources}),indent=2)+'\n')
 print('PASS drift candidate:',checks,'independent matrix states; double state, float covariance, both signs and unknown priors')
if __name__=='__main__':main()
