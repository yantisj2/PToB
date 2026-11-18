#!/usr/bin/env python3
"""Offline received-time quality for every capture/cohort, without a settle gate.

Copyright (c) 2026 Jonathan Yantis. MIT. No device, network or clock operations.
"""
import argparse
import bisect
from collections import Counter
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.analyse import analyse,hat_status
from gateways.pi5.usb.contract import scope
from gateways.pi5.usb.model import statistics
from gateways.pi5.usb.calibration import DEFAULT as DEFAULT_CALIBRATION,validate as validate_calibration


def capture_quality(path,raw,a):
    cohorts={};intervals=[];last_interval=None
    for row,sample in zip(a['source_rows'],raw['samples']):
        d=row.get('source_record');eligible=bool(d and d['valid'] and row.get('source',{}).get('state')!='WITHHELD')
        label=None
        if d and d['valid']:
            key=scope(d)
            if key not in cohorts:
                cohorts[key]=dict(label='C'+str(len(cohorts)+1),scope=list(key),root_context=tuple(d[k] for k in ('root','root_boot','source_epoch')),
                    counts=Counter(),device_errors=[],conditional_RAW_errors=[],phase_admitted=[],phase_withheld=[],
                    phase_corrected_admitted=[],phase_corrected_withheld=[],
                    phase_common_OLS_admitted=[],phase_common_OLS_withheld=[],
                    phase_common_OLS_corrected_admitted=[],phase_common_OLS_corrected_withheld=[],
                    phase_common_control_admitted=[],phase_common_control_withheld=[],
                    phase_common_control_corrected_admitted=[],phase_common_control_corrected_withheld=[],
                    phase_filtered_admitted=[],phase_filtered_withheld=[],
                    phase_filtered_corrected_admitted=[],phase_filtered_corrected_withheld=[],
                    phase_native_pair_admitted=[],phase_native_pair_withheld=[],phase_native_pair_corrected_admitted=[],phase_native_pair_corrected_withheld=[],
                    phase_native_pair_control_admitted=[],phase_native_pair_control_withheld=[],
                    phase_native_pair_control_corrected_admitted=[],phase_native_pair_control_corrected_withheld=[],GPIO_all=[],GPIO_eligible=[])
            c=cohorts[key];label=c['label'];c['counts']['wire_valid']+=1
            state=row['source']['state'];c['counts'][state]+=1
            if eligible:c['counts']['admitted']+=1
            if row['source'].get('causal_error_ns') is not None:c['device_errors'].append(row['source']['causal_error_ns'])
            if row.get('conditional_source',{}).get('causal_error_ns') is not None:c['conditional_RAW_errors'].append(row['conditional_source']['causal_error_ns'])
            phase=row.get('conditional_spec_shadow',{}).get('source_utc_minus_system_ns')
            if phase is not None:c['phase_admitted' if eligible else 'phase_withheld'].append(phase)
            corrected=row.get('conditional_spec_shadow',{}).get('provisional_source_utc_minus_system_ns')
            if corrected is not None:c['phase_corrected_admitted' if eligible else 'phase_corrected_withheld'].append(corrected)
            paired=row.get('native_realtime_pair_shadow',{})
            if 'source_utc_minus_system_ns' in paired:
                suffix='admitted' if eligible else 'withheld'
                c['phase_native_pair_'+suffix].append(paired['source_utc_minus_system_ns'])
                c['phase_native_pair_control_'+suffix].append(paired['full_bracket_control_source_utc_minus_system_ns'])
                value=paired['provisional_source_utc_minus_system_ns']
                if value is not None:c['phase_native_pair_corrected_'+suffix].append(value)
                if corrected is not None:c['phase_native_pair_control_corrected_'+suffix].append(corrected)
            filtered=row.get('experimental_host_filter',{})
            if filtered:
                suffix='admitted' if eligible else 'withheld'
                c['phase_common_OLS_'+suffix].append(phase)
                c['phase_filtered_'+suffix].append(filtered['source_utc_minus_system_ns'])
                c['phase_common_control_'+suffix].append(filtered['same_window_OLS_source_utc_minus_system_ns'])
                value=filtered['provisional_same_window_OLS_source_utc_minus_system_ns']
                if value is not None:c['phase_common_control_corrected_'+suffix].append(value)
                if corrected is not None:c['phase_common_OLS_corrected_'+suffix].append(corrected)
                value=filtered['provisional_source_utc_minus_system_ns']
                if value is not None:c['phase_filtered_corrected_'+suffix].append(value)
        at=(sample['clocks']['raw_before_ns']-raw['start']['raw_before_ns'])/1e9
        state='ADMITTED' if eligible else 'VALID_FLAG_WITHHELD' if d and d['valid'] else 'UNAVAILABLE_OR_NO_DECODE'
        if last_interval is None or (last_interval['cohort'],last_interval['state'])!=(label,state) or at-last_interval['last_s']>.5:
            last_interval=dict(cohort=label,state=state,first_s=at,last_s=at,n=1);intervals.append(last_interval)
        else:last_interval['last_s']=at;last_interval['n']+=1
    statuses=[hat_status(s) for s in raw['status']];times=[s['boottime_ns'] for s in statuses]
    for p in raw['pps']:
        if p['seed']:continue
        stamp=p['assertion'].split('#')[0];ns=int(Decimal(stamp)*1_000_000_000);phase=(ns+500_000_000)%1_000_000_000-500_000_000
        k=bisect.bisect_left(times,p['before']['boottime_ns'])
        if not 0<k<len(statuses):continue
        left,right=statuses[k-1:k+1]
        if right['boottime_ns']-left['boottime_ns']>1_600_000_000:continue
        for c in cohorts.values():
            if all(tuple(s.get(f) for f in ('root','root_boot','epoch'))==c['root_context'] for s in (left,right)):
                c['GPIO_all'].append(phase)
                if left['eligible'] and right['eligible'] and left['context']==right['context']:c['GPIO_eligible'].append(phase)
    result=[]
    for c in cohorts.values():
        for field in list(c):
            if field in ('device_errors','conditional_RAW_errors','GPIO_all','GPIO_eligible') or field.startswith('phase_'):c[field]=statistics(c[field])
        c['counts']=dict(c['counts']);c['root_context']=list(c['root_context']);result.append(c)
    return dict(path=str(path),input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        capture_identity=[raw.get('boot_id'),raw['start']['raw_before_ns'],raw['end']['raw_after_ns']],
        rate=raw['rate'],method=raw.get('host_read_method','seek-read'),elapsed_s=a['observer']['elapsed_s'],
        attempts=a['attempts'],source_counts=a['source']['counts'],cohorts=result,received_intervals=intervals,
        GPIO_all=a['simultaneous_GPIO']['new_edges'],GPIO_eligible=a['simultaneous_GPIO']['source_eligible_edges'],
        GPIO_status=a['simultaneous_GPIO']['status_observations'],GPIO_eligible_status=a['simultaneous_GPIO']['eligible_status_observations'],
        gaps=a['simultaneous_GPIO']['gaps'],capture_errors=raw['errors'],released=raw['interface_released'],
        host=a['host'],observer=a['observer'],provisional_calibration=a['provisional_calibration'],
        host_preparation_warmup=a['host_preparation_warmup'],
        experimental_host_filter=a['experimental_host_filter'],local_estimate_diagnostic=a['local_estimate_diagnostic'],
        native_realtime_pair_shadow=a['native_realtime_pair_shadow'],qualified_parity=False)


