#!/usr/bin/env python3
"""Verify selected production HOLD output against BLE and replay frozen inputs.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,math,re
from pathlib import Path
from shadow_holdover_analyze import tails,UNKNOWN
HZ=20000000

def fields(text):
    pairs=[p.split('=',1) for p in text.split()];assert len({p[0] for p in pairs})==len(pairs)
    return dict(pairs)

def integers(row):return {k:int(v) for k,v in row.items()}
def rounded(n):return math.floor(n+.5) if n>=0 else math.ceil(n-.5)

def predict(begin,sensors,at):
    start=int(begin['start20']);assert at>=start
    elapsed=at-int(begin['clock_tick20']);assert elapsed>=0
    seconds=elapsed/HZ;phase=int(begin['phase_ns']);rate=float(begin['rate'])
    slew=math.copysign(min(abs(phase),seconds*(1000 if abs(phase)<2000 else min(50000,4*abs(phase)))),phase)
    base=int(begin['epoch_ns'])+elapsed*50+rounded(seconds*rate+slew)
    if float(begin['beta'])==0:return base,base
    lagged=float(begin['lagged']);initial=lagged;measured=float(begin['measured']);beta=float(begin['beta'])
    point=start;area=0
    for sensor in sensors:
        tick=sensor['tick20']
        if tick<=start:continue
        if tick>at:break
        dt=(tick-point)/HZ;decay=math.exp(-dt/8)
        area+=(measured-initial)*dt+(lagged-measured)*8*(1-decay)
        lagged=measured+(lagged-measured)*decay;measured=sensor['temperature_mc']/1000;point=tick
    dt=(at-point)/HZ
    area+=(measured-initial)*dt+(lagged-measured)*8*(1-math.exp(-dt/8))
    return base+rounded(beta*area),base

def analyze(raw):
    begins={};ends={};sensors={};rows={};statuses={};resumes={};seen={};rejects=[]
    for line in raw.decode('ascii',errors='strict').splitlines():
        m=re.search(r'(?:^|record=)(W12_HOLDOVER(?:_EVENT|_SENSOR|_STATUS|_RESUME)?) (.*)',line)
        if not m:continue
        kind=m[1];r=fields(m[2]);assert r['production']=='1' and r['model']=='1'
        if kind=='W12_HOLDOVER_EVENT':
            if r['event']=='BEGIN':
                start=int(r['start20']);assert start not in begins;begins[start]=r
                if r.get('temperature_shadow_only')=='1':assert float(r['beta'])==0 and int(r['beta_mppb_c'])==0,'Temperature escaped shadow into production'
                assert int(r['until20'])-start==int(r['duration_ms'])*20000 and 1000<=int(r['duration_ms'])<=120000
            elif r['event']=='END':ends[int(r['start20'])]=r
            else:rejects.append(r)
        elif kind=='W12_HOLDOVER_SENSOR':
            r=integers(r);sensors.setdefault(r['test'],{})[r['tick20']]=r
        elif kind=='W12_HOLDOVER_STATUS':
            r=integers(r)
            if r['test_active']:
                assert r['selected']==1 and r['following']==0
                assert r['output_ns']>0 and r['output_ns']//1000==r['delivered_us']
                statuses.setdefault(r['start20'],[]).append(r)
        elif kind=='W12_HOLDOVER_RESUME':
            r=integers(r);resumes.setdefault(r['test'],[]).append(r)
        else:
            r=integers(r);assert r['output_ns']==r['predicted_ns']+r['utc_offset_ns']+r['receiver_ns']+r['output_correction_ns']
            assert r['predicted_ns']-r['reference_ns']==r['error_ns']
            assert r['baseline_ns']-r['reference_ns']==r['baseline_error_ns']
            key=(r['boot'],r['seq'])
            if key in seen:assert seen[key]==r;continue
            seen[key]=r;rows.setdefault(r['start20'],[]).append(r)
    result=[]
    for start,b in begins.items():
        test=int(b['test']);records=sorted(rows.get(start,[]),key=lambda r:r['seq']);ss=sorted(sensors.get(test,{}).values(),key=lambda s:s['tick20'])
        end=ends.get(start);snapshots=statuses.get(start,[]);replay_max=0
        for r in records:
            assert start<r['local20']<int(b['until20'])
            predicted,base=predict(b,ss,r['local20'])
            replay_max=max(replay_max,abs(predicted-r['predicted_ns']),abs(base-r['baseline_ns']))
            assert abs(predicted-r['predicted_ns'])<=1 and base==r['baseline_ns'],'Production projector replay mismatch'
        for r in snapshots:
            predicted,_=predict(b,ss,r['sample20'])
            expected=predicted+int(b['utc_offset_ns'])+int(b['receiver_ns'])
            replay_max=max(replay_max,abs(expected-r['output_ns']));assert abs(expected-r['output_ns'])<=1,'Selected delivery API replay mismatch'
        gaps=sum(max(0,y['seq']-x['seq']-1) for x,y in zip(records,records[1:]));window=records[-128:]
        complete_window=bool(records) and len(window)==records[-1]['n'] and not any(y['seq']!=x['seq']+1 for x,y in zip(window,window[1:]))
        if complete_window:
            q=tails([r['error_ns'] for r in window]);last=records[-1]
            assert q['max_ns']==last['max_ns']
            if len(window)>=100:assert q['p95_ns']==last['p95_ns'] and q['p99_ns']==last['p99_ns']
        resume=resumes.get(test,[]);zero_step=any(r['applied']==1 and r['step_ns']==0 and r['old_output_ns']==r['new_output_ns'] for r in resume)
        active=bool(end and end['complete']=='1' and records and len(records)>=100 and records[0]['n']==1 and snapshots and complete_window and not gaps and zero_step)
        last=records[-1] if records else {}
        result.append(dict(test=test,start20=start,end=end,activation_verified=active,selected_status_samples=len(snapshots),
            observed=tails([r['error_ns'] for r in records]),baseline=tails([r['baseline_error_ns'] for r in records]),
            rolling=tails([r['error_ns'] for r in window]),baseline_rolling=tails([r['baseline_error_ns'] for r in window]),
            rolling_reproduced=complete_window,missing_log_records=gaps,replay_max_difference_ns=replay_max,
            resume_zero_step=zero_step,resumes=resume,coverage_gaps=last.get('gaps'),maximum_gap_seconds=last.get('max_gap20',0)/HZ,
            last_sample_age_seconds=(last.get('local20',start)-start)/HZ,
            sampled_safe_seconds=last.get('safe20',0)/HZ,first_crossing_seconds=last.get('cross20',0)/HZ or None,
            baseline_first_crossing_seconds=last.get('base_cross20',0)/HZ or None,
            temperature_shadow_only=b.get('temperature_shadow_only')=='1',beta_mppb_c=int(b['beta_mppb_c']),nonzero_corrections=sum(r['predicted_ns']!=r['baseline_ns'] for r in records)))
    return dict(status='PRODUCTION_HOLDOVER_ACTIVATION_AND_BLE_SAMPLES',sha256=hashlib.sha256(raw).hexdigest(),
                trials=result,rejects=rejects,complete=bool(result) and all(r['activation_verified'] for r in result) and not rejects,
                independent_UTC_qualified=False,continuous_error_bound=False)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    r=analyze(a.capture.read_bytes());text=json.dumps(r,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='')
