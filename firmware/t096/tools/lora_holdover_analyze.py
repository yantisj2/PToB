#!/usr/bin/env python3
"""Causal LoRa frozen-clock comparison against later authenticated pairs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Relative input consistency, not independent UTC or continuous output accuracy.
"""
import argparse,json,math
from collections import defaultdict
from pathlib import Path

AGES=(30,60,90,120)
def trunc(n,d):return n//d if n>=0 else -((-n)//d)
def project(anchor,tick,rate):
 dt=tick-anchor['tick']
 if dt<0 or abs(rate)>100000:raise ValueError('invalid frozen projection')
 return anchor['ns']+dt*125//2+trunc(dt*rate,16000000)
def disciplined(anchor,tick):
 if 'model'not in anchor:return project(anchor,tick,anchor['chosen'])
 at,ns,rate,phase=anchor['model'];dt=tick-at
 if dt<0:raise ValueError('future model used for earlier witness')
 return ns+dt*125//2+trunc(dt*rate,16000000)+trunc(min(dt,16000000)*phase,16000000)
def statistics(values):
 if not values:return dict(samples=0,rms_ns=None,mean_ns=None,p99_abs_ns=None,max_abs_ns=None)
 absolute=sorted(abs(x)for x in values)
 return dict(samples=len(values),rms_ns=math.sqrt(sum(x*x for x in values)/len(values)),
             mean_ns=sum(values)/len(values),p99_abs_ns=absolute[math.ceil(.99*len(values))-1],max_abs_ns=absolute[-1])
def analyze(rows):
 groups=defaultdict(list);seen={};missing=defaultdict(int);invalid_snapshots=0
 for row in rows:
  p=row.get('payload',row)
  if p.get('profile')!='RAM_30_120_300' or p.get('transmitter') or 'hold_rates'not in p:continue
  if not p.get('accepted'):continue
  # Fresh META can retain old event counters while clearing their capture
  # scope and permission. Such a snapshot is not a new physical event in the
  # new stream. Keep its denominator separately instead of inventing a cohort.
  if not p['capture_epoch']or('physical_until_us'in p and p['physical_until_us']<=p['at_us']):
   invalid_snapshots+=1;continue
  scope=tuple(p[k]for k in ('boot','emitter','emitter_boot','stream','source_epoch','capture_epoch'))
  identity=(p['accepted'],p['event_tick'],p['event_ns'])
  previous=seen.get(scope)
  if previous==identity:continue
  if previous:
   if any(b<=a for a,b in zip(previous,identity)):raise ValueError('accepted event regressed or mutated')
   missing[scope]+=p['accepted']-previous[0]-1
  fast,chosen,*_=p['hold_rates']
  point=dict(tick=p['event_tick'],ns=p['event_ns'],fast=fast,chosen=chosen,
             trained=p['trained'],selected=p['hold_selected'],rms=p['link_fit'][1])
  if 'hold_model'in p:point['model']=p['hold_model']
  groups[scope].append(point)
  seen[scope]=identity
 result=[]
 for scope,points in groups.items():
  windows={age:dict(baseline=[],hold=[],unknown=0,censored=0,actual=[])for age in AGES}
  for i,anchor in enumerate(points):
   if not anchor['trained']:continue
   for age,w in windows.items():
    witness=None
    for future in points[i+1:]:
     seconds=(future['tick']-anchor['tick'])/16000000
     if seconds>age+2.51:break
     if seconds>=age-2.51:witness=future;break
    if witness is None:
     field='unknown'if points[-1]['tick']-anchor['tick']>=(age+2.51)*16000000 else'censored'
     w[field]+=1;continue
    w['baseline'].append(project(anchor,witness['tick'],anchor['fast'])-witness['ns'])
    w['hold'].append(disciplined(anchor,witness['tick'])-witness['ns'])
    w['actual'].append((witness['tick']-anchor['tick'])/16000000)
  result.append(dict(scope=list(scope),accepted_events_observed=len(points),unobserved_accepted_events=missing[scope],
                     trained_anchors=sum(p['trained']for p in points),selected_anchors=sum(p['trained']and p['selected']for p in points),
                     overlapping_fit=statistics([p['rms']for p in points if p['trained']]),
                     frozen_comparisons=[dict(age_s=age,baseline=statistics(w['baseline']),hold=statistics(w['hold']),
                       unknown=w['unknown'],censored=w['censored'],actual_age_range_s=[min(w['actual']),max(w['actual'])]if w['actual']else None)
                       for age,w in windows.items()]))
 return dict(metric='CAUSAL_FROZEN_LORA_PAIR_ERROR',rate_policy='LORA_SPARSE_CONSENSUS_180_300',
             witness_rule='first future pair within target age +/-2.51s; all errors retained',
             independent_utc_accuracy=False,continuous_output_bound=False,overlapping_correlated_anchors=True,
             invalid_event_snapshots=invalid_snapshots,groups=result)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
 result=analyze([json.loads(x)for x in a.capture.read_text().splitlines()if x.strip()]);text=json.dumps(result,indent=2)+'\n'
 if a.out:a.out.write_text(text)
 print(text,end='')
if __name__=='__main__':main()