def fmt(value):return 'unknown' if value is None else f'{value:.3f}'


def metrics(name,s):
    return '| '+name+' | '+str(s['n'])+' | '+' | '.join(fmt(s.get(k)) for k in
        ('mean_ns','sigma_ns','rms_ns','p99_abs_ns','max_abs_ns','above_1us','above_2us','above_5us'))+' |'


def markdown(report):
    lines=['# Received-time observations','',
        'Jonathan Yantis. CC BY 4.0 derived observations; private, unreviewed. All selected captures are retained, including outages.','',
        'Received-time diagnostics do not require a continuous600s window. That requirement applies to eventual qualification. '
        'Source/SOF stability and conditional RAW consistency describe admitted segments. Phase SD describes variation about an uncalibrated bias. '
        'Neither supplies an absolute timestamp bound or proves USB/M-HAT parity. Pi realtime is M-HAT disciplined.','',
        'The provisional additive−25,600ns correction is frozen from the first admitted phase cohort A (648 samples, '
        '+25,590.433ns mean) in the8Hz600s candidate. It is dependent on that M-HAT-disciplined clock, '
        'trained with seek-read and applied unchanged to later methods/cohorts. Raw and corrected tails remain separate; no re-zero or learned mean is applied.','',
        'Cumulative denominators across non-overlapping captures: `'+json.dumps(report['cumulative_counts'],sort_keys=True)+'`. '
        'No RMS or percentile is pooled across captures, methods or authenticated cohorts.','']
    for r in report['captures']:
        lines.extend(['## '+Path(r['path']).name,'',f"Attempts {r['attempts'].get('attempted',0)}; wire-valid {r['attempts'].get('source_valid',0)}; "
            f"unavailable {r['attempts'].get('source_unavailable',0)}; admitted {r['source_counts'].get('accepted',0)}; "
            f"warmup {r['source_counts'].get('warmup',0)}; predicted {r['source_counts'].get('predicted',0)}. "
            f"Method {r['method']}; errors {len(r['capture_errors'])}; released {r['released']}. "
            f"M-HAT status eligible {r['GPIO_eligible_status']}/{r['GPIO_status']}. Final intervals/gaps remain in JSON.",'',
            '| Metric (ns) | n | Bias | SD | RMS | abs P99 | abs max | >1µs | >2µs | >5µs |',
            '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |',
            metrics('M-HAT all new PPS',r['GPIO_all']),metrics('M-HAT eligible PPS',r['GPIO_eligible'])])
        for c in r['cohorts']:
            label=c['label'];lines.extend([metrics(label+' source/device-SOF causal',c['device_errors']),
                metrics(label+' conditional source/RAW causal',c['conditional_RAW_errors']),
                metrics(label+' conditional USB/system phase ADMITTED',c['phase_admitted']),
                metrics(label+' fixed−25,600ns phase ADMITTED',c['phase_corrected_admitted']),
                metrics(label+' conditional phase WITHHELD diagnostic',c['phase_withheld']),
                metrics(label+' fixed−25,600ns WITHHELD diagnostic',c['phase_corrected_withheld']),
                metrics(label+' GPIO eligible, sampled root match',c['GPIO_eligible'])])
            if r['native_realtime_pair_shadow']['common_predictions']:
                lines.extend([metrics(label+' paired full-bracket control ADMITTED',c['phase_native_pair_control_admitted']),
                    metrics(label+' native realtime sub-bracket ADMITTED',c['phase_native_pair_admitted']),
                    metrics(label+' paired full-bracket fixed offset ADMITTED',c['phase_native_pair_control_corrected_admitted']),
                    metrics(label+' native realtime fixed offset ADMITTED',c['phase_native_pair_corrected_admitted']),
                    metrics(label+' native realtime WITHHELD diagnostic',c['phase_native_pair_withheld']),
                    metrics(label+' native realtime fixed offset WITHHELD',c['phase_native_pair_corrected_withheld'])])
            if r['experimental_host_filter']['enabled']:
                lines.extend([metrics(label+' common-time OLS phase ADMITTED',c['phase_common_OLS_admitted']),
                    metrics(label+' common-time OLS fixed offset ADMITTED',c['phase_common_OLS_corrected_admitted']),
                    metrics(label+' common-window OLS fixed offset ADMITTED',c['phase_common_control_corrected_admitted']),
                    metrics(label+' experimental host-filter phase ADMITTED',c['phase_filtered_admitted']),
                    metrics(label+' experimental host-filter fixed offset ADMITTED',c['phase_filtered_corrected_admitted']),
                    metrics(label+' common-time OLS WITHHELD diagnostic',c['phase_common_OLS_withheld']),
                    metrics(label+' experimental host-filter WITHHELD diagnostic',c['phase_filtered_withheld']),
                    metrics(label+' experimental host-filter fixed offset WITHHELD',c['phase_filtered_corrected_withheld'])])
        lines.append('')
        preparation=r['host_preparation_warmup']
        if preparation['requested_seconds']:
            lines.extend([f"Host-only preparation: {preparation['requested_seconds']}s requested, {preparation['bursts']}bursts, "
                f"{preparation['retained_reads']}retained register reads. Its cost remains in the full observer region; "
                'source training remains independent. The fixed acquisition window begins after preparation.',''])
        pair=r['native_realtime_pair_shadow'];snapshots=pair['all_snapshots']
        if pair['method']=='native-pair':
            lines.extend(['Native cross-clock snapshots: `'+json.dumps(snapshots['counts'],sort_keys=True)+'`. '
                f"Matched full/native brackets {snapshots['paired_full_bracket_ns']['n']}/{snapshots['native_bracket_ns']['n']}; "
                f"mean {fmt(snapshots['paired_full_bracket_ns']['mean_ns'])}/{fmt(snapshots['native_bracket_ns']['mean_ns'])}ns; "
                f"maximum {fmt(snapshots['paired_full_bracket_ns']['max_abs_ns'])}/{fmt(snapshots['native_bracket_ns']['max_abs_ns'])}ns. "
                f"Common phase predictions {pair['common_predictions']}. The realtime value belongs to its own RAW pair. "
                'All unavailable-source snapshots remain; smaller cross-clock brackets do not bound USB event timing.',''])
        local=r['local_estimate_diagnostic']
        if local['enabled']:
            lines.extend(['Optional1Hz UTE1 local-only diagnostics: `'+json.dumps(local['counts'],sort_keys=True)+'`. '
                'Full per-snapshot source/boot/path scope, discipline/session tick ages, original request brackets and sequential strict-map states remain in JSON. '
                'A retained estimate cannot restore strict admission, refresh expiry or identify an RF/GNSS cause.',''])
        h=r['host']['experimental_robust_host']
        if h['enabled']:
            lines.extend(['Experimental '+h['method']+': `'+json.dumps(h['counts'],sort_keys=True)+'`. '
                f"Control window {h['control_maximum_training_points']}points. "
                f"Common-time forecasts {h['common_denominator']}; training-selected {h['training_selected_denominator']}; "
                f"widest withheld complete bracket {fmt(h['widest_training_withheld_bracket_ns'])}ns. "
                'All forecast errors, including training exclusions, are scored below. Selected diagnostics use a matched OLS subset; '
                'neither subset establishes physical timestamp accuracy.','',
                '| Host midpoint forecast diagnostic (ns) | n | Bias | SD | RMS | abs P99 | abs max | >1µs | >2µs | >5µs |',
                '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |',
                metrics('Common OLS, all forecasts',h['common_OLS']),
                metrics('Common-window OLS, all forecasts',h['common_window_OLS']),
                metrics('Common robust, all forecasts including exclusions',h['common_robust_all_forecasts']),
                metrics('Matched OLS training-selected subset',h['selected_OLS']),
                metrics('Robust training-selected subset',h['selected_robust']),''])
    lines.extend(['Host frame observations retain full brackets and declared model assumptions; controller/PHY and UTC bias remain unknown. '
        'GPIO cohort assignment uses bracketing cached source-status snapshots, not exact electrical-event lineage. '
        'Near1ms rejected diagnostics remain failures in availability; they are not replaced by zero or silently trimmed.'])
    return '\n'.join(lines)+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('inputs',nargs='+',type=Path);p.add_argument('--out',type=Path,required=True)
    calibration=p.add_mutually_exclusive_group();calibration.add_argument('--calibration',type=Path);calibration.add_argument('--no-calibration',action='store_true')
    p.add_argument('--host-filter',choices=('none','robust-host-v1','weighted-host-v2'),default='none')
    args=p.parse_args();out=args.out.resolve()
    correction=None if args.no_calibration else validate_calibration(json.loads(args.calibration.read_text()) if args.calibration else DEFAULT_CALIBRATION)
    if out.exists() or not out.is_relative_to(ROOT/'artifacts'):raise ValueError('choose a new ignored artifacts/ report')
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    frozen=dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),source_snapshot=sources,
        implementation={name:hashlib.sha256(value.encode()).hexdigest() for name,value in sources.items()},
        calibration_configuration=correction,experimental_host_filter=args.host_filter)
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(frozen,f,indent=2)
    results=[];identities=set();counts=Counter()
    for path in args.inputs:
        path=path.resolve();document=json.loads(path.read_text());raw=document.get('raw',document)
        identity=(raw.get('boot_id'),raw['start']['raw_before_ns'],raw['end']['raw_after_ns'])
        if identity in identities:raise ValueError('duplicate capture/replay selected')
        identities.add(identity)
        for previous in results:
            boot,lo,hi=previous['capture_identity']
            if boot==identity[0] and max(lo,identity[1])<min(hi,identity[2]):raise ValueError('overlapping capture windows cannot be accumulated')
        a=analyse(raw,calibration=correction,host_filter=args.host_filter);result=capture_quality(path,raw,a);results.append(result)
        for key in ('attempted','source_valid','source_unavailable'):counts[key]+=a['attempts'].get(key,0)
        for key in ('accepted','warmup','predicted'):counts[key]+=a['source']['counts'].get(key,0)
        counts['valid_flag_withheld']+=a['attempts'].get('source_valid',0)-a['source']['counts'].get('accepted',0)
        counts['capture_errors']+=len(raw['errors'])
        print(Path(path).name,result['source_counts'],flush=True)
    report=dict(created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),license='CC BY 4.0',
        selection='all explicit non-overlapping captures, no error-magnitude selection or600s gate',
        cumulative_counts=dict(counts),captures=results,qualified_parity=False,
        limits='Conditional host model and biased phase are not physical timestamp/UTC accuracy; no cross-cohort quality pooling.')
    with out.open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    with out.with_suffix('.md').open('x') as f:f.write(markdown(report))
    print(json.dumps(dict(report=str(out),cumulative_counts=dict(counts))),flush=True)


if __name__=='__main__':main()
