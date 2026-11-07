#!/usr/bin/env python3
"""Read-only RF comparison owners with actual delivered models, synthetic HAL.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
for name in ['kernel.h','irq.h','sys/atomic.h','bluetooth/bluetooth.h','bluetooth/conn.h','bluetooth/gatt.h','bluetooth/hci.h','random/random.h']:
 f=O/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Host HAL boundary only. */\n')
(O/'build_identity.h').write_text('#define R2_SOURCE_SHA "'+'f'*64+'"\n')
s=(A/'tests/radio_service_test.c').read_text().replace('int main(int argc,char **argv)','int historical_radio_main(int argc,char **argv)');assert s.endswith('}\n');(O/'gm7_pt_radio_fixture.inc').write_text(s[:-2]+' return 0;\n}\n')
s=(A/'tests/lora_fixed500_test.c').read_text();assert s.endswith('}\n');(O/'lora_fixed500_rf_fixture.inc').write_text(s[:-2]+' return 0;\n}\n')
s=(A/'src/lora_passive_service.c').read_text();(O/'owner.c').write_text(re.sub(r'^#include[^\n]*\n','',s,flags=re.M))
s=(A/'src/lora_hw.c').read_text();start=s.index('static struct r2_lora_hw_rejection rejected;');(O/'admission.c').write_text(s[start:s.index('\n#endif\nstatic int ',start)])
s=(A/'src/rc_dynamic_service.c').read_text()
def function(name):
 start=s.index(name+'(');start=s.rfind('\n',0,start)+1;end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
start=s.index('struct rf_rc_map {');end=s.index('static struct rf_rc_map rf_rc;',start)+len('static struct rf_rc_map rf_rc;')
(O/'rf_rc_under_test.h').write_text(s[start:end]+'\n'+function('rf_clock_publish')+'\n'+function('r2_service_rf_clock')+'\n')
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(f)for f in[A/'include',A/'protocol',A/'tests',tiny/'include',O]]]
crypto=[tiny/'source'/n for n in ['hmac.c','sha256.c','utils.c']]
radio=[A/'tests/rf_clock_radio_test.c',*[A/'src'/n for n in ['radio_precision.c','relay_precision.c','core.c','clock.c','nmea.c','precision.c','lora_source.c','lora_rc8_policy.c']],*[A/'protocol'/n for n in ['gm7_bridge.c','gm7_promote.c','gnss_diagnostics.c','role_announcement.c','group_join.c','group_auth.c','group_control.c','group_precision.c','authority_codec.c','precision_codec.c','relay_codec.c','lora_rc8.c']],*crypto]
lora=[A/'tests/rf_clock_lora_test.c',*[A/'src'/n for n in ['lora_passive_core.c','lora_receiver_watch.c','lora_rc8_policy.c']],*[A/'protocol'/n for n in ['lora_passive.c','lora_rc8.c','group_auth.c']],*crypto]
rc=[A/'tests/rf_clock_rc_test.c',A/'src/rc_boundary.c',A/'protocol/group_auth.c',*crypto]
owned=['include/rf_clock.h','include/service_backend_alias.h','src/radio_service.c','src/lora_passive_service.c','src/lora_service.c','src/rc_dynamic_service.c','src/rc_profile_service.c','src/gm7_service.c','src/ble_service.c','tests/lora_ram_fixture.h','tests/gm7_service_test.c','tests/rf_clock_radio_test.c','tests/rf_clock_lora_test.c','tests/rf_clock_rc_test.c','tools/rf_clock_tests.py']
inputs=set(radio+lora+rc+[A/f for f in owned]+[A/'tests/gm7_pt_recovery_test.c',A/'tests/radio_service_test.c',A/'tests/lora_fixed500_test.c']+list((A/'include').glob('*.h')))
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
r={'status':'INCOMPLETE','hardware':False,'scope':'ACTUAL_GM_PT_MAC_AND_WIRE3_OWNER; EXTRACTED_ACTUAL_RC7_PUBLICATION_QUERY_WITH_REAL_RC5_PROJECTOR','inputs':{str(f):sha(f)for f in sorted(inputs)},'steps':[],'limits':['Synthetic inputs do not prove RF, UTC accuracy, or native stack/RAM.']}
def run(name,cmd):
 r['steps'].append({'label':name,'command':cmd})
 with(O/(name+'.log')).open('x')as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
try:
 base=flags+['-DR2_GM7_NATIVE=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1']
 run('radio-compile',base+list(map(str,radio))+['-o',str(O/'radio')])
 for mode in (1,2):run('radio-'+str(mode),[str(O/'radio'),str(mode)])
 run('lora-compile',base+['-DR2_PASSIVE_SERVICE_BODY="'+str(O/'owner.c')+'"','-DR2_HW_ADMISSION_BODY="'+str(O/'admission.c')+'"']+list(map(str,lora))+['-o',str(O/'lora')])
 for mode in (1,2):run('lora-'+str(mode),[str(O/'lora'),str(mode)])
 run('rc-compile',flags+['-DR2_LORA=1']+list(map(str,rc))+['-o',str(O/'rc')])
 run('rc',[str(O/'rc')])
 assert all(sha(f)==h for f,h in r['inputs'].items());r['status']='PASS'
finally:
 r['files']={str(f.relative_to(O)):sha(f)for f in O.rglob('*')if f.is_file()and f.name!='receipt.json'};(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS RF clock exactns/actualGM+BLE+wire3+RC7/nonauthority:',O)
