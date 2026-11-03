#!/usr/bin/env python3
"""Actual GM owner stage bins and production-cycle smoke, fake boundaries.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for n in('kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h'):
 f=O/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Fixture platform declarations. */\n')
fixture=O/'stage.c';fixture.write_text('#define main historical_owner_main\n#include "'+str(A/'tests/gm7_service_test.c')+'"\n#undef main\nint main(void){worker_stage_accounting();ordinary_progress(17,33);assert(!status.supervision.stage);return 0;}\n')
sources=[A/'protocol'/n for n in('gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c')]+[A/'src'/n for n in('gm7_source.c','precision.c','ui_render.c')]+[tiny/'source'/n for n in('hmac.c','sha256.c','utils.c')]+[fixture]
command=[os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-DR2_GM5_NATIVE=1',*['-I'+str(f)for f in(A/'include',A/'protocol',A/'tests',tiny/'include',O)],*map(str,sources),'-o',str(O/'stage')]
for name,cmd in(('compile',command),('run',[str(O/'stage')])):
 r=subprocess.run(cmd,capture_output=True,text=True);(O/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
inputs=[A/'src/gm7_service.c',A/'tests/gm7_service_test.c',A/'tests/gm7_service_fake.h',A/'include/ble_service.h',Path(__file__)]
(O/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_STAGE_BINS_PRODUCTION_OWNER_SMOKE',hardware=False,new_mutable_state_bytes=0,limits='Wall-time instrumentation includes preemption and waits; no CPU-occupancy inference',command=command,sources={str(f.relative_to(A)):hashlib.sha256(f.read_bytes()).hexdigest()for f in inputs}),indent=2)+'\n');print('PASS actual GM stage maxima/saturation/regression and normal production owner cycle')
