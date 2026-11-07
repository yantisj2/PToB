#!/usr/bin/env python3
"""Actual GM7 control/PT context regression; fake physical/HCI boundaries only.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-source',type=Path);a=p.parse_args()
A=Path(__file__).resolve().parents[1];B=a.baseline_source.resolve()if a.baseline_source else A;O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for name in ['kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h']:
 f=O/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public host boundary; native uses actual Zephyr. */\n')
(O/'build_identity.h').write_text('#define R2_SOURCE_SHA "'+'f'*64+'"\n')
# Copy only the selected harness so its quoted fixture include resolves against
# B/tests, never accidentally against the candidate harness directory.
entry=A/'tests/gm7_control_rebind_test.c';test=O/'test.c';test.write_bytes(entry.read_bytes())
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(x)for x in[B/'include',B/'protocol',B/'tests',tiny/'include',O]],'-DR2_GM5_NATIVE=1','-DR2_GM7_NATIVE=1','-DR2_BLE_CAUSAL_EVENTS=1','-DR2_TZ_WIRE=1']
if not a.baseline_source:flags+=['-DCANDIDATE_FIXED=1']
sources=[test,*[B/'protocol'/n for n in ['gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c']],*[B/'src'/n for n in ['gm7_source.c','precision.c','ui_render.c','core.c']],*[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]]
inputs=set(sources+[entry,B/'src/gm7_service.c',B/'tests/gm7_service_test.c',B/'tests/gm7_service_fake.h',Path(__file__)]+list((B/'include').glob('*.h')));sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
r={'status':'INCOMPLETE','hardware':False,'baseline':bool(a.baseline_source),'production_source':str(B),'inputs':{str(f):sha(f)for f in sorted(inputs)},'steps':[],'limits':['Actual production C/encoded MAC control with fake physical PT snapshot and HCI; not RF, field availability or UTC accuracy evidence.','Synthetic token mismatch proves reset loop mechanism, not exact live mismatch origin.']}
def run(label,cmd):
 r['steps'].append({'label':label,'command':cmd})
 with(O/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
try:
 assert sha(test)==sha(entry);run('compile',flags+list(map(str,sources))+['-o',str(O/'test')])
 for mode in range(20):run('mode'+str(mode),[str(O/'test'),str(mode)])
 assert all(sha(f)==h for f,h in r['inputs'].items());r['status']='PASS'
finally:(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual production control rebind tests:',O)
