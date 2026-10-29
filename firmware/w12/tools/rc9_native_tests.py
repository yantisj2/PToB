#!/usr/bin/env python3
"""Actual sole native RC7/RC9 controller with synthetic public SDK callbacks."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,json,os,subprocess
from pathlib import Path
from rc7_permission_e2e import native_fixture,server_fixture,replace_once,sha
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--objects',type=Path,required=True);p.add_argument('--tz',action='store_true');a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out);server_fixture(out)
s=(out/'server.cpp').read_text();s=s[:s.index('int main(')]
s=s.replace('p->sid==12+i','p->sid==v12::relay_profile*1u+5u+i')
s=s.replace('assert(sid==12);fixture_sync_arg=arg;', 'assert(sid==v12::sync_selected_sid);fixture_sync_arg=arg;')
s=s.replace('e.periodic_sync.per_adv_ival=50;', 'e.periodic_sync.sid=v12::sync_selected_sid;e.periodic_sync.per_adv_ival=50;')
s=s.replace('c.uuid.u128=v12::peer.relay?v12::rc_data_uuid:v12::data_uuid;', 'c.uuid.u128=v12::peer.rc9?v12::rc9_data_uuid:v12::peer.relay?v12::rc_data_uuid:v12::data_uuid;')
s=s.replace('os_mbuf b;b.bytes.resize(RC_DYNAMIC_CONTROL_BYTES);const auto& c=', '''os_mbuf b;b.bytes.resize(212);
 if(v12::peer.rc9){v12::rc9::Control c;c.sender=77;c.sender_boot=88;c.next_challenge=99;
 if(v12::peer.stage!=2){const auto& q=v12::rc9_client.request;c.type=v12::rc9::Type::Response;c.target=q.sender;c.target_boot=q.sender_boot;
 c.next_challenge=q.challenge+1;c.challenge=q.challenge;c.nonce=q.nonce;c.sequence=q.sequence;c.lease_us=q.lease_us;c.server_us=fake_now;c.publication_floor=10;}
 assert(v12::rc9::encode_control(b.bytes.data(),c,v12::provision.group));ble_gatt_attr a;a.om=&b;ble_gatt_error e;cb(h,&e,&a,arg);return 0;}
 b.bytes.resize(RC_DYNAMIC_CONTROL_BYTES);const auto& c=''')
s=s.replace('assert(n==RC_DYNAMIC_CONTROL_BYTES);assert(!rc7_control_decode', '''if(v12::peer.rc9){assert(n==212);v12::rc9::Control q;assert(v12::rc9::decode_control(q,static_cast<const uint8_t*>(wire),n,v12::provision.group));ble_gatt_error e;cb(h,&e,nullptr,arg);return 0;}assert(n==RC_DYNAMIC_CONTROL_BYTES);assert(!rc7_control_decode''')
s+='\n#include "rc9_native_client_fixture.inc"\n#include "rc9_native_fixture.inc"\n';(out/'rc9.cpp').write_text(s)
crypto=Path(os.environ['IDF_PATH'])/'components/bt/host/nimble/nimble/ext/tinycrypt/include'
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DV12_R2_ENABLED=1','-DV12_R2_CONFIG=13','-DV12_R2_FIELD_CLIENT=1','-DV12_UNIT="C2"','-DV12_R2_G1_LISTENING_PERCENT=100','-DV12_R2_TX_SHADOW=1','-DV12_R2_RC7_TX_ESTIMATE_LAB=1','-DPTOB_RC9=1','-DPTOB_W12_LORA_RX=1']
if a.tz:flags+=['-DR2_TZ_WIRE=1']
for q in (out,APP/'tests',APP/'tests/r2_fakes',APP/'tests/console_fakes',APP/'main',APP/'legacy/reference',APP/'main/r2/r2-g1',APP/'main/r2/rc7/include',crypto):flags+=['-I'+str(q)]
objects=[a.objects.resolve()/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','rc_dynamic','rc_boundary','rc_slots','g1_hmac','sha256','utils')]
cmd=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'),*flags,out/'rc9.cpp',APP/'main/rc9/rc9.cpp',*objects,'-o',out/'native']
(out/'compile-command.json').write_text(json.dumps(list(map(str,cmd)),indent=2)+'\n')
with (out/'compile.log').open('w') as f:subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,check=True)
cases=('client-terminate-ack','client-pending-relay-return','client-relay-return','recover','client-cache-fence','client-scan','client-return','client-expired','client-recovery-pin','client-no-source','client-hint','client','client-pin','speculative','race','withdraw','lease','rebind','off')
for case in cases:
 with (out/(case+'.log')).open('w') as f:subprocess.run([out/'native',case],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=15)
paths=[APP/'main/native_g1.cpp',APP/'main/g1_parent_selection.hpp',APP/'main/rc7_relay_native.inc',APP/'main/rc9_client_native.inc',APP/'main/rc9_server_native.inc',APP/'tests/native_g1_test.cpp',APP/'tools/rc7_permission_e2e.py',APP/'tests/rc9_native_client_fixture.inc',APP/'tests/rc9_native_fixture.inc',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',cases=cases,hardware_access=False,scope='ACTUAL_NATIVE_RC9_CLASS_AUTH_LEASE_WITHDRAW_SNAPSHOT_RACE_OFF_ORIGINAL_DRAIN_ASAN_UBSAN',sources={str(p):sha(p) for p in paths}),indent=2)+'\n')
print('PASS native RC9 controller')
