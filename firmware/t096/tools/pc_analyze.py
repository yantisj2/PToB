#!/usr/bin/env python3
"""Analyze normalized PC observations offline; no hardware or network access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Derived evidence/CSVs: CC BY 4.0, Jonathan Yantis.
"""
import argparse, csv, hashlib, json, math
from collections import defaultdict
from pathlib import Path

def statistics(values):
    if not values:
        return dict(n=0,mean_ns=None,rms_ns=None,std_ns=None,max_abs_ns=None)
    n=len(values);mean=sum(values)/n
    return dict(n=n,mean_ns=mean,rms_ns=math.sqrt(sum(x*x for x in values)/n),
                std_ns=math.sqrt(sum((x-mean)**2 for x in values)/n),max_abs_ns=max(map(abs,values)))

def analyze(path,out):
    out.mkdir(parents=True,exist_ok=False)
    trials={};pairs={};witness={};boots=set();rows=0
    with path.open() as stream:
        for line in stream:
            x=json.loads(line);rows+=1;p=x.get('payload',{});boot=p.get('boot')
            if boot:boots.add(boot)
            if x['kind']=='pc':
                if p['trial']:trials[(boot,p['trial'])]=p
                if p.get('found'):pairs[(boot,p['record']['sequence'])]=p['record']
            if x['kind']=='shadow' and p.get('found'):witness[(boot,p['sequence'])]=dict(p,_segment=x.get('segment',''))
    groups=defaultdict(lambda:dict(values=[],invalid=0,records=0,trials=set()))
    output=[]
    for (boot,seq),p in sorted(witness.items()):
        tick=p['edge_tick'];trial=0;phase='baseline';mode=listen=hold=0;age=None
        candidates=[t for (b,_),t in trials.items() if b==boot and t['requested_tick']<=tick]
        if candidates and p['_segment']!='baseline':
            t=max(candidates,key=lambda t:t['requested_tick']);trial=t['trial'];mode=t['mode'];listen=t['listen_ms'];hold=t['hold_ms']
            if t['hold_from_tick'] and t['hold_from_tick']<=tick<t['hold_until_tick']:
                phase='hold';age=(tick-t['hold_from_tick'])/16000000
            elif t['hold_until_tick'] and tick>=t['hold_until_tick']:phase='recovery'
            else:phase='acquisition_listen'
        eligible=bool(p['map_valid'] and p['reference_seen'] and p['reference_fix'] and p['reference_pps'] and p['reason']==0)
        error=p['phase_ns'] if eligible else None
        age_bin=math.floor(age) if age is not None else -1
        key=(phase,mode,listen,hold,age_bin);g=groups[key];g['records']+=1;g['trials'].add((boot,trial))
        if eligible:g['values'].append(error)
        else:g['invalid']+=1
        output.append(dict(boot=boot,sequence=seq,trial=trial,phase=phase,mode=mode,listen_ms=listen,hold_ms=hold,
            hold_age_s=age,edge_tick=tick,eligible=eligible,error_ns=error,reason=p['reason'],
            pc_projection=p.get('pc_projection',False),capture_epoch=p['edge_epoch'],root_epoch=p['root_epoch'],
            map_generation=p['map_generation'],reference_seen=p['reference_seen'],reference_fix=p['reference_fix'],
            reference_pps=p['reference_pps'],association=p['association']))
    if output:
        with (out/'witness.csv').open('w',newline='') as stream:
            w=csv.DictWriter(stream,fieldnames=output[0]);w.writeheader();w.writerows(output)
    summary=[]
    for key,g in sorted(groups.items()):
        phase,mode,listen,hold,age_bin=key
        s=dict(phase=phase,mode=mode,listen_ms=listen,hold_ms=hold,hold_age_bin_s=age_bin,
               records=g['records'],invalid=g['invalid'],distinct_trials=len(g['trials']),**statistics(g['values']))
        s.update({f'rms_below_{us}us':None if not s['n'] else s['rms_ns']<us*1000 for us in (1,10,100)})
        summary.append(s)
    if summary:
        with (out/'age-bins.csv').open('w',newline='') as stream:
            w=csv.DictWriter(stream,fieldnames=summary[0]);w.writeheader();w.writerows(summary)
    def gaps(items):
        result=[]
        for boot in boots:
            seq=sorted(n for b,n in items if b==boot)
            result.extend(dict(boot=boot,after=a,before=b,missing=b-a-1) for a,b in zip([0]+seq,seq) if b>a+1)
        return result
    report=dict(schema=1,status='ANALYZED_NOT_QUALIFIED',input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        analyzer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),input_rows=rows,boots=sorted(boots),
        witness_records=len(witness),causal_ble_records=len(pairs),witness_cursor_gaps=gaps(witness),ble_cursor_gaps=gaps(pairs),
        trials=[dict(boot=b,trial=n,mode=t['mode'],listen_ms=t['listen_ms'],hold_ms=t['hold_ms'],last_phase=t['phase'],
                     failures=t['failures'],hold_from_tick=t['hold_from_tick'],hold_until_tick=t['hold_until_tick'],
                     rf_closed_tick=t['rf_closed_tick'],first_return_sequence=t['first_return_sequence']) for (b,n),t in sorted(trials.items())],
        populations=summary,evidence_license='CC-BY-4.0',author='Jonathan Yantis',
        limitations=['Fractional relative PPS phase, no calibrated UTC/output qualification.',
                     'No energy or independently measured RF airtime.',
                     'Small age-bin populations are exploratory; no reliability certification.',
                     'Sequence gaps are unavailable observations, never zero error.',
                     'Raw causal BLE error and PPS phase are distinct populations.'])
    (out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('observations',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    r=analyze(a.observations,a.out);print(json.dumps({k:r[k] for k in ('status','witness_records','causal_ble_records','trials')},indent=2))
