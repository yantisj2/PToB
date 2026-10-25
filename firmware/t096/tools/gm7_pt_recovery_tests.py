#!/usr/bin/env python3
"""Focused actual GM7 producer/PT recovery composition; no hardware operations.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for n in ['kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h']:
 f=O/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public platform declarations supplied by actual fixture. */\n')
fixture=(A/'tests/radio_service_test.c').read_text().replace('int main(int argc,char **argv)','int historical_radio_main(int argc,char **argv)');assert fixture.endswith('}\n');(O/'gm7_pt_radio_fixture.inc').write_text(fixture[:-2]+' return 0;\n}\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_GM7_NATIVE=1',*['-I'+str(f)for f in[A/'include',A/'protocol',A/'tests',tiny/'include',O]]]
crypto=[tiny/'source'/n for n in['hmac.c','sha256.c','utils.c']]
radio=[A/'tests/gm7_pt_recovery_test.c',*[A/'src'/n for n in['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c','lora_source.c','lora_rc8_policy.c']],*[A/'protocol'/n for n in['gm7_promote.c','gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c','lora_rc8.c']],*crypto]
producer=[A/'tests/gm7_pt_producer_test.c',*[A/'protocol'/n for n in['gm7_codec.c','gm7_session.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c']],*[A/'src'/n for n in['gm7_source.c','precision.c','ui_render.c']],*crypto]
inputs=set(radio+producer+[A/'src/gm7_service.c',A/'src/radio_service.c',A/'src/radio_audit.c',A/'tests/gm7_service_test.c',A/'tests/gm7_service_fake.h',A/'tests/radio_service_test.c',A/'tests/radio_service_fake.h',Path(__file__).resolve()]+list((A/'include').glob('*.h'))+list((A/'protocol').glob('*.h')))
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();r=dict(status='INCOMPLETE',class_='ACTUAL_GM_TIMING_PRODUCER_AND_PT_NATIVE_LIFECYCLE_FAKE_HAL_HCI',inputs={str(f):sha(f)for f in sorted(inputs)},steps=[],hardware_operated=False,limits=['Qualified owner grants/physical sources and RF edges are synthetic; actual production producer/PT/precision/codec/start/stop/export code is executed.','Successful HCI boundary requests are not actual RF START or hardware precision/retention evidence.','CPUcorruption remains separate; source/build/native review still required.'])
def run(label,cmd):
 r['steps'].append(dict(label=label,command=cmd))
 with(O/(label+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
try:
 run('radio-compile',flags+['-DR2_LORA=1']+list(map(str,radio))+['-o',str(O/'radio')])
 for n in list(range(21))+[116,117,120,122,130,131,132,133,134,150,161]:run('radio-'+str(n),[str(O/'radio'),str(n)])
 run('producer-compile',flags+['-DR2_GM5_NATIVE=1']+list(map(str,producer))+['-o',str(O/'producer')])
 for n in range(6):run('producer-'+str(n),[str(O/'producer'),str(n)])
 assert all(sha(f)==h for f,h in r['inputs'].items());r['status']='PASS'
finally:
 r['files']={str(f.relative_to(O)):sha(f)for f in O.rglob('*')if f.is_file()and f.name!='receipt.json'};(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS focused actual GM7 PT recovery:',O)
