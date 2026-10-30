#!/usr/bin/env python3
"""Independent numerical replay of sampled run-prefix hold duration.

Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Raw mode is a private extraction tool. Public mode reads only selected numeric
records exported with common coordinate origins and pseudonymous boot labels.
"""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import re

HZ = 20_000_000
AGES = list(range(250, 4001, 250)) + [6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000, 96000, 120000]
KINDS = ('START', 'END', 'STATUS', 'RESET', 'PAIR')

def rounded(x):
    return math.floor(x + .5) if x >= 0 else math.ceil(x - .5)

def quantile(values, p):
    if not values:
        return None
    return sorted(values)[math.ceil(len(values) * p / 100) - 1]

def stats(b):
    known = [x['peak_ns'] for x in b if x['witness']]
    n = len(b)
    unknown = sum(x['outcome'] == 3 for x in b)
    fail = sum(x['outcome'] == 2 for x in b)
    return dict(n=n, scored=n-unknown, bad=unknown+fail, fail=fail, unknown=unknown,
                p95_ns=quantile(known, 95), p99_ns=quantile(known, 99), max_ns=max(known, default=None),
                numeric_prefixes=len(known), numeric_censored=len(known)!=n,
                quantile_scope='actual witnessed numeric prefixes only' if len(known)!=n else 'all prefixes',ready=n-unknown >= 100,
                passes=n-unknown >= 100 and 100*(unknown+fail) <= n)

def view(bins):
    r=dict(ready=False, duration_ms=0, lower_bound=False, n=0, bad=0, unknown=0)
    if bins[0]:
        s=stats(bins[0]);r.update({k:s[k] for k in ('n','bad','unknown')})
    bounded=False
    for age,b in zip(AGES,bins):
        s=stats(b)
        if not s['ready']:
            break
        if not s['passes']:
            if s['fail']*100>s['n']:
                bounded=True;r['ready']=True
            break
        r.update(ready=True,duration_ms=age,**{k:s[k] for k in ('n','bad','unknown')})
    r['lower_bound']=r['ready'] and not bounded
    return r

def raw_records(raw):
    records=[]
    pattern=re.compile(r'(?:^|record=)(W12_HORIZON_(?:START|END|STATUS|RESET)|W12_HOLDOVER_PAIR) (.*)')
    for line in raw.decode('ascii').splitlines():
        m=pattern.search(line)
        if not m:
            continue
        items=[p.split('=',1) for p in m[2].split()]
        assert len(dict(items))==len(items)
        v=dict(items);kind='PAIR' if m[1]=='W12_HOLDOVER_PAIR' else m[1][12:]
        # Selected numeric whitelist: never export identity/enrollment/console text.
        keys={'PAIR':['seq','local20','reference_ns','available20','channel','generation'],
              'START':['trial','start20','probe_ms','epoch_ns','phase_ns','rate'],
              'END':['trial','start20','probe_ms','last20','outcomes','peak_ns','gaps'],
              'STATUS':['at_us','duration_ms','probe_ms','ready','lower_bound','n','bad','unknown','started','completed','aborted','gaps','last20'],
              'RESET':[]}[kind]
        assert kind=='PAIR' or v.get('temperature')=='0' and v.get('production')=='0'
        d={'kind':kind,**{k:float(v[k]) if k=='rate' else int(v[k]) for k in keys}}
        records.append(d)
    return records

def normalized(records):
    pair=next(r for r in records if r['kind']=='PAIR')
    tick_origin,reference_origin=pair['local20'],pair['reference_ns']
    result=[]
    for r in records:
        r=r.copy()
        for k in ('local20','available20','start20','last20'):
            if k in r:r[k]-=tick_origin
        for k in ('epoch_ns','reference_ns'):
            if k in r:r[k]-=reference_origin
        result.append(r)
    return result

