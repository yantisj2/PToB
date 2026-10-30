#!/usr/bin/env python3
"""Recompute the selected public duration populations and plot tables.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from duration_replay import analyze, stats, HZ, AGES
from fine_frontier import fine_frontier

BASE=Path(__file__).resolve().parents[1]
def write_csv(path,rows,columns):
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=columns,extrasaction='ignore');w.writeheader();w.writerows(rows)

def recompute(write=False):
    provenance=json.loads((BASE/'evidence/provenance.json').read_text())
    results=[];plot=[]
    for population in provenance['populations']:
        path=BASE/population['public_file'];raw=path.read_bytes();records=[json.loads(x) for x in raw.decode().splitlines()]
        r=analyze(records,population['known_empty_population'])
        pairs=[v for v in records if v['kind']=='PAIR']
        gaps=[(b['local20']-a['local20'])/HZ for a,b in zip(pairs,pairs[1:])]
        statuses=r.pop('statuses');prefixes=r.pop('prefixes');first12=r.pop('first_verified_12s')
        verified=[s for s in statuses if s['verified']]
        longest=max((s for s in verified if s['ready']),key=lambda s:s['duration_ms'],default=None)
        observed=max((s for s in statuses if s['ready']),key=lambda s:s['duration_ms'],default=None)
        changes=[];previous=None
        for s in verified:
            if s['duration_ms']!=previous:
                changes.append({k:s[k] for k in ('at_us','duration_ms','n','bad','unknown','lower_bound')})
                previous=s['duration_ms']
        result={**population,**r,'public_sha256':hashlib.sha256(raw).hexdigest(),
                'accepted_pair_span_s':(pairs[-1]['local20']-pairs[0]['local20'])/HZ if pairs else 0,
                'accepted_pair_gaps_above_250ms':sum(g>.25 for g in gaps),'maximum_accepted_pair_gap_s':max(gaps,default=0),
                'verified_statuses':len(verified),'unverified_statuses':len(statuses)-len(verified),
                'ready_verified_statuses':sum(s['ready'] for s in verified),
                'ready_verified_horizon_range_ms':[min(s['duration_ms'] for s in verified if s['ready']),max(s['duration_ms'] for s in verified if s['ready'])] if any(s['ready'] for s in verified) else None,
                'verified_statuses_at_least_12s':sum(s['duration_ms']>=12000 for s in verified),
                'verified_statuses_at_least_48s':sum(s['duration_ms']>=48000 for s in verified),
                'horizon_changes':changes,
                'longest_verified_status':{**{k:longest[k] for k in ('at_us','duration_ms','n','bad','unknown','lower_bound')},'prefix_statistics':longest['age_stats'][AGES.index(longest['duration_ms'])]} if longest and longest['duration_ms'] else None,
                'longest_verified_status_times_us':[s['at_us'] for s in verified if longest and s['duration_ms']==longest['duration_ms']] if longest else [],
                'longest_observed_status':{k:observed[k] for k in ('at_us','duration_ms','n','bad','unknown','verified')} if observed else None,
                'first_verified_12s_uptime_s':first12['at_us']/1e6 if first12 else None,
                'first_verified_12s_since_first_captured_start_s':first12['at_since_first_start_s'] if first12 else None,
                'first_verified_status':{k:verified[0][k] for k in ('at_us','duration_ms','n','bad','unknown')} if verified else None,
                'last_verified_status':{k:verified[-1][k] for k in ('at_us','duration_ms','n','bad','unknown','lower_bound')} if verified else None,
                'range_verified_horizon_ms':[min(s['duration_ms'] for s in verified),max(s['duration_ms'] for s in verified)] if verified else None}
        if '-normal' in population['id'] or '-frontier' in population['id']:
            result['secondary_fine_grid']=fine_frontier(records)
        results.append(result)
        for s in statuses:
            a=s.get('age_stats',[None]*len(AGES))[AGES.index(12000)]
            if a is not None:
                plot.append(dict(population=population['id'],uptime_s=s['at_us']/1e6,from_capture_start_s=s['at_since_first_start_s'],
                                 horizon_s=s['duration_ms']/1000,verified=int(s['verified']),
                                 twelve_population_known=int(s['age_population_known'][AGES.index(12000)]),
                                 twelve_n=a['n'],twelve_bad=a['bad'],twelve_unknown=a['unknown'],
                                 twelve_p95_ns=a['p95_ns'],twelve_p99_ns=a['p99_ns'],twelve_max_ns=a['max_ns']))
        if write:
            write_csv(BASE/'evidence'/f"{population['id']}-prefixes.csv",prefixes,['trial','start20','probe_ms','age_ms','witness20','overshoot_ms','peak_ns','outcome','witness','gaps','max_gap_ms'])
    result=dict(author='Jonathan Yantis',license='CC-BY-4.0',estimand='Nearest-rank empirical Q99 of sampled run-prefix maximum absolute error, inverted at strict <1000 ns.',populations=results,statistic_independent_runs=False)
    if write:
        (BASE/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
        write_csv(BASE/'figures/duration-status.csv',plot,['population','uptime_s','from_capture_start_s','horizon_s','verified','twelve_population_known','twelve_n','twelve_bad','twelve_unknown','twelve_p95_ns','twelve_p99_ns','twelve_max_ns'])
        for population in provenance['populations']:
            rows=[x for x in plot if x['population']==population['id']]
            if rows:
                write_csv(BASE/'figures'/f"{population['id']}.csv",rows,['uptime_s','from_capture_start_s','horizon_s','verified','twelve_population_known','twelve_n','twelve_bad','twelve_unknown','twelve_p95_ns','twelve_p99_ns','twelve_max_ns'])
                write_csv(BASE/'figures'/f"{population['id']}-known.csv",[x for x in rows if x['verified']],['uptime_s','from_capture_start_s','horizon_s'])
        for r in results:
            if '-normal' in r['id'] or '-frontier' in r['id']:
                rows=[dict(age_s=x['age_ms']/1000,n=x['n'],bad=x['bad'],unknown=x['unknown'],p99_ns=x['p99_ns'],population_known=int(x['population_known'])) for x in r['final_age_results'] if x['n']>=100 and x['population_known'] and not x['numeric_censored']]
                write_csv(BASE/'figures'/f"{r['id']}-frontier.csv",rows,['age_s','n','bad','unknown','p99_ns','population_known'])
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write',action='store_true');a=p.parse_args()
    r=recompute(a.write)
    for v in r['populations']:
        print(v['id'], 'pairs',v['pairs'],'runs',v['starts'],'replayed ends',v['replayed_ends'],'known statuses',v['verified_statuses'])
        print('12s final',next(x for x in v['final_age_results'] if x['age_ms']==12000))
