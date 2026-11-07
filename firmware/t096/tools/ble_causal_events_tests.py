#!/usr/bin/env python3
"""Actual BLE timing-owner causal event regressions. No hardware operations.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False);tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
for name in ['kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h']:
 f=O/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Public host platform boundary; actual native uses Zephyr. */\n')
(O/'build_identity.h').write_text('#define R2_SOURCE_SHA "'+'f'*64+'"\n')
s=(A/'tests/radio_service_test.c').read_text().replace('int main(int argc,char **argv)','int historical_radio_main(int argc,char **argv)');assert s.endswith('}\n');(O/'gm7_pt_radio_fixture.inc').write_text(s[:-2]+' return 0;\n}\n')
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(f)for f in[A/'include',A/'protocol',A/'tests',tiny/'include',O]]]
crypto=[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
radio=[A/'tests/ble_causal_producer_test.c',*[A/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c','lora_source.c','lora_rc8_policy.c']],*[A/'protocol'/n for n in ['gm7_bridge.c','gm7_promote.c','gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c','lora_rc8.c']],*crypto]
events=[A/'src/timing_events.c',A/'src/ble_causal_events.c']
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
fence_sources=[A/'tests/ble_causal_fence_test.c',*[A/'protocol'/n for n in ['gm7_codec.c','gm7_control.c','gm7_owner.c','gm7_schedule.c','gm7_air.c','gm7_bridge.c','gm7_promote.c','group_auth.c','gnss_diagnostics.c']],*[A/'src'/n for n in ['gm7_source.c','precision.c','ui_render.c','core.c']],*crypto]
inputs=set(radio+events+fence_sources+[A/'tests/ble_causal_events_test.c',A/'tests/gm7_pt_recovery_test.c',A/'tests/radio_service_test.c',A/'tests/radio_service_fake.h',A/'src/radio_service.c',A/'src/gm7_service.c',A/'tests/gm7_service_test.c',A/'tests/gm7_service_fake.h',Path(__file__).resolve()]+list((A/'include').glob('*.h')))
r={'status':'INCOMPLETE','hardware':False,'class':'ACTUAL_PRODUCER_MAC_PT_MODEL_WITH_FAKE_PHYSICAL_HAL_HCI','inputs':{str(f):sha(f)for f in sorted(inputs)},'steps':[],'limits':['Synthetic physical inputs and real production C/MAC are not RF or UTC accuracy evidence.','Native RAM/stack/transport framing and exact hardware guards remain separate.']}
def run(name,cmd):
 r['steps'].append({'label':name,'command':cmd})
 with(O/(name+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
try:
 run('recorder-compile',flags+['-DR2_BLE_CAUSAL_EVENTS=1','-DR2_TIMING_EVENTS_TEST=1',str(A/'tests/ble_causal_events_test.c'),str(A/'src/timing_events.c'),str(A/'src/core.c'),'-o',str(O/'recorder')])
 run('recorder',[str(O/'recorder')])
 base=flags+['-DR2_GM7_NATIVE=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1']
 run('producer-on-compile',base+['-DR2_BLE_CAUSAL_EVENTS=1','-DR2_TIMING_EVENTS_TEST=1']+list(map(str,radio+events))+['-o',str(O/'producer-on')])
 for mode in (0,1,2,3,4,5,6,7,8,9,10,11,90,91,92):run('producer-on-'+str(mode),[str(O/'producer-on'),str(mode)])
 run('fence-compile',flags+['-DR2_GM5_NATIVE=1','-DR2_GM7_NATIVE=1','-DR2_BLE_CAUSAL_EVENTS=1','-DR2_TZ_WIRE=1']+list(map(str,fence_sources))+['-o',str(O/'fence')])
 run('fence',[str(O/'fence')])
 run('producer-off-compile',base+list(map(str,radio))+['-o',str(O/'producer-off')])
 for mode in (90,91,92):run('producer-off-'+str(mode),[str(O/'producer-off'),str(mode)])
 matches=[re.search(r'BLE_MODEL_REPLAY ([0-9a-f]+) accepted(\d+) rejected(\d+) until(\d+) observer(\d+)',(O/(kind+'-'+str(mode)+'.log')).read_text())for kind in ['producer-on','producer-off']for mode in (90,91,92)]
 assert all(matches)and len({m.groups()[:4]for m in matches})==1
 r['identical_900_packet_model_UTC_permission_replay']=matches[0].groups()[:4]
 assert all(sha(f)==h for f,h in r['inputs'].items());r['status']='PASS'
finally:
 r['files']={str(f.relative_to(O)):sha(f)for f in O.rglob('*')if f.is_file()and f.name!='receipt.json'};(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual causal producer/events:',O)
