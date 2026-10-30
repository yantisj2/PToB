#!/usr/bin/env python3
"""Replay BLE-only frozen shadow clocks and their sampled P99 hold seconds.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,collections,hashlib,json,math,re
from pathlib import Path
HZ=20000000
AGES=list(range(250,4001,250))+[6000,8000,12000,16000,24000,32000,48000,64000,96000,120000]
def fields(s):
    p=[x.split('=',1) for x in s.split()];assert len({x[0] for x in p})==len(p);return dict(p)
def rounded(x):return math.floor(x+.5) if x>=0 else math.ceil(x-.5)
def summary(bins):
    result=dict(ready=False,duration_ms=0,lower_bound=False,n=0,bad=0,unknown=0)
    bounded=False
    if bins:result.update(n=len(bins[0]),bad=sum(x!=1 for x in bins[0]),unknown=bins[0].count(3))
    for age,b in zip(AGES,bins):
        if len(b)-b.count(3)<100:break
        if sum(x!=1 for x in b)*100>len(b):
            if b.count(2)*100>len(b):bounded=True;result['ready']=True
            break
        result.update(ready=True,duration_ms=age,n=len(b),bad=sum(x!=1 for x in b),unknown=b.count(3))
    result['lower_bound']=result['ready'] and not bounded
    return result
def analyze(raw):
    bins=[collections.deque(maxlen=128) for _ in AGES];slots=[None]*48;states={};finished={};statuses=[];orphans=[];pairs={}
    complete=0;resets=0;replay_difference=0;population_known=False
    counts=[0]*len(AGES)
    def add(r,i,outcome):
        bins[i].append(outcome);counts[i]+=1;r['bits']|=outcome<<(2*i)
    def finish(r):
        while r['next']<len(AGES) and AGES[r['next']]<=r['probe']:
            add(r,r['next'],2 if r['peak']>=1000 else 3);r['next']+=1
        slots[r['slot']]=None;r['ended']=True
    for line in raw.decode('ascii',errors='strict').splitlines():
        m=re.search(r'(?:^|record=)(W12_HORIZON_(?:START|END|STATUS|RESET)|W12_HOLDOVER_PAIR) (.*)',line)
        if not m:continue
        kind=m[1];v=fields(m[2])
        if kind=='W12_HORIZON_RESET':
            bins=[collections.deque(maxlen=128) for _ in AGES];slots=[None]*48;states={};complete=0;resets+=1;population_known=True;counts=[0]*len(AGES);continue
        if kind=='W12_HORIZON_START':
            assert v['temperature']=='0' and v['production']=='0'
            key=(int(v['boot']),int(v['trial']));assert key not in states and None in slots
            if key[1]==1:population_known=True
            r=dict(start=int(v['start20']),last=int(v['start20']),epoch=int(v['epoch_ns']),phase=int(v['phase_ns']),rate=float(v['rate']),probe=int(v['probe_ms']),next=0,bits=0,peak=0,gaps=0,slot=slots.index(None),ended=False)
            states[key]=r;slots[r['slot']]=r
        elif kind=='W12_HOLDOVER_PAIR':
            v={k:int(x) for k,x in v.items()};key=(v['boot'],v['seq'])
            if key in pairs:assert pairs[key]==v;continue
            pairs[key]=v;local=v['local20'];assert v['available20']>=local
            for r in list(slots):
                if r is None or local<=r['start']:continue
                gap=local-r['last'];assert gap>0;r['last']=local;r['gaps']+=gap>HZ//4
                dt=local-r['start'];t=dt/HZ;phase=r['phase']
                slew=math.copysign(min(abs(phase),t*1000),phase)
                prediction=r['epoch']+dt*50+rounded(t*r['rate']+slew)
                r['peak']=max(r['peak'],min(2**32-1,abs(prediction-v['reference_ns'])))
                while r['next']<len(AGES) and AGES[r['next']]<=r['probe'] and dt>=AGES[r['next']]*20000:
                    over=dt-AGES[r['next']]*20000;add(r,r['next'],2 if r['peak']>=1000 else 1 if over<=HZ//4 else 3);r['next']+=1
                if dt>=r['probe']*20000:finish(r)
        elif kind=='W12_HORIZON_END':
            key=(int(v['boot']),int(v['trial']));r=states.get(key)
            if r is None:orphans.append(key);continue
            if not r['ended']:finish(r)
            assert r['last']==int(v['last20']) and r['bits']==int(v['outcomes']) and r['gaps']==int(v['gaps']),'Shadow duration/outcome replay mismatch'
            difference=abs(r['peak']-int(v['peak_ns']));replay_difference=max(replay_difference,difference);assert difference<=1
            finished[key]=dict(trial=key[1],probe_ms=r['probe'],peak_ns=r['peak'],gaps=r['gaps'],outcomes=r['bits']);complete+=1
        else:
            assert v['policy']=='SAMPLED_PREFIX_Q99_1US_V1' and v['temperature']=='0' and v['production']=='0'
            # Mixed-duration lanes must evict pre-capture evidence separately
            # at each age. Short completions cannot certify a long-age ring.
            if not population_known:
                known=True
                for i,b in enumerate(bins):
                    if counts[i]<128:known=False;break
                    if len(b)-b.count(3)<100 or sum(x!=1 for x in b)*100>len(b):break
                if not known:continue
            expected=summary(bins)
            for k in ('ready','duration_ms','lower_bound','n','bad','unknown'):assert int(v[k])==expected[k],(k,v[k],expected)
            statuses.append({**expected,'at_us':int(v['at_us']),'probe_ms':int(v['probe_ms'])})
    return dict(status='SAMPLED_BLE_SHADOW_HOLD_SECONDS',sha256=hashlib.sha256(raw).hexdigest(),replayed_runs=list(finished.values()),orphan_ends=orphans,resets=resets,verified_statuses=statuses,
                latest=statuses[-1] if statuses else None,replay_max_difference_ns=replay_difference,complete=bool(statuses),independent_runs=False,continuous_error_bound=False,independent_UTC_qualified=False)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args();r=analyze(a.capture.read_bytes());s=json.dumps(r,indent=2)+'\n'
    if a.out:a.out.write_text(s)
    print(s,end='')
