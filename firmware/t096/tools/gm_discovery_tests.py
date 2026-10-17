#!/usr/bin/env python3
"""Actual native GM transport discovery regression at public boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-unready-core',action='store_true');p.add_argument('--legacy-unready-scan',action='store_true');p.add_argument('--legacy-continuous-init',action='store_true');p.add_argument('--legacy-slow-control',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
for n in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
 f=out/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public boundary supplied by fixture. */\n')
source=(app/'protocol/ble_transport.c').read_text()
conn=(a.deps/'zephyr/include/zephyr/bluetooth/conn.h').read_text();gap=(a.deps/'zephyr/include/zephyr/bluetooth/gap.h').read_text()
default=conn[conn.index('#define BT_CONN_LE_CREATE_CONN \\\n'):conn.index('/** Default LE create connection using filter')]
assert default.count('BT_GAP_SCAN_FAST_INTERVAL')==2
assert '#define BT_GAP_SCAN_FAST_INTERVAL               0x0060' in gap
if a.legacy_continuous_init:
 old='const struct bt_conn_le_create_param *create=group_id?\n      BT_CONN_LE_CREATE_PARAM(BT_CONN_LE_OPT_NONE,0x20,0x08):BT_CONN_LE_CREATE_CONN;'
 assert source.count(old)==1
 source=source.replace(old,'const struct bt_conn_le_create_param *create=BT_CONN_LE_CREATE_CONN;')
if a.legacy_unready_core:
 old='if(group_client||(group_id&&i==CONTROL_PEERS))p->connect_until=now+100000;'
 assert source.count(old)==1
 source=source.replace(old,'if(group_client||(group_id&&server_ready&&i==CONTROL_PEERS))p->connect_until=now+100000;')
if a.legacy_unready_scan:
 old='bool core_discovery=group_id&&!group_client&&!precision_discovery&&!sync_acquiring;'
 assert source.count(old)==1
 source=source.replace(old,'bool core_discovery=group_id&&!group_client&&(precision_serving||server_ready)&&!precision_discovery&&!sync_acquiring;')
if a.legacy_slow_control:
 old='unsigned interval=(field_profile||group_id)&&group_client?6:group_id?16:40;'
 assert source.count(old)==1
 source=source.replace(old,'unsigned interval=(field_profile||group_id)&&group_client?6:40;')
(out/'transport.c').write_text(source)
test=(app/'tests/ble_transport_test.c').read_text().replace('#include "../protocol/ble_transport.c"','#include "transport.c"')
(out/'test.c').write_text(test)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}' for x in [out,app/'tests',app/'include',app/'protocol',tiny/'include']],str(out/'test.c'),*[str(app/'protocol'/n) for n in ['group_auth.c','group_control.c','authority_codec.c']],*[str(tiny/'source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'test')]
passed=False;interval=None;modeled_delay=None;network_cmd=None;model_sources={}
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  for case in ['gm-unready','gm-continuous','gm-mtu']:subprocess.run([str(out/'test'),case],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  result=subprocess.run([str(out/'test'),'gm-interval'],capture_output=True,text=True,check=True,timeout=30)
  log.write(result.stdout);interval=int(re.search(r'GM_CONTROL_INTERVAL_UNITS=(\d+)',result.stdout)[1])
  # Production G1 codec/network, using the interval actually selected by the
  # transport. Model one event wait +5ms owner delay in each direction. This
  # budget check does not model physical loss/preemption or qualify hardware.
  modeled_delay=interval*1250+5000
  crypto=[app/'protocol'/n for n in ['group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c']]+[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
  network_cmd=[cmd[0],'-std=c11','-Wall','-Wextra','-Werror','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}' for x in [app/'include',app/'protocol',tiny/'include']],str(app/'tests/group_network_test.c'),str(app/'protocol/network.c'),str(app/'protocol/election_policy.c'),*map(str,crypto),'-o',str(out/'network')]
  model_sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [app/'tests/group_network_test.c',app/'protocol/network.c',app/'protocol/election_policy.c',*crypto]}
  subprocess.run(network_cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'network'),str(modeled_delay)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'production_transport_sha256':hashlib.sha256(source.encode()).hexdigest(),'test_sha256':hashlib.sha256(test.encode()).hexdigest(),'legacy_unready_core':a.legacy_unready_core,'legacy_unready_scan':a.legacy_unready_scan,'legacy_continuous_init':a.legacy_continuous_init,'legacy_slow_control':a.legacy_slow_control,'selected_control_interval_units':interval,'modeled_one_way_delay_us':modeled_delay,'model_compile_command':network_cmd,'model_sources_sha256':model_sources,'model_limit':'One ACL event wait plus5ms owner delay each direction. No RF/scheduler-loss model or hardware qualification.','pinned_default_initiator_us':[60000,60000],'pinned_conn_header_sha256':hashlib.sha256(conn.encode()).hexdigest()},indent=2)+'\n')
print('PASS native GM discovery cancellation and MTU regressions')
