#!/usr/bin/env python3
"""Offline token-bound SOF/controller origin intervals, never a time source.

Copyright (c) 2026 Jonathan Yantis. MIT. No USB/network/clock operations.
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
from gateways.pi5.usb.clock_pair import realtime_interval
from gateways.pi5.usb.contract import decode,scope
from gateways.pi5.usb.interval import AffineBrackets
from gateways.pi5.usb.model import statistics

ORIGINS=(-1,0,1)
PROFILES=('OUT_START_TO_MATCHING_IN_COMPLETION','OUT_ONLY_CONDITIONAL_COMPLETION_ORDER')


def snapshot_interval(anchor,marker,profile):
    if profile not in PROFILES:raise ValueError('unknown request enclosure profile')
    if not anchor['valid'] or anchor['contract']!='UFA1' or anchor['token']!=marker['token'] or marker.get('error') or marker.get('out_length')!=0 or marker.get('length')!=64:
        raise ValueError('invalid token-bound immutable marker')
    previous=None
    for key in ('out_before','out_after','in_before','in_after'):
        c=marker[key];realtime_interval(c)
        if previous is not None and c['raw_before_ns']<previous:raise ValueError('request clock brackets overlap or roll back')
        previous=c['raw_after_ns']
    age=anchor['current_tick']-anchor['tick']
    if not 0<=age<16000:raise ValueError('invalid original SOF age')
    end=marker['in_after' if profile==PROFILES[0] else 'out_after']['raw_after_ns']
    if end-marker['out_before']['raw_before_ns']>100_000_000:raise ValueError('request enclosure ambiguous across frames')
    # Half-nanosecond arithmetic is exact. The extra margin includes the
    # declared1000ppm age-rate scenario, one63ns counter tick and1ns clock grid.
    elapsed_twice=age*125;margin_twice=-(-elapsed_twice*1000//1_000_000)+128
    return dict(low_raw_twice_ns=2*marker['out_before']['raw_before_ns']-elapsed_twice-margin_twice,
        high_raw_twice_ns=2*end-elapsed_twice+margin_twice,original_tick=anchor['tick'],current_tick=anchor['current_tick'],
        request_enclosure_ns=end-marker['out_before']['raw_before_ns'],age_ticks=age,
        age_rate_scenario_ppm=1000,rate_physically_calibrated=False,source_permission=False)


class OriginIntersection:
    """Conditional constant-origin set; a contradiction stays infeasible."""
    def __init__(self):self.bounds=None;self.n=0;self.infeasible=False
    def observe(self,lo,hi):
        if any(type(v) is not int for v in (lo,hi)) or hi<lo:raise ValueError('invalid origin interval')
        prior=self.bounds;prediction=prior if self.n>=16 and not self.infeasible else None
        row=dict(prior_low_twice_ns=prediction[0] if prediction else None,prior_high_twice_ns=prediction[1] if prediction else None,
            prior_prediction_available=prediction is not None,prior_points=self.n,
            overlaps_current_interval=None if prediction is None else max(prediction[0],lo)<=min(prediction[1],hi),
            contains_complete_current_interval=None if prediction is None else prediction[0]<=lo and prediction[1]>=hi)
        self.n+=1
        if not self.infeasible:
            updated=(lo,hi) if prior is None else (max(lo,prior[0]),min(hi,prior[1]))
            if updated[0]>updated[1]:self.infeasible=True;self.bounds=None
            else:self.bounds=updated
        row['infeasible_after_observation']=self.infeasible
        return row


def evaluate(raw):
    a=analyse(raw,calibration=None)
    if any(r.get('host_boundary_reset') for r in a['source_rows']):raise ValueError('split suspend/clock cohorts before origin analysis')
    transitions=a['host_transitions'];pointer=0;host=AffineBrackets();host_segment=0;previous_host=None
    groups={};rows=[];counts=Counter();last_marker={};blocked=False
    expected_boot=next((r['source_record']['boot'] for r in a['source_rows'] if 'source_record' in r),None)
    for sample in raw['samples']:
        if 'marker' not in sample:continue
        counts['marker_attempted']+=1;m=sample['marker'];row=dict(index=sample['index']);rows.append(row)
        try:
            if blocked:raise ValueError('capture marker association fenced after an earlier rejection')
            anchor=decode(bytes.fromhex(m['wire_hex']),raw['device']['silicon']);row['anchor_record']=anchor
            if anchor['contract']!='UFA1' or expected_boot is not None and anchor['boot']!=expected_boot:raise ValueError('marker contract/MCU boot changed; fresh capture required')
            cut=m['out_before']['raw_before_ns']
            while pointer<len(transitions) and transitions[pointer]['high_raw_ns']<=cut:
                t=transitions[pointer];pointer+=1;x=t['ordinal']
                if previous_host and (x<=previous_host['ordinal'] or t['low_raw_ns']-previous_host['high_raw_ns']>500_000_000):
                    host=AffineBrackets();host_segment+=1
                host.observe(x,t['low_raw_ns'],t['high_raw_ns']);previous_host=t
            physical=scope(anchor);old=last_marker.get(physical)
            if old and (anchor['sequence']<=old['sequence'] or anchor['current_tick']<=old['current_tick']):raise ValueError('stale marker sequence/counter in unchanged epoch')
            enclosures={profile:snapshot_interval(anchor,m,profile) for profile in PROFILES}
            last_marker[physical]=anchor
            if not host.points:raise ValueError('no preceding host transition')
            near=host.points[-1][0];base=anchor['frame']*8;wrap=(near-base+8192)//16384;frame=base+wrap*16384
            if abs(frame-near)>=4000:raise ValueError('frame wrap not nearby')
            key=(physical,host_segment)
            group=groups.setdefault(key,dict(scope=list(physical),host_segment=host_segment,
                tracker={(profile,origin):OriginIntersection() for profile in PROFILES for origin in ORIGINS}))
            row.update(host_segment=host_segment,host_training_points=len(host.points),host_model_frozen_before_OUT=True,scenarios=[])
            for profile,enclosure in enclosures.items():
                for origin in ORIGINS:
                    event=frame+8*origin;prediction=host.predict(event)
                    if prediction is None:continue
                    lo=enclosure['low_raw_twice_ns']-2*prediction[1];hi=enclosure['high_raw_twice_ns']-2*prediction[0]
                    scored=group['tracker'][(profile,origin)].observe(lo,hi)
                    row['scenarios'].append(dict(profile=profile,integer_frame_origin=origin,
                        combined_device_capture_minus_MFI_low_twice_ns=lo,combined_device_capture_minus_MFI_high_twice_ns=hi,
                        MFI_ordinal=event,MFI_low_raw_ns=prediction[0],MFI_high_raw_ns=prediction[1],
                        original_SOF_enclosure=enclosure,constant_origin_prior=scored,
                        grants_time_authority=False,absolute_uncertainty_ns=None))
            counts['marker_decoded_valid']+=1
            if not row['scenarios']:counts['host_model_warmup']+=1
        except (ValueError,KeyError) as e:
            row['rejected']=str(e);counts['rejected']+=1
            if str(e)!='no preceding host transition':blocked=True
    summaries=[]
    for key,group in groups.items():
        scenarios=[]
        for (profile,origin),tracker in group.pop('tracker').items():
            entries=[s for r in rows if r.get('anchor_record') and scope(r['anchor_record'])==key[0] and r.get('host_segment')==key[1] for s in r.get('scenarios',[]) if s['profile']==profile and s['integer_frame_origin']==origin]
            prior=[e['constant_origin_prior'] for e in entries if e['constant_origin_prior']['prior_prediction_available']]
            scenarios.append(dict(profile=profile,integer_frame_origin=origin,observations=tracker.n,infeasible=tracker.infeasible,
                retrospective_intersection_low_twice_ns=tracker.bounds[0] if tracker.bounds else None,
                retrospective_intersection_high_twice_ns=tracker.bounds[1] if tracker.bounds else None,
                retrospective_width_ns=(tracker.bounds[1]-tracker.bounds[0])/2 if tracker.bounds else None,
                prior_predictions=len(prior),prior_overlap=sum(p['overlaps_current_interval'] for p in prior),
                prior_contains_complete=sum(p['contains_complete_current_interval'] for p in prior),
                observed_origin_interval_width_ns=statistics([(e['combined_device_capture_minus_MFI_high_twice_ns']-e['combined_device_capture_minus_MFI_low_twice_ns'])/2 for e in entries])))
        summaries.append(dict(**group,scenarios=scenarios))
    return dict(schema=1,mode='OFFLINE_CONDITIONAL_ORIGIN_ENCLOSURE',counts=dict(counts),groups=summaries,rows=rows,
        source_counts=a['source']['counts'],attempts=a['attempts'],source_permission_or_calibration_changed=False,
        frozen_shadow_correction_ns=-25600,shadow_correction_applied_to_RAW_origin_bounds=False,
        assumptions=dict(integer_frame_origins=list(ORIGINS),host_affine_window=64,host_minimum_points=16,age_rate_scenario_ppm=1000,
            physical_rate_or_constant_origin_qualified=False,constant_origin_intersections_are_retrospective=True,
            OUT_only_profile_needs_separate_completion_order_proof=True,matching_IN_profile_contains_both_requests=True),
        absolute_uncertainty_ns=None,chrony_eligible=False,parity_demonstrated=False,
        limits='Original device SOF ticks are retained; request/arrival midpoints are not event timestamps. Controller/wire/capture bias is combined and conditional. A constant integer-frame shift can be absorbed by origin bias; no physical bias prior means these scenarios do not independently choose an integer origin. An empty intersection falsifies that scenario and is never repaired by deleting a marker.')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts') or not out.parent.is_dir():raise ValueError('new ignored artifacts output required')
    sources={f.name:f.read_text() for pattern in ('*.py','*.c','*.json') for f in Path(__file__).parent.glob(pattern)}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),source_snapshot=sources,hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}),f,indent=2)
    data=a.input.read_bytes();document=json.loads(data);report=evaluate(document.get('raw',document))
    report.update(input=str(a.input.resolve()),input_sha256=hashlib.sha256(data).hexdigest(),created_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),observation_license='CC BY 4.0')
    with out.open('x') as f:json.dump(report,f,indent=2,allow_nan=False);f.write('\n')
    lines=['# Conditional token/SOF origin enclosures','',
        'Jonathan Yantis. CC BY 4.0 derived observations; private engineering evidence. Saved-data-only.','',
        'The device snapshot is enclosed by OUT-start through matching-IN completion. The OUT-only alternative additionally assumes '
        'handler/completion ordering. Original SOF counters are projected backward by their recorded age with an uncalibrated±1000ppm '
        'rate scenario and explicit tick/grid margin. MFINDEX affine intervals use only complete transitions preceding that OUT request.','',
        'Constant-origin intersections below are retrospective hypothesis sets, not event-error measurements or an independent physical calibration. '
        'The−25,600ns shadow correction is unchanged and is not applied to these RAW-domain origin intervals. '
        'A different integer-frame origin shifts the unknown combined bias; no scenario is selected by a small interval or midpoint.','',
        '| Physical epoch group / enclosure | Assumed integer origin | Marker intervals | Retrospective low/high (µs) | Width (µs) | Prior predictions / overlap / full contain | Infeasible |',
        '| --- | ---: | ---: | --- | ---: | --- | --- |']
    for i,g in enumerate(report['groups']):
        for s in g['scenarios']:
            def f(v,scale):return 'unknown' if v is None else f'{v/scale:.3f}'
            lines.append('| G'+str(i+1)+' '+s['profile']+' | '+str(s['integer_frame_origin'])+' | '+str(s['observations'])+' | '+
                f(s['retrospective_intersection_low_twice_ns'],2000)+' / '+f(s['retrospective_intersection_high_twice_ns'],2000)+' | '+
                f(s['retrospective_width_ns'],1000)+' | '+str(s['prior_predictions'])+' / '+str(s['prior_overlap'])+' / '+str(s['prior_contains_complete'])+' | '+str(s['infeasible'])+' |')
    lines.extend(['',report['limits'],''])
    with out.with_suffix('.md').open('x') as f:f.write('\n'.join(lines))
    print(json.dumps(dict(report=str(out),counts=report['counts'],groups=len(report['groups']))))


if __name__=='__main__':main()
