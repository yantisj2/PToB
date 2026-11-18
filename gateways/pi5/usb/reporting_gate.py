#!/usr/bin/env python3
"""Offline conditional shadow reporting readiness; no device or clock operations.

Copyright (c) 2026 Jonathan Yantis. MIT. Historical classifications stay intact.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
import math
from pathlib import Path
import time

from .contract import scope
from .model import statistics

POLICY=dict(stable_ns=1_000_000_000,minimum_ready_observations=9,
            maximum_gap_ns=500_000_000,association_max_age_ns=1_500_000_000,
            required_source_training_points=64)
PHYSICAL=('silicon','boot','usb_epoch','capture_epoch')


def finite(x):return type(x) in (int,float) and math.isfinite(x)


class ReportingGate:
    """Past-only presentation gate, never source permission or event correction."""
    def __init__(self,policy=None,expected_silicon=None):
        self.policy=copy.deepcopy(POLICY if policy is None else policy)
        if self.policy!=POLICY:raise ValueError('use the frozen reporting policy')
        self.expected_silicon=expected_silicon
        self.context=None;self.last_request=None;self.association=None
        self.ready_since=None;self.ready_count=0

    def clear(self):self.ready_since=None;self.ready_count=0

    def observe(self,sample,row):
        row=row or {};d=row.get('source_record',{});s=row.get('source',{})
        p=row.get('native_realtime_pair_shadow',{});request=sample.get('source',{})
        before=request.get('before',{}).get('raw_before_ns')
        after=request.get('after',{}).get('raw_after_ns')
        bracket=type(before) is int and type(after) is int and 0<=before<=after
        resets=[];ctx=None
        if d.get('valid'):
            try:ctx=scope(d)
            except KeyError:pass
            if ctx is not None and any(x is None for x in ctx):ctx=None
            if ctx is not None and ctx!=self.context:
                resets.append('SOURCE_SCOPE_CHANGE');self.context=ctx
        if row.get('host_boundary_reset'):resets.append('HOST_CLOCK_BOUNDARY')
        if bracket:
            if self.last_request is not None and not 0<before-self.last_request<=self.policy['maximum_gap_ns']:
                resets.append('HOST_GAP_OR_ROLLBACK')
            self.last_request=before
        if 'marker' in sample:
            marker=row.get('marker_record',{});state=row.get('marker_identity',{})
            at=sample['marker'].get('in_after',{}).get('raw_after_ns')
            if marker.get('valid') and state.get('state')=='INTEGER_ORIGIN_CONSTRAINED' and type(at) is int:
                self.association=dict(physical=tuple(marker.get(k) for k in PHYSICAL),at=at)
            else:
                self.association=None;resets.append('ASSOCIATION_NOT_READY')
        if resets:self.clear()
        reason=None
        if not row:reason='MISSING_ANALYSIS'
        elif not bracket:reason='MISSING_REQUEST_BRACKET'
        elif d.get('contract')!='UTS1' or d.get('kind')!=1:reason='SOURCE_CONTRACT_INVALID'
        elif self.expected_silicon is not None and d.get('silicon')!=self.expected_silicon:reason='SOURCE_IDENTITY_MISMATCH'
        elif not d.get('valid'):reason='SOURCE_UNAVAILABLE'
        elif s.get('state')=='WITHHELD':reason='SOURCE_WITHHELD'
        elif ctx is None or tuple(s.get('scope',()))!=ctx:reason='SOURCE_SCOPE_INVALID'
        elif type(s.get('original_deadline_raw_ns')) is not int or after>=s['original_deadline_raw_ns']:
            reason='ORIGINAL_PERMISSION_EXPIRED_OR_MISSING'
        elif s.get('state')=='WARMUP':reason='SOURCE_WARMUP'
        elif s.get('state')!='CAUSAL_RELATIVE_DIAGNOSTIC':reason='SOURCE_NOT_CAUSAL_READY'
        elif not finite(p.get('source_utc_minus_system_ns')):reason='MISSING_NATIVE_PHASE'
        elif not self.association:reason='CONDITIONAL_ASSOCIATION_NOT_READY'
        elif tuple(d.get(k) for k in PHYSICAL)!=self.association['physical']:reason='ASSOCIATION_SCOPE_MISMATCH'
        elif not 0<=after-self.association['at']<=self.policy['association_max_age_ns']:reason='ASSOCIATION_STALE_OR_FUTURE'
        if reason:
            self.clear()
        else:
            if self.ready_since is None:self.ready_since=before
            self.ready_count+=1
            if self.ready_count<self.policy['minimum_ready_observations'] or before-self.ready_since<self.policy['stable_ns']:
                reason='RECOVERY_SETTLING'
        ready=reason is None
        raw=p.get('source_utc_minus_system_ns');fixed=p.get('provisional_source_utc_minus_system_ns')
        return dict(index=sample['index'],observation_raw_ns=before,scope=list(ctx) if ctx else None,
            historical_source_state=s.get('state'),historical_source_reason=s.get('reason'),
            historical_host_phase_state=p.get('source_measurement_state'),
            report_ready=ready,reason=reason or 'REPORT_READY',reset_reasons=resets,
            consecutive_ready_observations=self.ready_count,
            stable_observation_span_ns=before-self.ready_since if bracket and self.ready_since is not None else 0,
            diagnostic_native_raw_ns=raw if finite(raw) else None,
            diagnostic_native_fixed_ns=fixed if finite(fixed) else None,
            report_native_raw_ns=raw if ready else None,
            report_native_fixed_ns=fixed if ready and finite(fixed) else None,
            original_deadline_raw_ns=s.get('original_deadline_raw_ns'),
            reporting_qualification='CONDITIONAL_SHADOW_REPORTING_ONLY',
            chrony_eligible=False,absolute_uncertainty_ns=None)


def evaluate(raw,analysis):
    if analysis.get('source',{}).get('minimum_training_samples')!=64:
        raise ValueError('expected existing64-point source warmup contract')
    expected=raw.get('device',{}).get('silicon')
    if type(expected) is not str or len(expected)!=16 or any(c not in '0123456789abcdef' for c in expected):
        raise ValueError('missing exact captured silicon identity')
    gate=ReportingGate(expected_silicon=expected);by_index={r['index']:r for r in analysis['source_rows']}
    if len(by_index)!=len(analysis['source_rows']):raise ValueError('duplicate analysis index')
    samples=raw['samples']
    if len({s['index'] for s in samples})!=len(samples):raise ValueError('duplicate sample index')
    if set(by_index)-{s['index'] for s in samples}:raise ValueError('analysis outside capture')
    rows=[gate.observe(s,by_index.get(s['index'])) for s in samples]
    counts=Counter(r['reason'] for r in rows);cohorts={};runs=[]
    for r in rows:
        if r['scope']:
            key=tuple(r['scope'])
            if key not in cohorts:cohorts[key]=dict(scope=r['scope'],counts=Counter(),raw=[],admitted=[],ready=[])
            c=cohorts[key];c['counts'][r['reason']]+=1
            if r['diagnostic_native_raw_ns'] is not None:c['raw'].append(r)
            if r['historical_host_phase_state']=='ADMITTED' and r['diagnostic_native_raw_ns'] is not None:c['admitted'].append(r)
            if r['report_ready']:c['ready'].append(r)
        key=(r['report_ready'],r['reason'],r['scope'])
        if not runs or key!=(runs[-1]['report_ready'],runs[-1]['reason'],runs[-1]['scope']):
            runs.append(dict(report_ready=r['report_ready'],reason=r['reason'],scope=r['scope'],
                first_index=r['index'],last_index=r['index'],first_raw_ns=r['observation_raw_ns'],
                last_raw_ns=r['observation_raw_ns'],n=1))
        else:runs[-1].update(last_index=r['index'],last_raw_ns=r['observation_raw_ns'],n=runs[-1]['n']+1)
    result=[]
    for c in cohorts.values():
        populations={}
        for key in ('raw','admitted','ready'):
            selected=c.pop(key);populations[key]=dict(n=len(selected),
                raw=statistics([r['diagnostic_native_raw_ns'] for r in selected]),
                fixed=statistics([r['diagnostic_native_fixed_ns'] for r in selected if r['diagnostic_native_fixed_ns'] is not None]))
        c['counts']=dict(c['counts']);c['populations']=populations;result.append(c)
    return dict(schema=1,kind='OFFLINE_CONDITIONAL_SHADOW_REPORTING_GATE',policy=copy.deepcopy(POLICY),
        attempts=len(rows),counts=dict(counts),report_ready=sum(r['report_ready'] for r in rows),
        rows=rows,cohorts=result,runs=runs,qualification_start_allowed=False,parity=False,
        changes_historical_classification=False,changes_source_permission=False,
        applies_new_correction=False,absolute_uncertainty_ns=None,
        limits=['Initial/persistent integer association and physical/UTC bounds remain unknown.',
                'Report-ready is presentation readiness, not timing accuracy or clock authority.',
                'Settling uses observation RAW times; no packet arrival becomes an event timestamp.',
                'No phase magnitude, learned mean, trimming or retrospective backfill is used.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path);parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[3];out=args.out.resolve()
    if not out.is_relative_to(root/'artifacts') or out.exists() or not out.parent.is_dir():
        raise ValueError('choose an unused ignored artifacts/ output')
    data=args.input.read_bytes();v=json.loads(data)
    snapshot={p.name:p.read_text() for p in Path(__file__).parent.glob('*.py')}
    provenance=dict(started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
        input=str(args.input.resolve()),input_sha256=hashlib.sha256(data).hexdigest(),
        public_source_snapshot=snapshot,source_sha256={k:hashlib.sha256(s.encode()).hexdigest() for k,s in snapshot.items()},
        policy=POLICY)
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(provenance,f,indent=2)
    report=evaluate(v['raw'],v['analysis']);report.update(input=provenance['input'],input_sha256=provenance['input_sha256'])
    with out.open('x') as f:json.dump(report,f,indent=2,allow_nan=False);f.write('\n')
    if hashlib.sha256(args.input.read_bytes()).hexdigest()!=provenance['input_sha256']:
        raise ValueError('input changed during offline reporting')
    print(json.dumps(dict(report=str(out),attempts=report['attempts'],counts=report['counts'],report_ready=report['report_ready'])))


if __name__=='__main__':main()
