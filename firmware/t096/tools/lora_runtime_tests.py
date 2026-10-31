#!/usr/bin/env python3
"""RC8 actual runtime/source/rendering checks; all physical boundaries are fake.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 cc=os.environ.get('CC','clang');crypto=a.deps/'modules/crypto/tinycrypt/lib'
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')]
 objects=[]
 for n in ['hmac','sha256','utils']:
  obj=a.out/(n+'.o');objects.append(obj);subprocess.run([*flags,'-Wno-default-const-init-var-unsafe','-c',str(crypto/'source'/(n+'.c')),'-o',str(obj)],check=True)
 common=[APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*objects]
 for feature in [False,True]:
  binary=a.out/('core-on'if feature else'core-off');subprocess.run([*flags,*(['-DR2_LORA=1']if feature else[]),str(APP/'src/core.c'),str(APP/'tests/lora_core_test.c'),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
 source=a.out/'source';subprocess.run([*flags,*map(str,[APP/'tests/lora_source_test.c',APP/'src/lora_source.c',APP/'src/precision.c',*common]),'-o',str(source)],check=True);subprocess.run([str(source)],check=True)
 body=a.out/'service_body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_service.c').read_text(),flags=re.M))
 binary=a.out/'runtime';subprocess.run([*flags,'-DR2_LORA_SERVICE_BODY="'+str(body.resolve())+'"',*map(str,[APP/'tests/lora_service_test.c',*common]),'-o',str(binary)],check=True)
 logs=[]
 for mode in range(1,126):
  r=subprocess.run([str(binary),str(mode)],check=True,text=True,capture_output=True);print(r.stdout.splitlines()[0]);logs.append(r.stdout)
  payload=json.loads(next(line[5:]for line in r.stdout.splitlines()if line.startswith('JSON ')))
  assert len(payload['hw'])==15 and len(payload['link_fit'])==2
  assert all(type(x)is int for x in payload['hw']+payload['link_fit'])
  assert len(payload['drain'])==8 and all(type(x)is int for x in payload['drain'])
  assert len(payload['fault_record'])==16 and len(payload['owner'])==5 and len(payload['watchdog'])==7
  assert len(payload['coordination'])==4 and type(payload['rx_suppressed']) is bool
  assert len(payload['launch'])==5 and all(type(x)is int for x in payload['launch'])
  assert payload['rtt_reference']=='TX_BUSY_START_RX_HEADER' and len(payload['rtt_events'])==8 and type(payload['watch_rate_ppb']) is int and type(payload['watch_coarse_phase_ns']) is int
  assert bytes.fromhex(payload['sender_name_hex']).decode()==payload['sender_name']
  assert len(payload['name'])==6 and len(payload['display'])==3 and len(payload['signal'])==4 and len(payload['fit_provenance'])==3
  if mode==36:assert payload['hw'][12:]==[9,7,0x83]
 probe=a.out/'probe';subprocess.run([*flags,'-DR2_LORA_SERVICE_BODY="'+str(body.resolve())+'"',*map(str,[APP/'tests/lora_probe_test.c',*common]),'-o',str(probe)],check=True)
 for mode in range(1,29):subprocess.run([str(probe),str(mode)],check=True)
 ui=a.out/'ui';subprocess.run([*flags,'-DR2_LORA=1',str(APP/'tests/lora_ui_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),'-o',str(ui)],check=True);subprocess.run([str(ui),str(a.out)],check=True)
 for name in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/random/random.h']:
  h=a.out/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Fake UI boundary. */\n')
 witness=a.out/'witness';subprocess.run([*flags,'-I'+str(a.out.resolve()),str(APP/'tests/shadow_worker_test.c'),str(APP/'src/radio_precision.c'),str(APP/'src/rc_boundary.c'),*map(str,common),'-o',str(witness)],check=True);subprocess.run([str(witness)],check=True)
 session=a.out/'witness-session';subprocess.run([*flags,str(APP/'tests/shadow_session_test.c'),'-o',str(session)],check=True);subprocess.run([str(session)],check=True)
 uiworker=a.out/'uiworker';subprocess.run([*flags,'-DR2_LORA=1','-I'+str(a.out.resolve()),str(APP/'tests/lora_ui_worker_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),*map(str,common),'-o',str(uiworker)],check=True)
 for mode in range(42,52):subprocess.run([str(uiworker),str(mode)],check=True)
 clockworker=a.out/'clockworker';subprocess.run([*flags,'-DR2_LORA=1','-I'+str(a.out.resolve()),str(APP/'tests/lora_ui_clock_worker_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),*map(str,common),'-o',str(clockworker)],check=True)
 for fraction in [0,1,499,999]:subprocess.run([str(clockworker),str(fraction)],check=True)
 main=(APP/'src/main.c').read_text()
 helper=main[main.index('static void lora_ui_view('):main.index('\n#endif',main.index('static void lora_ui_view('))]
 gm=main[main.index('static uint64_t __attribute__((noinline)) gm7_ui_publish('):main.index('\n#endif\nint main(void)',main.index('static uint64_t __attribute__((noinline)) gm7_ui_publish('))]
 normal=main[main.index('      struct r2_ui_model m=',main.index('    if(now>=ui_due)')):main.index('\n#endif\n    }',main.index('    if(now>=ui_due)'))]
 normal=normal.replace('ui_due=m.sampled_ms+20;','(void)(m.sampled_ms+20);')
 name=main[main.index('    if(r2_lora_source_needed()){'):main.index('\n    r2_lora_control(',main.index('    if(r2_lora_source_needed()){'))]
 (a.out/'main_lora_ui_under_test.h').write_text(helper+'\n'+gm+'\nstatic void normal_ui_publish(uint64_t now,bool platform_ready){\n'+normal+'\n}\nstatic void publish_lora_name(void){\n'+name+'\n}\n')
 mainui=a.out/'mainui';subprocess.run([*flags,'-DR2_LORA=1','-DR2_NATIVE_BLE=1','-I'+str(a.out.resolve()),str(APP/'tests/lora_main_ui_test.c'),'-o',str(mainui)],check=True);subprocess.run([str(mainui)],check=True)
 front3=a.out/'front3';subprocess.run([*flags,'-DR2_LORA=1',str(APP/'tests/lora_front3_test.c'),str(APP/'src/precision.c'),'-o',str(front3)],check=True);subprocess.run([str(front3),str(a.out)],check=True)
 (a.out/'runtime.log').write_text(''.join(logs))
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FAKE_BOUNDARIES_ONLY',modes=125,probe_modes=28,json_hw_fields=15,json_launch_fields=5,json_payload_limit=3072-96,json_nonzero_abort_fields=[9,7,0x83],source_parity=True,source_hashes={str(x.relative_to(APP)):hashlib.sha256(x.read_bytes()).hexdigest()for x in [APP/'src/lora_service.c',APP/'src/lora_source.c',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'src/rc_shadow.c',APP/'include/rc_shadow.h',APP/'include/shadow_settle.h',APP/'tests/shadow_worker_test.c',APP/'tests/shadow_session_test.c',APP/'src/lora_rc8_policy.c',APP/'include/lora_rc8_policy.h',APP/'tests/lora_service_test.c',APP/'tests/lora_probe_test.c',APP/'tests/lora_source_test.c',APP/'src/main.c',APP/'src/ui_render.c',APP/'src/ui_service.c',APP/'legacy_ui/legacy.h',APP/'legacy_ui/draw.inc',APP/'tests/lora_ui_test.c',APP/'tests/lora_ui_clock_worker_test.c',APP/'tests/lora_main_ui_test.c',APP/'tests/lora_front3_test.c',Path(__file__)]}),indent=2)+'\n')
if __name__=='__main__':main()
