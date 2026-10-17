#!/usr/bin/env python3
"""Actual native GM transport discovery regression at public boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-unready-core',action='store_true');p.add_argument('--legacy-unready-scan',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
for n in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
 f=out/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public boundary supplied by fixture. */\n')
source=(app/'protocol/ble_transport.c').read_text()
if a.legacy_unready_core:
 old='if(group_client||(group_id&&i==CONTROL_PEERS))p->connect_until=now+100000;'
 assert source.count(old)==1
 source=source.replace(old,'if(group_client||(group_id&&server_ready&&i==CONTROL_PEERS))p->connect_until=now+100000;')
if a.legacy_unready_scan:
 old='bool core_discovery=group_id&&!group_client&&!precision_discovery&&!sync_acquiring;'
 assert source.count(old)==1
 source=source.replace(old,'bool core_discovery=group_id&&!group_client&&(precision_serving||server_ready)&&!precision_discovery&&!sync_acquiring;')
(out/'transport.c').write_text(source)
test=(app/'tests/ble_transport_test.c').read_text().replace('#include "../protocol/ble_transport.c"','#include "transport.c"')
(out/'test.c').write_text(test)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}' for x in [out,app/'tests',app/'include',app/'protocol',tiny/'include']],str(out/'test.c'),*[str(app/'protocol'/n) for n in ['group_auth.c','group_control.c','authority_codec.c']],*[str(tiny/'source'/n) for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  for case in ['gm-unready','gm-continuous','gm-mtu']:subprocess.run([str(out/'test'),case],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:(out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'production_transport_sha256':hashlib.sha256(source.encode()).hexdigest(),'test_sha256':hashlib.sha256(test.encode()).hexdigest(),'legacy_unready_core':a.legacy_unready_core,'legacy_unready_scan':a.legacy_unready_scan},indent=2)+'\n')
print('PASS native GM discovery cancellation and MTU regressions')
