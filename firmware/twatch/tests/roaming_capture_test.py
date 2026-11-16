#!/usr/bin/env python3
"""Offline host guard/report regression for bounded watch-only source switches.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import copy
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from roaming_capture import BidirectionalRoaming, auto_restored, report
from framed_console import mutation_command

ROOT='0000000000000111'
RELAY='0000000000000333'


def frame(at, peer=ROOT, depth=0, following=True, target='0000000000000000', state=3):
    return {'capture_elapsed_s': at, 'rows': {
        'W12_R2_STATUS': {'client_running': '1', 'requested': '1', 'fault': '0', 'following': str(int(following)),
                          'root_identity': ROOT, 'root_boot': '0000000000000055', 'source_epoch': '2',
                          'joined_peer': peer, 'rc7': '2/0/'+str(int(depth>0))+'/'+str(depth),
                          'rp': target+'/1/'+str(state)+'/0/0/1'},
        'W12_V12_COMMAND': {'accepted': '1'},
        'TWATCH_UI': {'clock': '1', 'local': str(int(not following)), 'sampled_us': str(int(at*1000000)),
                      'utc_us': str(1700000000000000+int(at*1000000)), 'source': str(2 if depth else 1)}}}


def refuses(call, message):
    try:
        call()
    except RuntimeError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError('Expected guarded refusal: '+message)


first=frame(0)
policy=BidirectionalRoaming(first,RELAY)
assert policy.action(frame(29.99)) is None
assert policy.action(frame(30)) == 'rc-parent '+RELAY
policy.acknowledged(frame(30,target=RELAY,state=1))
assert policy.action(frame(31)) is None and policy.stage=='ADMIT' # Previous direct source cannot satisfy RC1.
assert policy.action(frame(32,RELAY,1,target=RELAY)) is None and policy.stage=='OBSERVE'
assert policy.action(frame(91.99,RELAY,1,target=RELAY)) is None
assert policy.action(frame(92,RELAY,1,target=RELAY)) == 'gm-parent '+ROOT
policy.acknowledged(frame(92,RELAY,1,target=ROOT,state=1))
assert policy.action(frame(93,ROOT,0,target=ROOT)) is None and policy.stage=='OBSERVE'
assert policy.action(frame(153,target=ROOT)) == 'rc-parent '+RELAY
policy.acknowledged(frame(153,target=RELAY,state=1))
assert policy.action(frame(154,RELAY,1,target=RELAY)) is None
assert policy.action(frame(214,RELAY,1,target=RELAY)) == 'rc-parent auto'
policy.acknowledged(frame(214,RELAY,1,state=1))
assert policy.action(frame(215,RELAY,1,state=2)) is None and not policy.done
assert policy.action(frame(216,RELAY,1)) is None and policy.done and auto_restored(frame(216))
assert [leg['kind'] for leg in policy.legs]==['RC1','BLE','RC1']
assert all(leg['admission_latency_s']<=2 for leg in policy.legs)

refuses(lambda: BidirectionalRoaming(frame(0,RELAY,1),RELAY),'direct GM')
refuses(lambda: BidirectionalRoaming(frame(0,following=False),RELAY),'direct GM')
bad=copy.deepcopy(first);bad['rows']['W12_R2_STATUS']['root_boot']='0000000000000000'
refuses(lambda: BidirectionalRoaming(bad,RELAY),'identity/scope')
failure=BidirectionalRoaming(first,RELAY);assert failure.action(frame(30))
failure.acknowledged(frame(30));refuses(lambda: failure.action(frame(75)),'within45s')
changed=frame(30);changed['rows']['W12_R2_STATUS']['root_boot']='0000000000000056'
refuses(lambda: BidirectionalRoaming(first,RELAY).action(changed),'incompatible cohorts')
fault=frame(30);fault['rows']['W12_R2_STATUS']['fault']='1'
refuses(lambda: BidirectionalRoaming(first,RELAY).action(fault),'owner stopped or faulted')
declined=frame(30);declined['rows']['W12_V12_COMMAND']['accepted']='0'
refuses(lambda: failure.acknowledged(declined),'refused')
assert failure.action({'capture_elapsed_s': 50, 'rows': {'W12_R2_STATUS': {'following': '1'}}}) is None

samples=[frame(30,following=False),frame(31,following=False),frame(32,RELAY,1),frame(33,RELAY,1)]
samples[-1]['rows']['TWATCH_UI']['utc_us']=samples[-2]['rows']['TWATCH_UI']['utc_us']
for at in (31,32,33):
    f=frame(at,RELAY,1,following=at>=32)
    f['rows']['TWATCH_SOURCE']={'servo_trained': '1', 'p99_ready': '1', 'p99_root': ROOT, 'p99_source': '2',
                              'p99_parent': RELAY, 'servo_raw_rms_ns': '4294967295' if at==31 else '90'}
    samples.append(f)
samples.sort(key=lambda f:f['capture_elapsed_s'])
summary=report(samples,policy)
leg=summary['legs'][0]
assert leg['first_servo_trained_latency_s']==2 and leg['first_p99_ready_latency_s']==2
assert leg['servo_raw_rms_ns']['max']==90 and leg['servo_raw_rms_ns']['unknown_samples']==1
assert leg['display_hold_sampled_s']==2 and not leg['display_backward_steps']
assert summary['scope']=='SAMPLED_FIRMWARE_DIAGNOSTICS_NOT_INDEPENDENT_ACCURACY'
print('PASS bounded RC1/direct/RC1 policy: fresh authenticated initial GM,45s admission/60s observation,cohort refusal,unavailable relay,fault,ack,AUTO drain,latency/hold/unknown-quality report; no hardware')
ticket={'begin':{'ticket':'0000000000000001','nonce':'0000000000000002','factory':'02:00:00:00:00:01','session':'0000000000000003'}}
for action in ('rc-parent '+RELAY,'gm-parent '+ROOT,'rc-parent auto','gm-parent auto','capture rolling 20000','capture stop'):
    command=mutation_command(ticket,'0000000000000004',action)
    assert command==b'\x00\n'+('v12 do 0000000000000001 0000000000000004 02:00:00:00:00:01 0000000000000003 '+action+'\n').encode()
for action in ('gm-parent -1','gm-parent '+ROOT+' extra','capture rolling 9999','capture rolling 600001','capture tx 1000'):
    try:mutation_command(ticket,'0000000000000004',action)
    except Exception:pass
    else:raise AssertionError('Unbounded or unsupported action serialized: '+action)
print('PASS actual fresh-ticket serialization: both parent kinds/AUTO/bounded observer, malformed and out-of-bound actions refused')
