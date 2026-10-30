#!/usr/bin/env python3
"""Predeclared secondary one-second grid on fixed mature long-run cohorts.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
No interpolation, extrapolated witnesses, firmware change or UI substitution.
"""
import math
from duration_replay import HZ, rounded, stats

def fine_frontier(records):
    starts=[v for v in records if v['kind']=='START' and v['probe_ms']>=96000]
    pairs=[v for v in records if v['kind']=='PAIR']
    ends={v['trial']:v for v in records if v['kind']=='END'}
    last=max((p['available20'] for p in pairs),default=0)
    mature=[s for s in starts if s['start20']+(s['probe_ms']+1000)*20000<=last]
    cohort=mature[-128:]
    by_age=[[] for _ in range(120)]
    for s in cohort:
        e=ends.get(s['trial']);peak=0;next_age=1
        if e is not None:
            for p in pairs:
                if p['local20']<=s['start20']:continue
                if p['local20']>e['last20']:break
                dt=p['local20']-s['start20'];t=dt/HZ;phase=s['phase_ns']
                predicted=s['epoch_ns']+50*dt+rounded(t*s['rate']+math.copysign(min(abs(phase),1000*t),phase))
                peak=max(peak,abs(predicted-p['reference_ns']))
                while next_age<=120 and next_age*1000<=s['probe_ms'] and dt>=next_age*HZ:
                    by_age[next_age-1].append(dict(peak_ns=peak,outcome=2 if peak>=1000 else 1,witness=True))
                    next_age+=1
        # Programmed shorter horizons remain right-censored at higher ages;
        # actual expiry after an earlier crossing remains a proven failure.
        while next_age<=120:
            by_age[next_age-1].append(dict(peak_ns=peak,outcome=2 if peak>=1000 else 3,witness=False))
            next_age+=1
    rows=[dict(age_s=i+1,**stats(v)) for i,v in enumerate(by_age)]
    longest=0;failure=None
    for row in rows:
        if not row['ready']:break
        if not row['passes']:
            if row['fail']*100>row['n']:failure=row['age_s']
            break
        longest=row['age_s']
    coarse=None
    if all(r['passes'] for r in rows[:4]):
        coarse=4000
        for age in (6,8,12,16,24,32,48,64,96,120):
            if not rows[age-1]['passes']:break
            coarse=age*1000
    return dict(label='OFFLINE_SECONDARY_FIXED_MATURE_LONG_RUN_COHORT_1S_GRID',
                selection='Final up-to-128 STARTs with programmed probe >=96 s and original probe+1 s deadline elapsed before final accepted-pair availability; no outcome selection.',
                long_starts=len(starts),mature_starts=len(mature),immature_right_censored_starts=len(starts)-len(mature),
                selected_starts=len(cohort),selected_trial_range=[cohort[0]['trial'],cohort[-1]['trial']] if cohort else None,
                selected_start20_range=[cohort[0]['start20'],cohort[-1]['start20']] if cohort else None,
                same_cohort_coarse_frontier_ms=coarse,
                selected_missing_end=sum(s['trial'] not in ends for s in cohort),minimum_scored=100,
                longest_consecutive_passing_s=longest,first_adequately_populated_accuracy_failure_s=failure,
                firmware_UI_grid_changed=False,independent_runs=False,ages=rows)
