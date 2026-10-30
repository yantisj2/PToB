#!/usr/bin/env python3
"""Recompute empirical BLE shadow/baseline tails from captured endpoint records.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,math,re
from pathlib import Path
UNKNOWN=(1<<64)-1
def tails(values):
    ordered=sorted(abs(x) for x in values);n=len(ordered)
    return dict(samples=n,p95_ns=ordered[math.ceil(.95*n)-1] if n else None,
                p99_ns=ordered[math.ceil(.99*n)-1] if n else None,max_ns=max(ordered) if n else None)
def analyze(raw):
    populations={};seen={};duplicates=0
    for line in raw.decode('ascii',errors='strict').splitlines():
        match=re.search(r'(?:^|record=)W12_SHADOW (.*)',line)
        if not match:continue
        row=dict(x.split('=',1) for x in match[1].split());row={k:int(v) for k,v in row.items()}
        assert row['production']==0 and row['horizon_ms']==1000
        assert 20000000<=row['local20']-row['start20']<=22500000
        assert row['predicted_ns']-row['reference_ns']==row['error_ns']
        assert row['baseline_ns']-row['reference_ns']==row['baseline_error_ns']
        key=(row['boot'],row['population'],row['seq'])
        if key in seen:
            assert seen[key]==row,'Conflicting duplicate endpoint';duplicates+=1;continue
        seen[key]=row;populations.setdefault((row['boot'],row['population']),[]).append(row)
    result=[]
    for population,rows in populations.items():
        rows.sort(key=lambda x:x['seq']);gaps=sum(max(0,b['seq']-a['seq']-1) for a,b in zip(rows,rows[1:]))
        latest=rows[-1];window=rows[-128:];complete=len(window)==latest['n'] and all(b['seq']==a['seq']+1 for a,b in zip(window,window[1:]))
        rolling=tails([x['error_ns'] for x in window])
        baseline_rolling=tails([x['baseline_error_ns'] for x in window])
        if complete:
            assert rolling['max_ns']==latest['max_ns']
            if len(window)>=100:assert rolling['p95_ns']==latest['p95_ns'] and rolling['p99_ns']==latest['p99_ns']
            if len(window)>=100 and 'base_p99_ns' in latest:assert baseline_rolling['p99_ns']==latest['base_p99_ns']
        result.append(dict(boot=population[0],population=population[1],observed=tails([x['error_ns'] for x in rows]),
            baseline=tails([x['baseline_error_ns'] for x in rows]),rolling=rolling,baseline_rolling=baseline_rolling,
            rolling_reproduced=complete,missing_log_endpoints=gaps,
            device_completed=latest['completed'],device_missed=latest['missed'],device_aborted=latest['aborted']))
    return dict(status='SAMPLED_BLE_RELATIVE_ONLY',sha256=hashlib.sha256(raw).hexdigest(),
        populations=result,duplicate_records=duplicates,continuous_maximum=False,UTC_qualified=False)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    result=analyze(a.capture.read_bytes());text=json.dumps(result,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='')
