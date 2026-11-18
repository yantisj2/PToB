#!/usr/bin/env python3
"""Exact retained UTE1 envelope margins, always diagnostic and nonmeasurement.

Copyright (c) 2026 Jonathan Yantis. MIT. No hardware/network/clock operations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from gateways.pi5.usb.contract import decode,scope,I64_MAX


def margins(d):
    if d['contract']!='UTE1' or d['measurement'] or not d['estimate_present']:
        raise ValueError('retained margins require explicit nonmeasurement UTE1 estimate')
    if not d['map_generation'] or not 0<=d['source_issued_ns']<d['source_expires_ns']<=I64_MAX:
        raise ValueError('invalid retained envelope metadata')
    def value(tick):
        # rc5_map_project()/map_value(), deliberately without any admission,
        # untilTick or issued/expires grant. This function returns margins,
        # never a usable timestamp or renewed permission.
        if tick<max(d['anchor_tick'],d['effective_tick']) or tick-d['anchor_tick']>((1<<64)-1)//125 or abs(d['map_ppb'])>300000 or abs(d['map_phase_ns'])>300000:
            raise ValueError('retained arithmetic outside original map domain')
        nominal=(tick-d['anchor_tick'])*125//2
        elapsed=nominal*1_000_000_000//(1_000_000_000+d['map_ppb'])
        if not 0<=elapsed<(1<<64) or not 0<=d['anchor_ns']+elapsed<(1<<64):
            raise ValueError('retained arithmetic overflow before phase')
        product=min(elapsed,1_000_000_000)*d['map_phase_ns']
        result=d['anchor_ns']+elapsed+(abs(product)//1_000_000_000)*(-1 if product<0 else 1)
        if not 0<=result<(1<<64):raise ValueError('retained arithmetic overflow')
        return result
    event=value(d['tick']);current=value(d['current_tick'])
    if event+d['utc_offset_ns']!=d['utc_ns']:raise ValueError('retained estimate/map disagreement')
    remaining=d['session_until_tick']-d['current_tick'];issued=d['source_issued_ns'];expires=d['source_expires_ns']
    within=issued<=current<expires
    gate='TRUE_FROM_SESSION_CURRENT_FLAG' if d['session_current'] else 'FALSE_FROM_NONCURRENT_FLAG_WITH_FUTURE_SESSION_UNTIL' if d['session_until_tick'] and remaining>0 else 'UNKNOWN_COMPOUND_CURRENT_AND_DEADLINE_GATE'
    return dict(projected_event_source_ns=event,projected_current_source_ns=current,
        source_issued_ns=issued,source_expires_ns=expires,
        event_minus_issued_ns=event-issued,current_minus_issued_ns=current-issued,
        expires_minus_event_ns=expires-event,expires_minus_current_ns=expires-current,
        arithmetic_root_interval_contains_event=issued<=event<expires,
        arithmetic_root_interval_contains_current=within,
        until_minus_current_ticks=d['until_tick']-d['current_tick'],
        active_hi_minus_current_ticks=d['active_hi_tick']-d['current_tick'],
        session_until_minus_current_ticks=remaining,
        session_tick_deadline_still_future=remaining>0,
        estimate_is_current_inference=gate,
        root_flag_and_arithmetic_agree_when_current_gate_true=(d['root_envelope_current']==within) if gate=='TRUE_FROM_SESSION_CURRENT_FLAG' else None,
        measurement=False,grants_time_authority=False,chrony_eligible=False,absolute_uncertainty_ns=None,
        cause='RF/GNSS_CAUSE_UNKNOWN; interval arithmetic and current-gate evidence only')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('inputs',nargs='+',type=Path);p.add_argument('--out',required=True,type=Path)
    args=p.parse_args();out=args.out.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'artifacts'):raise ValueError('choose a new ignored artifacts/ output')
    sources={f.name:f.read_text() for f in Path(__file__).parent.glob('*.py')}
    with out.with_suffix('.implementation.json').open('x') as f:json.dump(dict(source_snapshot=sources,
        hashes={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sources.items()}),f,indent=2)
    captures=[];identities=set()
    for path in args.inputs:
        content=path.read_bytes();document=json.loads(content);raw=document.get('raw',document)
        identity=(raw.get('boot_id'),raw['start']['raw_before_ns'],raw['end']['raw_after_ns'])
        if identity in identities:raise ValueError('duplicate capture population')
        identities.add(identity);states=[]
        for s in raw['samples']:
            transport=s.get('estimate')
            if not transport:continue
            row=dict(index=s['index'],transport=transport)
            try:
                if 'error' in transport:raise ValueError('estimate transport error')
                d=decode(bytes.fromhex(transport['wire_hex']),raw['device']['silicon'])
                row.update(scope=list(scope(d)),flags=d['flags'],session_current=d['session_current'],root_envelope_current=d['root_envelope_current'],
                    estimate_present=d['estimate_present'],last_discipline_tick=d['last_discipline_tick'],session_until_tick=d['session_until_tick'],
                    tick=d['tick'],current_tick=d['current_tick'],tracking_state=d['tracking_state'],reason=d['reason'],estimate_reason=d['estimate_reason'])
                row['margins']=margins(d)
            except (ValueError,KeyError) as e:row['diagnostic_unavailable']=str(e)
            states.append(row)
        captures.append(dict(input=str(path.resolve()),input_sha256=hashlib.sha256(content).hexdigest(),capture_identity=identity,
            states=states,measurement=False,source_authority_changed=False))
    with out.open('x') as f:json.dump(dict(kind='RETAINED_LOCAL_ONLY_ENVELOPE_DIAGNOSTICS',derived_data_license='CC BY 4.0',
        attribution='Jonathan Yantis',captures=captures,absolute_uncertainty_ns=None,chrony_eligible=False),f,indent=2);f.write('\n')
    print(out)


if __name__=='__main__':main()
