#!/usr/bin/env python3
"""Public synthetic host-filter recovery scenarios; no hardware/network access.

Copyright (c) 2026 Jonathan Yantis. MIT. Synthetic truth is not lab evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.host_filter import RobustHost
from gateways.pi5.usb.model import CausalAffine,statistics


def scenario(name,method='robust-host-v1'):
    if method not in ('robust-host-v1','weighted-host-v2'):raise ValueError('unknown recovery method')
    window=128 if method=='robust-host-v1' else 1024
    origin=8*10**18;robust=RobustHost(maximum=window,gated=method=='robust-host-v1');ols=CausalAffine();control=CausalAffine(maximum=window);rows=[]
    # Two observed transitions per125ms is a declared synthetic cadence,
    # not an assumed property of any captured population.
    def ordinal(i):return i//2*1000+i%2
    at_change=ordinal(160)*125000;source_phase=0
    for i in range(420):
        if name=='2.5s_host_observation_gap' and 160<=i<200:continue
        x=ordinal(i);phase=0
        if name=='50us_host_phase_step' and i>=160:phase=50000
        if name=='100ppm_host_rate_step' and i>=160:phase=(x-ordinal(160))*125000//10000
        if name=='constant_50ppm_drift':phase=x*125000//20000
        if name=='1ms_source_phase_epoch_step' and i>=160:source_phase=1_000_000
        truth=origin+x*125000+phase
        prediction=robust.predict(x);r=robust.observe(x,truth-10000,truth+10000)
        error=ols.observe(x,truth);same=control.observe(x,truth)
        rows.append(dict(index=i,elapsed_s=x*125000/1e9,synthetic_truth_raw_ns=truth,
            robust=r,OLS_error_ns=error,control_OLS_error_ns=same,
            derived_source_phase_ns=None if prediction is None else source_phase+(truth-prediction[0])-prediction[1]))
    changed=[r for r in rows if r['index']>=160]
    # Also report recovery AFTER the first failure/warmup. A rate change can
    # start at zero phase error, so its initial correct forecast is not recovery
    # from later lag. Every earlier and subsequent failure remains in rows.
    recovered=next((r for r in changed if r['robust']['forecast_error_ns'] is not None and abs(r['robust']['forecast_error_ns'])<=1000),None)
    first_failure=next((r for r in changed if r['robust']['forecast_error_ns'] is None or abs(r['robust']['forecast_error_ns'])>1000),None)
    after_failure=next((r for r in changed if first_failure and r['index']>first_failure['index'] and
        r['robust']['forecast_error_ns'] is not None and abs(r['robust']['forecast_error_ns'])<=1000),None)
    sustained=None;run=0
    for r in changed:
        e=r['robust']['forecast_error_ns']
        if first_failure and r['index']>first_failure['index'] and e is not None and abs(e)<=1000:run+=1
        else:run=0
        if run==16:sustained=r;break
    later=[abs(r['robust']['forecast_error_ns']) for r in changed if after_failure and r['index']>after_failure['index'] and r['robust']['forecast_error_ns'] is not None]
    common=[r for r in rows if r['robust']['forecast_error_ns'] is not None and r['OLS_error_ns'] is not None]
    return dict(name=name,method=method,control_maximum_points=window,observed=len(rows),counts=dict(robust.counts),
        declared_cadence='two transitions per125ms;20us symmetric complete brackets; integer RAW origin8e18ns',
        common_denominator=len(common),OLS=statistics([r['OLS_error_ns'] for r in common]),
        control_OLS=statistics([r['control_OLS_error_ns'] for r in common]),
        robust_all_forecasts=statistics([r['robust']['forecast_error_ns'] for r in common]),
        post_change_training_withheld=sum(not r['robust']['training_accepted'] for r in changed),
        post_change_warmup=sum(r['robust']['forecast_error_ns'] is None for r in changed),
        first_post_change_forecast_within_1us_index=recovered['index'] if recovered else None,
        first_post_change_forecast_within_1us_delay_s=recovered['elapsed_s']-at_change/1e9 if recovered else None,
        first_post_change_failure_index=first_failure['index'] if first_failure else None,
        recovery_after_first_failure_index=after_failure['index'] if after_failure else None,
        recovery_after_first_failure_delay_from_change_s=after_failure['elapsed_s']-at_change/1e9 if after_failure else None,
        recovery_after_first_failure_delay_from_failure_s=after_failure['elapsed_s']-first_failure['elapsed_s'] if after_failure else None,
        declaration_after_16_consecutive_forecasts_within_1us_index=sustained['index'] if sustained else None,
        declaration_after_16_consecutive_forecasts_within_1us_delay_from_change_s=sustained['elapsed_s']-at_change/1e9 if sustained else None,
        max_later_error_after_first_1us_crossing_ns=max(later,default=None),
        recovery_definition='first scored <=1us forecast after first post-change error>1us or missing forecast; all later errors still retained',
        derived_source_phase=statistics([r['derived_source_phase_ns'] for r in changed if r['derived_source_phase_ns'] is not None]),
        physical_accuracy_claim=False,source_authority_granted=False,rows=rows)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--host-filter',choices=('robust-host-v1','weighted-host-v2'),default='robust-host-v1');args=p.parse_args();out=args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts'):raise ValueError('choose a new ignored artifacts/ output')
    sources={f.name:f.read_text() for f in Path(__file__).parent.glob('*.py')}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(public_source_snapshot=sources,
        hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}),f,indent=2)
    scenarios=[scenario(name,args.host_filter) for name in ('constant_50ppm_drift','50us_host_phase_step','100ppm_host_rate_step',
        '2.5s_host_observation_gap','1ms_source_phase_epoch_step')]
    report=dict(kind='PUBLIC_SYNTHETIC_ONLY',attribution='Jonathan Yantis',derived_data_license='CC BY 4.0',
        source_code_license='MIT',experimental_filter=args.host_filter,scenarios=scenarios,
        source_recovery_fixture='Separate public integration test changes source epoch at32ms cadence:64 fresh source warmup points, first prediction2.048s later; host model does not grant source permission.')
    with out.open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    for s in scenarios:print(s['name'],s['counts'],'recovery_after_failure_from_change_s',s['recovery_after_first_failure_delay_from_change_s'])


if __name__=='__main__':main()
