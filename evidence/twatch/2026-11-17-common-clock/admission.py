#!/usr/bin/env python3
"""Admission intervals from source-bearing CRC status, separate from fit readiness.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,hashlib
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['frames','events','protocol','out']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();frames=[json.loads(x) for x in a.frames.read_text().splitlines()];events=json.loads(a.events.read_text());protocol=json.loads(a.protocol.read_text())
    # Owner proof is emitted immediately after this same validated status
    # returns. Anchor relative host timestamps, without using transported UTC.
    offsets=[]
    for e in events:
        if e['kind']=='RADIO_OWNER_PROVEN':
            match=next((f for f in frames if int(f['begin']['request_us'])==e['device_us']),None)
            if match:offsets.append(e['host_monotonic']-match['elapsed_s'])
    assert offsets,'No causal host/session anchor'
    anchor=min(offsets);spread=max(offsets)-min(offsets)
    result=dict(schema=1,author='Jonathan Yantis',license='CC-BY-4.0',
                method='First observed authenticated source state and previous negative-to-positive sampling bracket; earlier transient admissions cannot be excluded; independent of fit readiness',
                host_anchor_spread_seconds=spread,latencies=[])
    starts={e['stage']:e for e in events if e['kind']=='STAGE_START'}
    for spec in protocol['stages']:
        name=spec['name'];start=starts.get(name)
        if not start or not spec['ble']:continue
        origin=start['host_monotonic'];seconds=spec['seconds'];previous_false=0;first=None;wanted_rc=spec.get('upstream')=='relay'
        pinned=[e['action'].split()[-1] for e in events if e['stage']==name and e['kind']=='PARENT_CONTROL_ACCEPTED' and e['action'].startswith('gm-parent ')]
        for f in frames:
            at=f['elapsed_s']+anchor-origin
            if not 0<=at<seconds:continue
            b=f['rows'].get('W12_R2_STATUS',{})
            if not all(k in b for k in ['rc7','root_identity','joined_peer','group_member','GR_authenticated']):continue
            rc7=b['rc7'].split('/')
            relay=len(rc7)==4 and rc7[2]=='1' and int(rc7[3])>0
            direct=len(rc7)==4 and rc7[2]=='0' and b['joined_peer']==b['root_identity']
            authentic=b['following']=='1' and ((relay and rc7[0]=='2') or (direct and b['group_member']=='1' and int(b['GR_authenticated'])>0))
            wanted=authentic and (relay if wanted_rc else direct)
            if pinned:wanted=wanted and b['root_identity']==pinned[-1]
            if wanted:
                first=at;break
            previous_false=at
        result['latencies'].append(dict(stage=name,target='authenticated RC hop' if wanted_rc else 'authenticated direct GM',
            lower_seconds=previous_false,upper_seconds=first,censored=first is None,censor_seconds=previous_false if first is None else None,window_seconds=seconds,
            sampling='full source-bearing status, nominal5s plus control requests; timestamp anchor spread retained',
            clock_quality_inferred=False,observed_within_45s=bool(first is not None and first<=45),availability_failure_45s=bool(first is None or first>45)))
    result['provenance']={name+'_sha256':hashlib.sha256(getattr(a,name).read_bytes()).hexdigest() for name in ['frames','events','protocol']}
    a.out.write_text(json.dumps(result,indent=2)+'\n');print('Source-admission intervals:',len(result['latencies']))


if __name__=='__main__':main()