def analyze(records, cold=False):
    bins=[collections.deque(maxlen=128) for _ in AGES]
    active={};runs={};prefixes=[];statuses=[];orphans=[];snapshots={};pairs={}
    counts=[0]*len(AGES);first_start=None;last_seq=None;missing_pairs=0;resets=0;maxdiff=0
    reset_cold=cold;first_trial=None;ends=0;maxactive=0;gaps=[]
    # The probe only increases within a source scope. An observed initial long
    # probe (or earlier STATUS probe) proves that greater ages were still empty.
    initial_probe=next((v['probe_ms'] for v in records if v['kind']=='STATUS' or v['kind']=='START' and v['probe_ms']>2000),120000)
    def add(r,i,o,witness):
        row=dict(trial=r['trial'],start20=r['start20'],probe_ms=r['probe_ms'],age_ms=AGES[i],
                 witness20=r['last20'],overshoot_ms=(r['last20']-r['start20'])/20000-AGES[i] if witness else None,
                 peak_ns=r['peak'],outcome=o,witness=witness,gaps=r['gaps'],max_gap_ms=r['maxgap']/20000)
        bins[i].append(row);counts[i]+=1;prefixes.append(row);r['bits']|=o<<(2*i)
    def finish(r):
        while r['next']<len(AGES) and AGES[r['next']]<=r['probe_ms']:
            add(r,r['next'],2 if r['peak']>=1000 else 3,False);r['next']+=1
        active.pop(r['trial'],None);r['ended']=True
    for v in records:
        kind=v['kind']
        if kind=='RESET':
            # Firmware emits END records for active runs before RESET.
            assert not active
            bins=[collections.deque(maxlen=128) for _ in AGES];counts=[0]*len(AGES);snapshots={};reset_cold=True;resets+=1
        elif kind=='START':
            trial=v['trial'];assert trial not in runs
            if runs:assert trial==max(runs)+1, ('missing START record',trial,max(runs))
            if first_trial is None:first_trial=trial
            if trial==1:reset_cold=True
            r={**v,'last20':v['start20'],'peak':0,'gaps':0,'maxgap':0,'next':0,'bits':0,'ended':False}
            runs[trial]=active[trial]=r;maxactive=max(maxactive,len(active));assert maxactive<=48
            if first_start is None:first_start=v['start20']
        elif kind=='PAIR':
            seq=v['seq']
            if seq in pairs:
                assert pairs[seq]==v
                continue
            if last_seq is not None:
                assert seq>last_seq
                missing_pairs+=seq-last_seq-1
            pairs[seq]=v;last_seq=seq
            local=v['local20'];assert v['available20']>=local
            for r in list(active.values()):
                if local<=r['start20']:
                    continue
                gap=local-r['last20'];assert gap>0
                r['last20']=local;r['gaps']+=gap>HZ//4;r['maxgap']=max(r['maxgap'],gap)
                if gap>HZ//4:gaps.append(gap/HZ)
                dt=local-r['start20'];t=dt/HZ;phase=r['phase_ns']
                slew=math.copysign(min(abs(phase),t*1000),phase)
                predicted=r['epoch_ns']+dt*50+rounded(t*r['rate']+slew)
                r['peak']=max(r['peak'],min(2**32-1,abs(predicted-v['reference_ns'])))
                while r['next']<len(AGES) and AGES[r['next']]<=r['probe_ms'] and dt>=AGES[r['next']]*20000:
                    add(r,r['next'],2 if r['peak']>=1000 else 1,True);r['next']+=1
                if dt>=r['probe_ms']*20000:finish(r)
            snapshots[local]=(view(bins),list(counts),[stats(b) for b in bins],reset_cold)
        elif kind=='END':
            r=runs.get(v['trial'])
            if r is None:
                orphans.append(v['trial']);continue
            assert r['start20']==v['start20'] and r['probe_ms']==v['probe_ms']
            if not r['ended']:finish(r)
            assert r['last20']==v['last20'] and r['bits']==v['outcomes'] and r['gaps']==v['gaps'], ('outcome mismatch',v,r)
            diff=abs(r['peak']-v['peak_ns']);assert diff<=1,('numeric mismatch',v,r)
            maxdiff=max(maxdiff,diff);ends+=1
        elif kind=='STATUS':
            snap=snapshots.get(v['last20'])
            if snap is None:
                statuses.append({**v,'verified':False,'reason':'no exact accepted-pair snapshot'})
                continue
            expected,scored,age_stats,is_cold=snap
            # An age ring is known only after 128 captured inserts at that age,
            # or a known empty cold/reset population. Never use 2 s counts for 12 s.
            known=[is_cold or age>initial_probe or n>=128 for age,n in zip(AGES,scored)]
            need=next((i for i,a in enumerate(AGES) if a>expected['duration_ms']),len(AGES)-1)
            population_known=all(known[:need+1])
            if population_known:
                assert all(int(v[k])==expected[k] for k in expected), ('status mismatch',v,expected)
            statuses.append({**v,'verified':population_known,'age_population_known':known,'age_stats':age_stats,
                             'replayed':expected,'at_since_first_start_s':(v['last20']-first_start)/HZ if first_start is not None else None})
    age_all=[]
    for age in AGES:
        rows=[r for r in prefixes if r['age_ms']==age]
        age_all.append({'age_ms':age,**stats(rows), 'max_overshoot_ms':max((r['overshoot_ms'] for r in rows if r['witness']),default=None)})
    first12=next((s for s in statuses if s.get('verified') and s['ready'] and s['duration_ms']>=12000),None)
    return dict(status='SAMPLED_PREFIX_MAXIMUM_DURATION',first_trial=first_trial,cold_population=reset_cold,
                first_start20=first_start,pairs=len(pairs),missing_pair_records=missing_pairs,starts=len(runs),
                replayed_ends=ends,orphan_ends=orphans,unfinished_trials=list(active),resets=resets,
                max_numeric_difference_ns=maxdiff,max_active_runs=maxactive,all_age_results=age_all,
                final_replayed_horizon=view(bins),
                initial_probe_ms=initial_probe,
                final_age_results=[{'age_ms':a,**stats(b),'population_known':reset_cold or a>initial_probe or counts[i]>=128} for i,(a,b) in enumerate(zip(AGES,bins))],
                first_verified_12s=first12,statuses=statuses,prefixes=prefixes,
                independent_runs=False,continuous_error_bound=False,independent_UTC_qualified=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--raw',action='store_true');p.add_argument('--cold',action='store_true');p.add_argument('--export',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    raw=a.input.read_bytes();records=raw_records(raw) if a.raw else [json.loads(x) for x in raw.decode().splitlines()]
    if a.export:
        a.export.write_text(''.join(json.dumps(x,separators=(',',':'))+'\n' for x in normalized(records)))
    result=analyze(records,a.cold);result['input_sha256']=hashlib.sha256(raw).hexdigest()
    if a.out:a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('prefixes','statuses','first_verified_12s')},indent=2))
if __name__=='__main__':main()
