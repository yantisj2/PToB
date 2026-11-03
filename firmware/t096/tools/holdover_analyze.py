#!/usr/bin/env python3
"""Replay sampled HOLD prefix outcomes from boot-bound JSONL console captures.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,math
from collections import defaultdict
from pathlib import Path
AGES=(2000,12000,32000,64000,120000)
def trunc(n,d):return n//d if n>=0 else -((-n)//d)
def project(model,tick):
 anchor,value,rate,phase,_=model
 if tick<anchor or abs(rate)>300000:raise ValueError('invalid projection')
 nominal=(tick-anchor)*125//2
 elapsed=nominal*1000000000//(1000000000+rate)
 return value+elapsed+trunc(min(elapsed,1000000000)*phase,1000000000)
def analyze(rows):
 runs={};latest_reset={};notes=[];gaps=0
 for row in rows:
  p=row.get('payload',row);op=row.get('op','')
  if op=='line' or row.get('kind')=='line':
   line=row.get('line','')
   if not line.startswith('#R2_TIMING '):continue
   p=json.loads(line[len('#R2_TIMING '):]);op='timing'
  source=row.get('source',p.get('source','unknown'));boot=row.get('boot',p.get('boot','unknown'));base=(source,boot)
  if p.get('policy')=='BLE_RATE_ONLY_T096_V1':
   reset=p['resets'];latest_reset[base]=reset
   for lane in p['trials']:
    ident,probe,active,n,baseline,candidate,cross_b,cross_c,gap,age=lane
    if not ident:continue
    key=base+(ident,);r=runs.setdefault(key,dict(reset=reset,probe=probe,prefix={},gaps=0,ended=False))
    if r['reset']!=reset or r['probe']!=probe:raise ValueError('trial identity reused across contexts')
    r['gaps']=max(r['gaps'],gap)
    for target in AGES:
     if target<=probe and age>=target and n and target not in r['prefix']:
      r['prefix'][target]=dict(candidate=candidate,baseline=baseline,overshoot_ms=age-target,witnesses=n)
  elif op=='timing' or 'kind'in p and 'detail'in p and 'peer'in p:
   notes.append((base,p))
 # Enum values are append-only: START34, END35, RESET36. Match source/boot/id;
 # missing START or complete endpoint evidence remains explicitly unknown.
 resets=defaultdict(list)
 for base,p in notes:
  if p['kind']==36:resets[base].append((p['ms'],p['detail']))
 for base,p in notes:
  if p['kind']!=34:continue
  key=base+(p['detail'],)
  reset=max((x for x in resets[base]if x[0]<=p['ms']),default=(0,latest_reset.get(base,0)))[1]
  r=runs.setdefault(key,dict(reset=reset,probe=p['b'],prefix={},gaps=0,ended=False))
  r['start_seen']=True
 for base,p in notes:
  if p['kind']!=35:continue
  key=base+(p['detail'],)
  if key not in runs:continue
  r=runs[key];r['ended']=True;r['result']=p['result']
  if p['result']==0:
   r['prefix'][r['probe']]=dict(candidate=p['a'],baseline=p['b'],overshoot_ms=None,witnesses=None)
 groups=defaultdict(list)
 for (source,boot,ident),r in runs.items():groups[(source,boot,r['reset'])].append((ident,r))
 output=[]
 for scope,group in sorted(groups.items()):
  result=dict(source=scope[0],boot=scope[1],context=scope[2],ages=[],supported_ms=None)
  contiguous=True
  for age in AGES:
   eligible=[(ident,r)for ident,r in sorted(group)if r['probe']>=age][-128:]
   values=[r['prefix'].get(age)for _,r in eligible];n=len(values)
   errors=[v['candidate']if v is not None else math.inf for v in values]
   failed=sum(e>=1000 for e in errors);unknown=sum(v is None for v in values)
   p99=sorted(errors)[math.ceil(n*.99)-1]if n else math.inf
   passed=n>=100 and failed<=n//100 and p99<1000
   if contiguous and passed:result['supported_ms']=age
   else:contiguous=False
   observed=[e for e in errors if math.isfinite(e)]
   result['ages'].append(dict(age_ms=age,outcomes=n,unknown=unknown,failed_or_unknown=failed,
    p99_ns=p99 if math.isfinite(p99)else None,max_ns=max(observed)if observed else None,passed=passed))
  result['witness_gaps']=sum(r['gaps']for _,r in group);output.append(result)
 return dict(metric='EMPIRICAL_SAMPLED_PREFIX_P99_1000NS',minimum_outcomes=100,window=128,
  continuous_bound=False,utc_accuracy_qualified=False,groups=output)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 rows=[json.loads(x)for x in a.capture.read_text().splitlines()if x.strip()];r=analyze(rows);s=json.dumps(r,indent=2)+'\n'
 if a.out:a.out.write_text(s)
 print(s,end='')
if __name__=='__main__':main()
