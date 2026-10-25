#!/usr/bin/env python3
"""Actual GM7 source snapshot ordering/freshness regression; no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--historical-source',type=Path);a=p.parse_args()
A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for n in ['kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h']:
 q=O/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Public kernel/HAL declarations supplied by test fixture. */\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1','-DR2_GM5_NATIVE=1',*['-I'+str(f)for f in[A/'include',A/'protocol',A/'tests',tiny/'include',O]]]
sources=[A/'protocol'/n for n in ['gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c']]+[A/'src'/n for n in ['gm7_source.c','precision.c','ui_render.c']]+[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]+[A/'tests/gm7_source_order_test.c']
inputs=set(sources+[A/'src/gm7_service.c',A/'tests/gm7_service_test.c',A/'tests/gm7_service_fake.h',Path(__file__).resolve()]+list((A/'include').glob('*.h'))+list((A/'protocol').glob('*.h')))
legacy=[]
if a.historical_source:
 H=a.historical_source.resolve();fixture=H/'tests/gm7_service_test.c';service=H/'src/gm7_service.c';body=fixture.read_text();assert body.count('#include "../src/gm7_service.c"')==1
 q=O/'historical_fixture.inc';q.write_text(body.replace('#include "../src/gm7_service.c"','#include "'+str(service)+'"'));inputs.update([fixture,service,q]);legacy=['-DR2_SOURCE_ORDER_LEGACY=1','-DR2_SOURCE_ORDER_FIXTURE="'+str(q)+'"']
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();r=dict(status='INCOMPLETE',class_='ACTUAL_GM_SOURCE_READ_OWNER_UPDATE_FAKE_KERNEL_CAPTURE',inputs={str(q):sha(q)for q in sorted(inputs)},steps=[],hardware_operated=False,limits=['Controlled source publication between prior bridge and input copy is a production ordering counterexample, not proof of the field withdrawal trigger.','Freshness/generation/stop/fatal and physical capture fixtures are synthetic. No CPU-fix, actual RF/UTC or stability claim.','Historical source is optional explicit public source snapshot input; no enrolled binary or private sibling is required for current regression.'])
def run(label,command,expected=0):
 r['steps'].append(dict(label=label,command=command,expected_exit=expected))
 with(O/(label+'.log')).open('x')as f:q=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,timeout=45)
 r['steps'][-1]['exit']=q.returncode;assert q.returncode==expected,(label,q.returncode,expected)
try:
 if legacy:
  run('historical-compile',flags+legacy+list(map(str,sources))+['-o',str(O/'historical')])
  for n in range(2):run('historical-'+str(n),[str(O/'historical'),str(n)],1)
 run('compile',flags+list(map(str,sources))+['-o',str(O/'current')])
 for n in range(11):run('case-'+str(n),[str(O/'current'),str(n)])
 assert all(sha(q)==h for q,h in r['inputs'].items());r['status']='PASS'
finally:
 r['files']={str(q.relative_to(O)):sha(q)for q in O.rglob('*')if q.is_file()and q.name!='receipt.json'};(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual GM7 source order and original freshness:',O)
