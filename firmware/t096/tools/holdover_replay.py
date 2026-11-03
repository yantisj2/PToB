#!/usr/bin/env python3
"""Independent clock/rate replay and prospective host HOLD cohorts.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,math
from collections import defaultdict
from pathlib import Path
from holdover_analyze import AGES,project,trunc
HZ=16000000
def freeze(model,at,rate=None):
 nominal=(at-model[0])*125//2
 if nominal<0:raise ValueError('model was not available at entry')
 elapsed=nominal*1000000000//(1000000000+model[2])
 phase=trunc(model[3]*(1000000000-min(elapsed,1000000000)),1000000000)
 return [at,project(model,at),model[2]if rate is None else rate,phase,model[4]]
def fit(points,at,seconds):
 p=[x for x in points if x[0]<=at and x[2]<=at and at-x[0]<=seconds*HZ]
 if len(p)<(24 if seconds==16 else 32):return None
 span=(p[-1][0]-p[0][0])/HZ
 if span<seconds*.85:return None
 groups=defaultdict(list);base=p[0]
 for tick,ns,available,ch in p:
  x=(ns-base[1])/1e9;y=(tick-base[0])*62.5-(ns-base[1]);groups[ch].append((x,y))
 centered=[];df=0
 for values in groups.values():
  if len(values)<2:continue
  df+=len(values)-1;mx=sum(x for x,y in values)/len(values);my=sum(y for x,y in values)/len(values)
  centered.extend((x-mx,y-my)for x,y in values)
 if df<8:return None
 xx=sum(x*x for x,y in centered)
 if xx<=0:return None
 rate=sum(x*y for x,y in centered)/xx
 if abs(rate)>300000:return None
 square=sum((y-rate*x)**2 for x,y in centered)
 guard=math.ceil(max(62.5/span,3*math.sqrt(square/(df-1)/xx)))
 return rate,guard
def table(runs):
 result=[]
 for age in AGES:
  values=[r['prefix'].get(age)for r in runs][-128:];n=len(values)
  def metric(which):
   errors=[v[which]if v and not v['unknown']else math.inf for v in values]
   p99=sorted(errors)[math.ceil(n*.99)-1]if n else math.inf
   observed=[v[which]for v in values if v]
   return dict(p99_ns=p99 if math.isfinite(p99)else None,max_ns=max(observed)if observed else None,
    failed_or_unknown=sum(x>=1000 for x in errors),passed=n>=100 and sum(x>=1000 for x in errors)<=n//100 and p99<1000)
  result.append(dict(age_ms=age,outcomes=n,unknown=sum(not v or v['unknown']for v in values),candidate=metric('candidate'),baseline=metric('baseline')))
 return result
def replay(rows,starts=100,warmup_ms=32000,spacing_ms=2000,bias_ns=10543,entry_wait_ms=0):
 scopes=defaultdict(list)
 # Batch witness availability, rather than USB reply time, determines order.
 # When a real trace exists, latest-witness snapshots are diagnostics only.
 # The offline rate oracle may see all points but fit() enforces availability.
 traced={(r.get('source',r.get('payload',r).get('source')),r.get('boot',r.get('payload',r).get('boot')))for r in rows if r.get('payload',r).get('schema')==3}
 for row in rows:
  p=row.get('payload',row)
  source,boot=row.get('source',p.get('source')),row.get('boot',p.get('boot'))
  expanded=[]
  if p.get('schema')==3:
   for audit_seq,seq,tick,ns,available,ch,sampled,epoch,context in p['records']:
    expanded.append(dict(now_tick=available,raw_pair=[tick,ns,available,seq],
     pair=[tick,ns,available]if sampled else[0,0,0],channel=ch,epoch=epoch,resets=context))
  elif 'raw_pair'in p or 'map'in p:
   p=p.copy()
   if (source,boot)in traced:p.pop('raw_pair',None)
   expanded.append(p)
  for p in expanded:scopes[(source,boot,p['epoch'],p['resets'])].append(p)
 output=[]
 for scope,data in scopes.items():
  data.sort(key=lambda p:p['now_tick'])
  raw=[];points=[];point_ticks=set();last_seq=None;missing=0;origin=None;next_start=None;runs=[]
  active=[];models={};rate_checks=rate_errors=guard_errors=0;last_fit=0;first_raw=None;duplicates=0
  for p in data:
   pair=p.get('pair',[0,0,0]);ch=p.get('channel')
   if pair[0]and pair[0]not in point_ticks and ch is not None:
    point_ticks.add(pair[0]);points.append((*pair,ch))
  points.sort()
  for p in data:
   now=p['now_tick'];tick,ns,available,seq=p.get('raw_pair',[0,0,0,0])
   if available>now:raise ValueError('witness not yet available')
   if seq and first_raw is None:first_raw=tick
   gap=0
   if seq and last_seq is not None and seq<last_seq:raise ValueError('raw witness cursor regressed inside scope')
   if seq and (last_seq is None or seq>last_seq):
    gap=0 if last_seq is None else seq-last_seq-1;missing+=gap
    raw.append((tick,ns+bias_ns,seq));last_seq=seq
    for r in active:
     if tick<r['start']:continue
     r['missing']+=gap
     if tick-r['start']>121*HZ:continue
     r['witnesses']+=1
     for name in ('candidate','baseline'):r['maximum'][name]=max(r['maximum'][name],abs(project(r[name],tick)-(ns+bias_ns)))
     for age in AGES:
      if age not in r['prefix']and tick-r['start']>=age*16000:
       r['prefix'][age]=dict(**r['maximum'],unknown=bool(r['missing']or tick-r['start']>(age+1000)*16000),
        witnesses=r['witnesses'],overshoot_ticks=tick-r['start']-age*16000)
    active=[r for r in active if 120000 not in r['prefix']and tick-r['start']<=121*HZ]
   elif seq:duplicates+=1
   if 'rates'in p and p.get('fit_at',0)>last_fit:
    at=p['fit_at'];last_fit=at
    if points and at-points[0][0]>=32*HZ:
     for seconds,ri,gi in ((16,2,3),(32,4,5)):
      result=fit(points,at,seconds);advertised=p['rates'][gi]!=4294967295
      if result is not None and advertised:
       rate_checks+=1
       if abs(result[0]-p['rates'][ri])>2:rate_errors+=1
       if abs(result[1]-p['rates'][gi])>2:guard_errors+=1
      elif bool(result is not None)!=advertised:rate_errors+=1
   if 'map'not in p:continue
   if origin is None and p.get('available'):
    origin=now;next_start=origin+warmup_ms*16000
   if origin is not None:
    while len(runs)<starts and now>=next_start:
     ready=p.get('available')and p['map'][0]<=now and now-p['map'][0]<HZ/4 and abs(p['map'][3])<=250 and not p.get('test',[0])[0]
     deadline=(entry_wait_ms*16000 if entry_wait_ms else HZ/4)
     if not ready and entry_wait_ms and now-next_start<=deadline:break
     r=dict(start=now,scheduled=next_start,prefix={},witnesses=0,missing=0,maximum=dict(candidate=0,baseline=0));runs.append(r)
     if ready and now-next_start<=deadline:
      r['candidate']=freeze(p['map'],now);r['baseline']=freeze(p['map'],now,p['rates'][0]);active.append(r)
     else:r['entry_unknown']=True
     next_start+=spacing_ms*16000
   for lane,model in zip(p.get('trials',[]),p.get('trial_models',[])):
    ident=lane[0]
    if not ident:continue
    m=models.setdefault(ident,dict(model=model,snapshots=[]))
    if m['model']!=model:raise ValueError('frozen model changed')
    m['snapshots'].append(lane)
  checks=mismatches=complete_models=unreplayed_models=0
  for m in models.values():
   start,anchor,fast,chosen,phase=m['model']
   if first_raw is None or start<first_raw:unreplayed_models+=1;continue
   witnesses=[x for x in raw if start<=x[0]<=start+121*HZ]
   if any(b[2]-a[2]!=1 for a,b in zip(witnesses,witnesses[1:])):unreplayed_models+=1;continue
   errors=[[],[]]
   for tick,ns,seq in witnesses:
    for k,rate in enumerate((fast,chosen)):errors[k].append(abs(project([start,anchor,rate,phase,0],tick)-ns))
   compared=False
   for lane in m['snapshots']:
    count=lane[3]
    if not count or count>len(witnesses):continue
    checks+=1;compared=True
    if max(errors[0][:count])!=lane[4]or max(errors[1][:count])!=lane[5]:mismatches+=1
   complete_models+=compared
  ages=table(runs);frontier=None
  for age in ages:
   if not age['candidate']['passed']:break
   frontier=age['age_ms']
  output.append(dict(source=scope[0],boot=scope[1],epoch=scope[2],context=scope[3],
   accepted_witness_records=len(raw),missing_witness_records=missing,duplicate_snapshots=duplicates,
   planned_starts=starts,observed_starts=len(runs),supported_ms=frontier,ages=ages,
   onboard_replay=dict(checks=checks,mismatches=mismatches,models=complete_models,unknown_models=unreplayed_models),
   rate_replay=dict(checks=rate_checks,rate_disagreements=rate_errors,guard_disagreements=guard_errors),
   all_witness_replay_complete=missing==0 and mismatches==0 and checks>0))
 return dict(metric='PROSPECTIVE_HOST_FROZEN_PREFIX_P99_1000NS',correlated_overlapping_runs=True,
  entry_wait_ms=entry_wait_ms,physical_outage_test=False,continuous_bound=False,utc_accuracy_qualified=False,groups=output)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);p.add_argument('--starts',type=int,default=100);p.add_argument('--entry-wait-ms',type=int,choices=(0,1000),default=0);a=p.parse_args()
 result=replay([json.loads(x)for x in a.capture.read_text().splitlines()if x.strip()],starts=a.starts,entry_wait_ms=a.entry_wait_ms);s=json.dumps(result,indent=2)+'\n'
 if a.out:a.out.write_text(s)
 print(s,end='')
if __name__=='__main__':main()
