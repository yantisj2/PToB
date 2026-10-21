#!/usr/bin/env python3
"""Production GM control-drain regressions; synthetic public APIs, no hardware.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
p.add_argument('--legacy-fixed-retirement',action='store_true')
p.add_argument('--legacy-immediate-fault',action='store_true');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
for name in ['kernel.h','sys/atomic.h','random/random.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h']:
 f=out/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public API test boundary. */\n')
source=(app/'protocol/ble_transport.c').read_text()
if a.legacy_immediate_fault:
 old='if(rc&&rc!=-EACCES&&rc!=-ENOTCONN)stop_error=rc;'
 assert source.count(old)==1;source=source.replace(old,'if(rc)stop_error=rc;')
if a.legacy_fixed_retirement:
 assert 'allowance+=(uint64_t)*supervision*10000;' in source
 source=source.replace('allowance+=(uint64_t)*supervision*10000;','allowance+=0; /* old144 fixed2s */').replace('allowance=R2_CALLBACK_RETIRE_US','allowance=2000000')
(out/'transport.c').write_text(source)
test=(app/'tests/gm_control_drain_test.c').read_text().replace('#include "../protocol/ble_transport.c"','#include "transport.c"')
(out/'test.c').write_text(test)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
cc=os.environ.get('CC') or shutil.which('clang')
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*[f'-I{x}'for x in [out,app/'tests',app/'include',app/'protocol',tiny/'include']],str(out/'test.c'),*[str(app/'protocol'/n)for n in ['group_auth.c','group_control.c','authority_codec.c']],*[str(tiny/'source'/n)for n in ['hmac.c','sha256.c','utils.c']],'-o',str(out/'test')]
passed=False
try:
 with (out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 with (out/'run.log').open('x')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'legacy_immediate_fault':a.legacy_immediate_fault,'legacy_fixed_retirement':a.legacy_fixed_retirement,'command':cmd,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in [app/'protocol/ble_transport.c',app/'tests/gm_control_drain_test.c',app/'tests/ble_transport_fake.h']},'tested_transport_sha256':hashlib.sha256(source.encode()).hexdigest()},indent=2)+'\n')
print('PASS native GM control drain regressions')
