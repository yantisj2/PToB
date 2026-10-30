#!/usr/bin/env python3
"""Generate manuscript result text from the supplied recomputed analysis.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import json
import argparse
from pathlib import Path
from build_analysis import BASE
from build_analysis import recompute

IDS=['C1-normal','C2-frontier01','C2-frontier02','C1-frontier02']
LABELS=['C-1 initial','C-2 initial','C-2 later','C-1 later']

def age(r,ms):return next(a for a in r['final_age_results'] if a['age_ms']==ms)
def number(x):return '--' if x is None else str(x)
def seconds(ms):return f'{ms/1000:g}'
def make_tex(analysis):
    pops={p['id']:p for p in analysis['populations']}
    ids=IDS+(['C1-frontier03'] if 'C1-frontier03' in pops else [])
    labels=LABELS+(['C-1 conditioned'] if 'C1-frontier03' in pops else [])
    p=[pops[x] for x in ids]
    best=max(x['longest_verified_status']['duration_ms'] for x in p if x['longest_verified_status'])
    f=[x['final_replayed_horizon'] for x in p]
    table=[]
    for label,x,h in zip(labels,p,f):
        a=age(x,h['duration_ms']);symbol=r'\geq' if h['lower_bound'] else ''
        q='/'.join(number(a[k]) for k in ('p95_ns','p99_ns','max_ns')) if not a['numeric_censored'] else '-- (censored)'
        table.append(f"{label} & ${symbol}{seconds(h['duration_ms'])}$ s & {q} ns & {a['n']}; {a['fail']} / {a['unknown']}\\\\")
    fine=[];fine_text=[]
    for label,x in zip(labels,p):
        g=x['secondary_fine_grid']
        if g['selected_starts']<100:continue
        last=g['longest_consecutive_passing_s'];fail=g['first_adequately_populated_accuracy_failure_s']
        a=g['ages'][last-1] if last else None;b=g['ages'][fail-1] if fail else None
        aq=number(a['p99_ns']) if a and not a['numeric_censored'] else '--'
        bq=number(b['p99_ns']) if b and not b['numeric_censored'] else '--'
        fail_label=f'{fail} s' if fail else '--'
        fine.append(f"{label} & {g['selected_starts']} & {last} s & {fail_label} & {aq} / {bq} & {b['fail'] if b else '--'} / {b['unknown'] if b else '--'}\\\\")
        coarse=seconds(g['same_cohort_coarse_frontier_ms']) if g['same_cohort_coarse_frontier_ms'] is not None else '--'
        fine_text.append(f"{label}'s same fixed cohort gives {coarse} s on the coarse grid and {last} s on the one-second grid; {g['immature_right_censored_starts']} younger long starts remain censored separately.")
    pairs=sum(x['pairs'] for x in p);ends=sum(x['replayed_ends'] for x in p)
    gaps='/'.join(str(x['accepted_pair_gaps_above_250ms']) for x in p)
    maxgaps='/'.join(f"{x['maximum_accepted_pair_gap_s']:.3f}" for x in p)
    later='; '.join(f"{labels[i]} ends at {seconds(f[i]['duration_ms'])} s" for i in range(2,len(p)))
    winner=max((x for x in p if x['longest_verified_status']),key=lambda x:x['longest_verified_status']['duration_ms'])
    ws=winner['longest_verified_status'];wq=ws['prefix_statistics']
    abstract=f"The longest fully replayed rolling device frontier is {seconds(best)} seconds on {winner['device']}, with {ws['n']} prefixes, P99 {wq['p99_ns']} ns and maximum {wq['max_ns']} ns in that window."
    fw=max((x['secondary_fine_grid'] for x in p if x['secondary_fine_grid']['selected_starts']>=100),key=lambda x:x['longest_consecutive_passing_s'])
    boundary=f"and {fw['first_adequately_populated_accuracy_failure_s']} s failing" if fw['first_adequately_populated_accuracy_failure_s'] else 'with no adequately populated later failure'
    fq=fw['ages'][fw['longest_consecutive_passing_s']-1]['p99_ns']
    abstract2=f"A separately selected fixed mature {fw['selected_starts']}-run cohort resolves {fw['longest_consecutive_passing_s']} s passing (P99 {fq} ns) {boundary} on a one-second grid. Later capture ends vary: {later}."
    if best>=120000:abstract2+=' The 120 s ceiling is a test cap, not a physical maximum.'
    elif best>=96000:abstract2+=' The next 120 s age is reported with its actual population.'
    count='Five' if len(p)==5 else 'Four'
    conditional=' The fifth lease is the predeclared conditioned 120 s follow-up.' if len(p)==5 else ''
    primary=f"{count} fixed capture leases, each bounded at 600 s and stopped near 596 s, preserve separate device/time windows.{conditional} The table reports declared capture-end rings. Across all captured 48 s prefixes in the initial windows, C-1 has 220 prefixes (P99/max 911/953 ns) and C-2 has 193 (796/880 ns), with no failures. C-2's later device briefly reports 64 s before the new segment's long-age prehistory is flushed; the lighter plot line retains that observation without promoting it to an independently reconstructed 64 s numeric ring."
    times=winner['longest_verified_status_times_us']
    primary+=' The conditioned 120 s follow-up was not triggered because final 96 s qualification failed.'
    primary+=f" The fully replayed {seconds(best)} s result spans {len(times)} observed statuses at {min(times)/1e6:.3f}--{max(times)/1e6:.3f} s device uptime in {winner['device']}'s later window."
    coverage=f"The captures contain {pairs} accepted pairs and {ends} numerically replayed START/END pairs, with zero missing accepted-pair sequence records or capture-loss counter deltas. Gap counts above 250 ms are {gaps}; their maxima are {maxgaps} s, respectively in table order. Orphan, unfinished and unknown outcomes remain separate in the supplement."
    ramp="C-2 acquisition is captured before trial 1; its first 48 s qualification is at 400.690 s uptime. C-1's initial setup refusal occurred before USB opening; the retry begins at trial 668 and independently flushes each age's history. Its exact device trajectory reaches 48 s, drops to 1.5 s at 480.719 s because short runs expire unknown, and recovers at 520.782 s. C-2's later window also has an unknown-driven short-age dip, followed by actual longer-age timing failures and recovery. These are distinct causes of a lower displayed frontier."
    scope="P99 pairs in this table are at the last passing and first failing one-second ages, with the failure/unknown count at the failing age. All numeric prefixes at those boundaries are explicitly counted in the supplement. The same-cohort coarse comparison separates grid resolution from changing run populations; it does not substitute this older fixed cohort for the capture-end OLED ring."
    lines=['% Generated from analysis.json by tools/write_values.py; CC-BY-4.0, Jonathan Yantis.',r'\def\HaveCtwo{1}',r'\def\HaveWarm{1}']
    if len(p)==5:lines.append(r'\def\HaveCap{1}')
    for name,value in [('AbstractResult',abstract),('AbstractDuration',abstract2),('PrimaryTable','\n'.join(table)),('TableScope','All rows use installed v33 and the latest per-age ring at the declared capture end. A passing tested frontier with no adequately populated next-age failure is a lower bound. These values do not describe every status in the window.'),('PrimaryResults',primary),('CoverageResults',coverage),('RampResults',ramp),('FineGridResults',' '.join(fine_text)),('FineGridTable','\n'.join(fine)),('FineGridScope',scope)]:
        lines.append('\\newcommand{\\'+name+'}{'+value+'}')
    return '\n'.join(lines)+'\n'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=BASE.parents[1]/'artifacts/mcxo-holdover-duration/results.tex')
    args=parser.parse_args();args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(make_tex(recompute(False)))
