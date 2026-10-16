#!/usr/bin/env python3
"""Decompose retained USB UTC input. Offline diagnostics; no clock training."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


def phase(values, times):
    if not values:
        return {'n': 0}
    # Remove epochs as integers before converting to floating point.
    ys = [v-values[0] for v in values]
    xs = [(t-times[0])/1e9 for t in times]
    xm, ym = statistics.mean(xs), statistics.mean(ys)
    denom = sum((x-xm)**2 for x in xs)
    slope = sum((x-xm)*(y-ym) for x,y in zip(xs,ys))/denom if denom else 0
    residual = [y-ym-slope*(x-xm) for x,y in zip(xs,ys)]
    differences = [b-a for a,b in zip(values,values[1:])]
    return {'n':len(ys), 'raw_sigma_ns':statistics.pstdev(ys),
        'detrended_sigma_ns':math.sqrt(statistics.mean(r*r for r in residual)),
        'trend_ppm':slope/1000, 'range_ns':max(ys)-min(ys),
        'largest_successive_change_ns':max(map(abs,differences),default=0)}


def analyze(path):
    data=path.read_bytes();rows=[json.loads(x) for x in data.splitlines()]
    n=d=None;samples=[];rejected=0;current_serial=None
    for row in rows:
        h=row.get('helper',{}) if row.get('type')=='usb_raw' else row if row.get('event') else {}
        if h.get('event')=='manifest':
            n,d=h.get('timebase_numer'),h.get('timebase_denom');current_serial=h.get('serial')
        if h.get('event')!='sample' or not isinstance(h.get('device_source'),dict):continue
        p=h['device_source']
        if not n or not d:raise ValueError('missing Mach timebase')
        if row.get('ignored_after_stop') is True or row.get('identity_verified') is False or not p.get('valid') or int(p['tick'])!=int(p['current_tick']):rejected+=1;continue
        a=int(h['device_source_continuous_before'])*n//d;b=int(h['device_source_continuous_after'])*n//d
        wa=int(h['device_source_wall_before_ns']);wb=int(h['device_source_wall_after_ns'])
        if b<a or wb<wa or b-a>100_000_000 or abs(wb-wa-(b-a))>5_000_000:rejected+=1;continue
        if 'utc_offset_ns' not in p:rejected+=1;continue
        scope=':'.join(str(p.get(k,'')) for k in ('silicon','boot','usb_epoch','capture_epoch','root','root_boot','source_epoch','path_scope'))
        samples.append({'scope':scope,'serial':current_serial,'mach':(a+b)//2,'wall':(wa+wb)//2,
            'utc':int(p['utc_ns']),'offset':int(p['utc_offset_ns']),'counter_ns':int(p['tick'])*125//2,'width':b-a})
    groups={}
    for scope in sorted(set(r['scope'] for r in samples)):
        rs=[r for r in samples if r['scope']==scope];ts=[r['mach'] for r in rs]
        # Exact additive ledger: UTC-wall = (UTC-offset-counter)
        # + (counter-Mach) + offset - (wall-Mach).
        series={'UTC_minus_local_wall':[r['utc']-r['wall'] for r in rs],
            'counter_minus_request_midpoint':[r['counter_ns']-r['mach'] for r in rs],
            'source_map_minus_counter':[r['utc']-r['offset']-r['counter_ns'] for r in rs],
            'source_UTC_offset':[r['offset'] for r in rs],
            'local_wall_minus_Mach':[r['wall']-r['mach'] for r in rs]}
        assert all(series['UTC_minus_local_wall'][i]==series['source_map_minus_counter'][i]+series['counter_minus_request_midpoint'][i]+series['source_UTC_offset'][i]-series['local_wall_minus_Mach'][i] for i in range(len(rs)))
        groups[scope]={'n':len(rs),'diagnostics':{name:phase(values,ts) for name,values in series.items()},
            'request_width_ns':{'minimum':min(r['width'] for r in rs),'maximum':max(r['width'] for r in rs)},
            'midpoint_bias_ns':statistics.mean(series['UTC_minus_local_wall']),
            'source_UTC_offset_changes':sum(b['offset']!=a['offset'] for a,b in zip(rs,rs[1:]))}
    retained=[g for g in groups.values() if g['n']>=8]
    count=sum(g['n'] for g in retained)
    weighted={name:math.sqrt(sum(g['n']*g['diagnostics'][name]['detrended_sigma_ns']**2 for g in retained)/count)
        for name in retained[0]['diagnostics']} if retained else {}
    return {'per_epoch_summary':{'minimum_samples':8,'epochs':len(retained),'n':count,'weighted_detrended_sigma_ns':weighted},'schema':1,'capture':str(path.resolve()),'sha256':hashlib.sha256(data).hexdigest(),
        'valid_periodic_records':len(samples),'rejected_or_unavailable_periodic_records':rejected,'scopes':groups,
        'target_ns':100_000,'precision_qualified':False,
        'limitations':['Midpoints are request timing diagnostics; no physical event identity or absolute bias is inferred.',
          'Component variances are correlated and must not be summed as independent error terms.',
          'Retrospective detrending is not clock training or measured UTC accuracy.']}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path);parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();result=analyze(a.capture)
    with a.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps(result['per_epoch_summary'],indent=2))
