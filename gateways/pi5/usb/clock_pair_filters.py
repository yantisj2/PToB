#!/usr/bin/env python3
"""Saved-data common-event host filters with native realtime associations.

Copyright (c) 2026 Jonathan Yantis. MIT. No device/network/clock operations.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.analyse import analyse
from gateways.pi5.usb.calibration import DEFAULT,validate,corrected_phase
from gateways.pi5.usb.clock_pair import realtime_interval
from gateways.pi5.usb.contract import scope
from gateways.pi5.usb.model import statistics
from gateways.pi5.usb.filter_recovery import scenario

METHODS=('robust-host-v1','weighted-host-v2')


def evaluate(raw,calibration=DEFAULT):
    if raw.get('realtime_bracket_method')!='native-pair':raise ValueError('separately labeled native-pair capture required')
    baseline=analyse(raw,calibration=calibration);cohorts={};retained={};meta=baseline['provisional_calibration']
    for row in baseline['source_rows']:
        paired=row.get('native_realtime_pair_shadow',{})
        if 'source_utc_minus_system_ns' not in paired:continue
        key=(scope(row['source_record']),paired['source_measurement_state'])
        cohorts.setdefault(key,{})[row['index']]=paired['source_utc_minus_system_ns']
    forecasts={};host={};counts={}
    for method in METHODS:
        replay=analyse(raw,calibration=calibration,host_filter=method)
        if replay['source']!=baseline['source'] or replay['attempts']!=baseline['attempts']:
            raise ValueError('host filter changed strict source admission or raw denominators')
        values={};rows=[]
        for row,sample in zip(replay['source_rows'],raw['samples']):
            filtered=row.get('experimental_host_filter');paired=row.get('native_realtime_pair_shadow',{})
            if not filtered or 'source_utc_minus_system_ns' not in paired:continue
            clock=sample['source']['before'];association=realtime_interval(clock)
            if not association['nested']:raise ValueError('native association required')
            shift=(association['low_ns']+association['high_ns'])//2-(clock['raw_before_ns']+clock['raw_after_ns'])//2
            key=(scope(row['source_record']),paired['source_measurement_state']);index=row['index']
            current=filtered['source_utc_minus_system_ns']+shift
            control=filtered['same_window_OLS_source_utc_minus_system_ns']+shift
            if index not in cohorts.get(key,{}):raise ValueError('native common-event baseline missing')
            values.setdefault(key,{})[index]=(current,control)
            rows.append(dict(index=index,scope=list(key[0]),measurement_state=key[1],
                native_OLS_ns=cohorts[key][index],native_filtered_ns=current,native_same_window_OLS_ns=control,
                native_OLS_provisional_ns=corrected_phase(cohorts[key][index],meta),
                native_filtered_provisional_ns=corrected_phase(current,meta),
                native_same_window_OLS_provisional_ns=corrected_phase(control,meta),
                previous_model_only=True,source_admission_changed=False,chrony_eligible=False))
        forecasts[method]=values;retained[method]=rows;host[method]=replay['host']['experimental_robust_host']
        counts[method]=Counter(row['measurement_state'] for row in rows)
    results=[];labels={}
    def stats(values):
        return dict(raw=statistics(values),fixed_offset=statistics([corrected_phase(v,meta) for v in values]) if meta['enabled'] else statistics([]))
    for key,base in cohorts.items():
        labels.setdefault(key[0],'C'+str(len(labels)+1))
        indices=set(base);common=indices.intersection(*(set(forecasts[m].get(key,{})) for m in METHODS))
        result=dict(cohort=labels[key[0]],scope=list(key[0]),measurement_state=key[1],native_OLS_all=stats(list(base.values())),
            all_native_OLS_indices=sorted(indices),common_all_methods_indices=sorted(common),
            common_all_methods_native_OLS=stats([base[i] for i in sorted(common)]),methods={})
        for method in METHODS:
            values=forecasts[method].get(key,{});selected=sorted(indices.intersection(values))
            result['methods'][method]=dict(common_denominator=len(selected),missing_forecast_indices=sorted(indices.difference(values)),
                native_OLS_same_events=stats([base[i] for i in selected]),
                native_same_window_OLS=stats([values[i][1] for i in selected]),
                native_filtered=stats([values[i][0] for i in selected]),
                common_all_methods_native_filtered=stats([values[i][0] for i in sorted(common)]))
        results.append(result)
    return dict(schema=1,mode='SAVED_DATA_ONLY_NATIVE_CLOCK_PAIR_FILTER_COMPARISON',
        selection='all native-association phase events; explicit matched predictions and missing forecasts; no error-magnitude selection',
        source_counts=baseline['source']['counts'],attempts=baseline['attempts'],source_permission_changed=False,
        provisional_calibration=meta,filter_parameters_changed=False,phase_cohorts=results,
        retained_phase_forecasts=retained,host_all_forecasts=host,
        forecast_counts={k:dict(v) for k,v in counts.items()},
        same_window_GPIO=baseline['simultaneous_GPIO'],observer=baseline['observer'],
        all_native_clock_snapshots=baseline['native_realtime_pair_shadow']['all_snapshots'],
        absolute_uncertainty_ns=None,chrony_eligible=False,parity_demonstrated=False,
        limits='Current observations score the previous host model. Withheld frame/counter events remain diagnostic; filters grant no source authority. Missing forecasts and all raw tails remain. Clock/controller/PHY bias is unknown; realtime is M-HAT disciplined.')


def markdown(report):
    enabled=report['provisional_calibration']['enabled'];which='fixed_offset' if enabled else 'raw'
    lines=['# Native clock-pair host-filter comparison','',
        'Jonathan Yantis. CC BY 4.0 derived observations; private engineering evidence. Saved-data-only; no source authority or calibration change.','',
        ('The fixed additive−25,600ns correction is unchanged. ' if enabled else 'The derived correction is disabled; this table reports raw phase. ')+
        'Every quality comparison uses the same native realtime association and named common events. '
        'All OLS events, missing filter forecasts, withheld diagnostics and host forecast tails remain in JSON. No parity/UTC claim.','',
        '| Population/method, '+('fixed offset' if enabled else 'raw phase')+' (ns) | n | Bias | SD | RMS | abs P99 | abs max | >1µs | >2µs | >5µs |',
        '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    def add(name,s):
        def f(v):return 'unknown' if v is None else f'{v:.3f}'
        lines.append('| '+name+' | '+str(s['n'])+' | '+' | '.join(f(s.get(k)) for k in ('mean_ns','sigma_ns','rms_ns','p99_abs_ns','max_abs_ns','above_1us','above_2us','above_5us'))+' |')
    for i,c in enumerate(report['phase_cohorts']):
        name=c['cohort']+' '+c['measurement_state'];add(name+' all native OLS',c['native_OLS_all'][which])
        add(name+' common all methods OLS',c['common_all_methods_native_OLS'][which])
        for method,m in c['methods'].items():
            add(name+' '+method+' same-event OLS',m['native_OLS_same_events'][which])
            add(name+' '+method+' same-window OLS',m['native_same_window_OLS'][which])
            add(name+' '+method+' filtered',m['native_filtered'][which])
            add(name+' '+method+' common all methods',m['common_all_methods_native_filtered'][which])
    if report.get('synthetic_recovery'):
        lines.extend(['','## Separate public synthetic recovery','',
            'These known-truth scenarios use two transitions per125ms and symmetric20µs brackets. They are separate from hardware observations; '
            'a first crossing within1µs is not sustained recovery. Full failed/missing forecasts and later errors remain in JSON.','',
            '| Filter/scenario | First recovery after failure (s from change) |16-consecutive declaration (s) | Post-change missing forecasts | Later max after first crossing (ns) |',
            '| --- | ---: | ---: | ---: | ---: |'])
        for s in report['synthetic_recovery']:
            def f(v):return 'not observed' if v is None else f'{v:.3f}'
            no_failure=s['first_post_change_failure_index'] is None
            lines.append('| '+s['method']+' '+s['name']+' | '+('no host failure' if no_failure else f(s['recovery_after_first_failure_delay_from_change_s']))+' | '+
                ('no host failure' if no_failure else f(s['declaration_after_16_consecutive_forecasts_within_1us_delay_from_change_s']))+' | '+str(s['post_change_warmup'])+' | '+f(s['max_later_error_after_first_1us_crossing_ns'])+' |')
    return '\n'.join(lines)+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--out',type=Path,required=True)
    correction=p.add_mutually_exclusive_group();correction.add_argument('--calibration',type=Path);correction.add_argument('--no-calibration',action='store_true')
    p.add_argument('--synthetic-recovery',action='store_true',help='append separate public known-truth step/drift/gap scenarios for both unchanged filters')
    a=p.parse_args();out=a.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts') or not out.parent.is_dir():raise ValueError('new ignored artifacts output required')
    calibration=None if a.no_calibration else validate(json.loads(a.calibration.read_text()) if a.calibration else DEFAULT)
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    provenance=dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),source_snapshot=sources,
        implementation={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()},calibration_configuration=calibration)
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(provenance,f,indent=2)
    document=json.loads(a.input.read_text());raw=document.get('raw',document);report=evaluate(raw,calibration)
    if a.synthetic_recovery:
        report['synthetic_recovery']=[scenario(name,method) for method in METHODS for name in
            ('constant_50ppm_drift','50us_host_phase_step','100ppm_host_rate_step','2.5s_host_observation_gap','1ms_source_phase_epoch_step')]
        report['synthetic_source_recovery_note']='Independent source64-point warmup:2.048s at32ms cadence; no host filter can grant source permission. Recovery cadence is declared synthetic, not assumed from lab data.'
    report.update(created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),input=str(a.input.resolve()),input_sha256=hashlib.sha256(a.input.read_bytes()).hexdigest(),observation_license='CC BY 4.0')
    with out.open('x') as f:json.dump(report,f,indent=2,allow_nan=False);f.write('\n')
    with out.with_suffix('.md').open('x') as f:f.write(markdown(report))
    print(json.dumps(dict(report=str(out),forecast_counts=report['forecast_counts'],source_counts=report['source_counts'])))


if __name__=='__main__':main()
