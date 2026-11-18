#!/usr/bin/env python3
"""Render an offline paired shadow report with every metric denominator.

Copyright (c) 2026 Jonathan Yantis. MIT. No hardware or clock operations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.analyse import analyse
from gateways.pi5.usb.calibration import DEFAULT as DEFAULT_CALIBRATION,validate as validate_calibration


def number(value):return 'unknown' if value is None else f'{value:.3f}'


def table(rows):
    lines=['| Metric | n | Bias ns | RMS ns | SD ns | abs P99 ns | abs max ns | >1µs | >2µs | >5µs |',
           '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for name,s in rows:
        lines.append('| '+name+' | '+str(s['n'])+' | '+' | '.join(number(s.get(k)) for k in
            ('mean_ns','rms_ns','sigma_ns','p99_abs_ns','max_abs_ns','above_1us','above_2us','above_5us'))+' |')
    return lines


def render(raw,a,input_path,input_hash):
    g=a['simultaneous_GPIO'];host=a['host'];marker=a['marker_identity'];d=a['denominators']
    lines=['# Paired USB shadow observation','',
        'Jonathan Yantis. Derived observation report: CC BY 4.0. Private, unreviewed hardware evidence.','',
        f'Input: `{input_path}`; SHA256 `{input_hash}`. Offline analysis; no device opened.','',
        f"Fixed requested duration {raw['requested_seconds']}s; elapsed {a['observer']['elapsed_s']:.6f}s; "
        f"USB rate {raw['rate']}Hz; mode {raw['mode']}; host read method {raw.get('host_read_method','seek-read')}. "
        f"Capture errors {len(raw['errors'])}; interface released {raw['interface_released']}.",'',
        'The Pi system clock is disciplined by M-HAT. USB/system agreement is a dependent shadow comparison, '
        'not independent UTC accuracy. USB/M-HAT parity is **not demonstrated**. No source promotion is authorized by this report.','',
        '## Availability and provenance','',
        '| Population | Count |','| --- | ---: |']
    labels={'attempted':'All USB SOF attempts','wire_source_valid':'Decoded source-valid flag',
        'wire_source_unavailable':'Decoded source unavailable','source_admitted':'Host-admitted source measurements',
        'source_warmup':'Admitted warmup','source_causal_predicted':'Source causal predictions',
        'valid_flag_withheld_by_host':'Valid flag but host withheld',
        'conditional_offset_admitted':'Conditional host offset, admitted',
        'conditional_offset_withheld_diagnostic':'Conditional host offset, withheld diagnostics',
        'valid_without_conditional_offset':'Source-valid without conditional host offset'}
    for key,label in labels.items():lines.append(f'| {label} | {d[key]} |')
    capture=a['captured_tuple_diagnostic']['counts']
    lines.extend([f"| Raw SOF tuples, including source-unavailable | {capture.get('captured_tuple',0)} |",
        f"| Adjacent raw tuple transitions compared / disagreeing | {capture.get('compared_transitions',0)} / {capture.get('counter_frame_mismatch',0)} |"])
    lines.append(f"| Valid full source cohorts / paths | {a['source_coherence']['valid_full_source_cohorts']} / {a['source_coherence']['valid_paths']} |")
    lines.extend([f"| Current-counter decoded / valid / unavailable | {a['attempts'].get('current_decoded',0)} / {a['attempts'].get('current_valid',0)} / {a['attempts'].get('current_unavailable',0)} |",
        f"| M-HAT status / eligible | {g['status_observations']} / {g['eligible_status_observations']} |",
        f"| New GPIO pulses / source-eligible pulses | {g['new_edges']['n']} / {g['source_eligible_edges']['n']} |",
        f"| Cached seed assertions (excluded from new pulses) | {g['cached_seeds']} |",
        f"| PPS gaps | {len(g['gaps'])} |",
        f"| Sampled common GM/root/boot/epoch | {a['sampled_lineage_join'].get('MATCHING_SAMPLED_GM_ROOT_BOOT_EPOCH',0)} |",'',
        'All original packets, unavailable states, rejected diagnostics, training prefixes and gaps remain in the input. '
        'Sampled lineage matching does not prove exact-event lineage. Source/map/path/boot epochs remain segmented.','',
        'Sequential current/SOF map coverage: `'+json.dumps(a['source_coherence']['paired_current_SOF_map_coverage'],sort_keys=True)+'`. '
        'A source publication can change between these two requests. Raw captured-tuple continuity is independent of '
        'map permission; disagreeing adjacent transitions retain both endpoints without selecting a correct packet or granting UTC authority.','',
        'Host source outcomes: `'+json.dumps(a['source']['counts'],sort_keys=True)+'`.','',
        '## M-HAT GPIO phase against its disciplined system clock',''])
    frozen=dict(n=600,rms_ns=872.002,p99_abs_ns=4809,max_abs_ns=7017,above_1us=46,above_2us=19,above_5us=5)
    lines+=table([('Contemporaneous all new GPIO',g['new_edges']),('Contemporaneous eligible GPIO',g['source_eligible_edges']),('Separate published600s baseline',frozen)])
    lines.extend(['','The published window remains a separate historical control, not a replacement for this population. '
        'Bias and SD are unknown in this compact baseline record. No feedback-related metric is an independent output measurement.','',
        '## Causal source residuals','',
        'Each prediction uses the preceding fit. Device SOF-ordinal residuals omit host-clock phase. '
        'Conditional RAW residuals include an unqualified spec mapping and fitted host phase. Neither metric is '
        'equivalent to the historical macOS SOF/Mach experiment or independent UTC accuracy.',''])
    rows=[]
    for name,series in [('Device SOF ordinal',a['source']),('Conditional RAW',a['conditional_spec_shadow']['source_causal'])]:
        for i,segment in enumerate(series['segments']):rows.append((f'{name}, segment{i+1}',segment['errors']))
    lines+=table(rows)
    lines.extend(['','## Conditional USB-minus-system offsets','',
        'Physical controller/PHY phase is unknown. These uncalibrated offsets and their constant biases are retained; '
        'the fixed provisional correction is shown separately. Withheld diagnostics are not admitted timing samples, '
        'and still count against availability.',''])
    phase_rows=[]
    for i,c in enumerate(a['conditional_spec_shadow']['offset_cohorts']):
        phase_rows.extend([(f"Cohort{i+1} {c['measurement_state']} RAW",c['USB_minus_system']),
            (f"Cohort{i+1} {c['measurement_state']} FIXED−25,600ns",c['provisional_USB_minus_system'])])
    lines+=table(phase_rows)
    lines.extend(['','Fixed provisional calibration: `'+json.dumps(a['provisional_calibration'],sort_keys=True)+'`. '
        'The basis is dependent on the M-HAT-disciplined system, frozen and not retuned for later methods, cohorts or holdouts. '
        'Raw source/maps, authority and expiry remain unchanged; absolute uncertainty stays unknown.'])
    local=a['local_estimate_diagnostic']
    if local['enabled']:
        lines.extend(['','## Optional local-estimate diagnostics','',
            'Sequential1Hz UTE1 snapshots: `'+json.dumps(local['counts'],sort_keys=True)+'`. '
            'The raw input retains every source/boot/path scope, original discipline/session-until tick and request bracket. '
            'Retained local estimates never restore strict admission or refresh a deadline. Session/root flags describe '
            'current local state; a false flag does not establish an RF/GNSS cause.'])
    filtered=a['experimental_host_filter']
    if filtered['enabled']:
        h=host['experimental_robust_host']
        lines.extend(['','## Experimental causal host filter','',
            filtered['method']+'; common-time control maximum '+str(h['control_maximum_training_points'])+'points. '
            'Current innovations are scored before training; every excluded or downweighted point remains in the all-forecast population. '
            'No USB/M-HAT phase trains the host model or changes the frozen correction. Counts: `'+json.dumps(h['counts'],sort_keys=True)+'`.',''])
        lines+=table([('Common original OLS, all forecasts',h['common_OLS']),
            ('Common-window OLS, all forecasts',h['common_window_OLS']),
            ('Experimental host model, all forecasts',h['common_robust_all_forecasts']),
            ('Matched OLS selected training subset',h['selected_OLS']),
            ('Experimental selected training subset',h['selected_robust'])])
        phase=[]
        for i,c in enumerate(filtered['phase_cohorts']):
            label=f"Cohort{i+1} {c['measurement_state']} n{c['common_denominator']}"
            phase.extend([(label+' matched OLS fixed offset',c['common_OLS_provisional_USB_minus_system']),
                (label+' matched-window OLS fixed offset',c['common_window_OLS_provisional_USB_minus_system']),
                (label+' filtered raw',c['filtered_USB_minus_system']),
                (label+' filtered fixed offset',c['filtered_provisional_USB_minus_system'])])
        lines.extend(['','Common-time source phase estimates follow; excluded/missing predictions remain in availability, '
            'and withheld frame/counter diagnostics remain withheld. Unknown physical phase and source authority are unchanged.',''])
        lines+=table(phase)
    lines.extend(['','## Host frame association limits','',
        f"Markers: {a['attempts'].get('marker_attempted',0)} attempted; {marker.get('counts',{})}; "
        f"integer origin constrained {marker['integer_origin_constrained']}; candidates {marker.get('candidates')}. "
        'Integer identity does not qualify physical phase.','',
        'Complete transition bracket widths: '+json.dumps(host['transition_bracket_width_ns'],sort_keys=True)+'.','',
        'Midpoint OLS remains a diagnostic; every endpoint and out-of-sample failure is retained. Interval models use '
        'a declared ±1000ppm RAW/USB rate scenario, not a calibrated oscillator bound. Overlap is nonfalsification, '
        'not actual-event coverage. Controller-to-wire bias and absolute uncertainty remain unknown.','',
        '| Causal host model | Predictions | Overlap | Entire observed bracket contained | Disjoint | Max predicted width ns |',
        '| --- | ---: | ---: | ---: | ---: | ---: |'])
    for label,key in [('Bounded rate','bounded_rate_intervals'),('Affine trial','affine_set_intervals')]:
        v=host[key];c=v['all_prior_model_predictions']
        lines.append(f"| {label} | {c['n']} | {c['overlap_not_falsified']} | {c['contains_complete_observation_bracket']} | {c['disjoint']} | {number(v['predicted_width_ns']['max_abs_ns'])} |")
    lines.extend(['',f"Observer CPU {a['observer']['cpu_one_core_percent']:.4f}% of one core; register reads "
        f"{a['observer']['register_reads']} ({a['observer']['register_reads_per_s']:.3f}/s). "
        'Existing clock/collector/PTP service snapshots and workload/IRQ metadata remain in the raw input.','',
        'Full observer CPU percent (including preparation/cleanup and child tasks where recorded): '
        +number(a['observer'].get('full_observer_CPU_one_core_percent'))+'. Native compiler startup wall ns: '
        +str(a['observer'].get('native_compile_startup_wall_ns',0))+'; compiler child CPU ns: '
        +str(a['observer'].get('native_compiler_child_cpu_ns',0))+'. Legacy missing child costs remain unknown.','',
        'Next discriminating measurements: a validated narrower stock host read bracket, repeated causal association '
        'through source recovery, and an independent physical reference for USB controller/PHY and PPS-to-UTC bias.'])
    return '\n'.join(lines)+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--host-filter',choices=('none','robust-host-v1','weighted-host-v2'),default='none')
    calibration=p.add_mutually_exclusive_group();calibration.add_argument('--calibration',type=Path);calibration.add_argument('--no-calibration',action='store_true')
    args=p.parse_args();out=args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts'):raise ValueError('choose a new ignored artifacts/ report')
    content=args.input.read_bytes();document=json.loads(content);raw=document.get('raw',document)
    correction=None if args.no_calibration else validate_calibration(json.loads(args.calibration.read_text()) if args.calibration else DEFAULT_CALIBRATION)
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(source_snapshot=sources,
        hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()},
        calibration_configuration=correction,host_filter=args.host_filter),f,indent=2)
    a=analyse(raw,calibration=correction,host_filter=args.host_filter);report=render(raw,a,args.input.resolve(),hashlib.sha256(content).hexdigest())
    with out.open('x') as f:f.write(report)
    print(out)


if __name__=='__main__':main()
