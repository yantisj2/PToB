#!/usr/bin/env python3
"""Offline affine host-enclosure window sensitivity; no hardware access.

Copyright (c) 2026 Jonathan Yantis. MIT. Conditional models are not accuracy.
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
from gateways.pi5.usb.interval import AffineBrackets
from gateways.pi5.usb.model import statistics

WINDOWS=(64,128,256,512)


def score(transitions,windows=WINDOWS):
    if not windows or len(set(windows))!=len(windows):raise ValueError('distinct declared windows required')
    models={w:AffineBrackets(maximum=w) for w in windows};rows=[];previous=None;boundaries=[];resets=Counter()
    for i,t in enumerate(transitions):
        x,lo,hi=(t[k] for k in ('ordinal','low_raw_ns','high_raw_ns'))
        if any(type(v) is not int for v in (x,lo,hi)) or not 0<=lo<=hi:raise ValueError('invalid complete host bracket')
        if previous and (x<=previous[0] or hi<previous[2] or lo-previous[2]>500_000_000):
            boundaries.append(i);models={w:AffineBrackets(maximum=w) for w in windows}
        r=dict(index=i,ordinal=x,low_raw_ns=lo,high_raw_ns=hi,acquisition=t.get('acquisition',True),
            causal_OLS_midpoint_error_ns=t.get('causal_midpoint_error_ns'),models={})
        for w,model in models.items():
            points=len(model.points);span=model.points[-1][2]-model.points[0][1] if points else 0
            before=model.counts['inconsistent_reset'];prediction=model.observe(x,lo,hi)
            reset=model.counts['inconsistent_reset']-before;resets[w]+=reset
            r['models'][w]=dict(prediction=prediction,inconsistent_reset_after_score=bool(reset),
                preceding_points=points,preceding_span_ns=span)
        rows.append(r);previous=(x,lo,hi)
    def coverage(selected,w):
        scored=[r for r in selected if r['models'][w]['prediction'] is not None]
        predictions=[r['models'][w]['prediction'] for r in scored]
        return dict(denominator=len(selected),predicted=len(predictions),missing=len(selected)-len(predictions),
            overlap_not_falsified=sum(p['overlaps_complete_read_bracket'] for p in predictions),
            contains_complete_bracket=sum(p['contains_complete_read_bracket'] for p in predictions),
            disjoint=sum(not p['overlaps_complete_read_bracket'] for p in predictions),
            predicted_width_ns=statistics([p['width_ns'] for p in predictions]),
            maximum_actual_preceding_points=max((r['models'][w]['preceding_points'] for r in scored),default=0),
            preceding_span_ns=statistics([r['models'][w]['preceding_span_ns'] for r in scored]),
            disjoint_distance_ns=statistics([p['disjoint_distance_ns'] for p in predictions]),actual_event_coverage='UNKNOWN_WITHIN_COMPLETE_BRACKET')
    common=[r for r in rows if all(r['models'][w]['prediction'] is not None for w in windows)]
    acquisition=[r for r in rows if r['acquisition']];common_acquisition=[r for r in common if r['acquisition']]
    return dict(windows=list(windows),transitions=len(rows),acquisition_transitions=len(acquisition),
        common_all_windows=len(common),common_all_windows_acquisition=len(common_acquisition),boundaries=boundaries,
        common_acquisition_OLS_midpoint_diagnostic_ns=statistics([r['causal_OLS_midpoint_error_ns'] for r in common_acquisition if r['causal_OLS_midpoint_error_ns'] is not None]),
        models={w:dict(inconsistent_resets=resets[w],all=coverage(rows,w),acquisition=coverage(acquisition,w),
            common_all_windows=coverage(common,w),common_all_windows_acquisition=coverage(common_acquisition,w)) for w in windows},rows=rows,
        assumptions=dict(constant_period_over_each_preceding_window=True,rate_ratio_scenario_ppm=1000,
            rate_or_drift_physically_calibrated=False,minimum_points=16,endpoint_extra_outward_ns=1,
            numeric_clipping_not_formally_certified=True,current_observation_not_used_in_its_prediction=True),
        physical_controller_to_wire_bias_ns=None,absolute_uncertainty_ns=None,chrony_eligible=False,
        interpretation='Overlap cannot establish coverage. Wider windows strengthen an unqualified constant-rate assumption; every miss/reset remains. No midpoint or observed envelope width bounds UTC/PHY error.')


def evaluate(raw,windows=WINDOWS):
    a=analyse(raw,calibration=None)
    if any(r.get('host_boundary_reset') for r in a['source_rows']):
        raise ValueError('split host suspend/clock cohorts explicitly before window sensitivity analysis')
    transitions=[dict(t,acquisition=t['high_raw_ns']>=raw['start']['raw_before_ns']) for t in a['host_transitions']]
    result=score(transitions,windows);result.update(source_counts=a['source']['counts'],attempts=a['attempts'],
        host_read_method=raw.get('host_read_method','seek-read'),realtime_bracket_method=raw.get('realtime_bracket_method','legacy-wide'),
        original_complete_brackets=a['host']['transition_bracket_width_ns'],host_preparation=a['host_preparation_warmup'])
    return result


def markdown(report):
    lines=['# Conditional affine enclosure sensitivity','',
        'Jonathan Yantis. CC BY 4.0 derived observations; private engineering evidence. Saved data only.','',
        'Windows64/128/256/512 and±1000ppm are declared sensitivity scenarios. Constant rate over longer history is an assumption, '
        'not an oscillator calibration. Predictions precede current brackets; every missing prediction, miss, reset and wide interval remains. '
        'Overlap does not establish event coverage, and no result includes physical controller/wire or UTC bias.','',
        '| Window cap / actual max points | Max preceding span (s) | Common acquisition predictions | Mean predicted width (ns) | P99 width | Max width | Disjoint | Contains whole observed bracket | All-history resets |',
        '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for w in report['windows']:
        model=report['models'][w];m=model['common_all_windows_acquisition'];s=m['predicted_width_ns']
        def f(v):return 'unknown' if v is None else f'{v:.3f}'
        span=m['preceding_span_ns']['max_abs_ns']
        lines.append('| '+str(w)+' / '+str(m['maximum_actual_preceding_points'])+' | '+f(span/1e9 if span is not None else None)+' | '+str(m['predicted'])+' | '+f(s['mean_ns'])+' | '+f(s['p99_abs_ns'])+' | '+f(s['max_abs_ns'])+' | '+str(m['disjoint'])+' | '+str(m['contains_complete_bracket'])+' | '+str(model['inconsistent_resets'])+' |')
    return '\n'.join(lines)+'\n'


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts') or not out.parent.is_dir():raise ValueError('new ignored artifacts output required')
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),source_snapshot=sources,
        hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()},declared_windows=list(WINDOWS)),f,indent=2)
    data=a.input.read_bytes();document=json.loads(data);raw=document.get('raw',document);started=time.monotonic_ns();cpu=time.process_time_ns()
    result=evaluate(raw);result.update(input=str(a.input.resolve()),input_sha256=hashlib.sha256(data).hexdigest(),
        created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),observation_license='CC BY 4.0',
        offline_cost=dict(elapsed_ns=time.monotonic_ns()-started,process_CPU_ns=time.process_time_ns()-cpu,live_observer_load=False))
    with out.open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    with out.with_suffix('.md').open('x') as f:f.write(markdown(result))
    print(json.dumps(dict(report=str(out),transitions=result['transitions'],common_acquisition=result['common_all_windows_acquisition'])))


if __name__=='__main__':main()
