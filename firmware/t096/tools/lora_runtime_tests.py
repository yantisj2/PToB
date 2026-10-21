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
 for mode in range(1,68):
  r=subprocess.run([str(binary),str(mode)],check=True,text=True,capture_output=True);print(r.stdout.splitlines()[0]);logs.append(r.stdout)
  payload=json.loads(next(line[5:]for line in r.stdout.splitlines()if line.startswith('JSON ')))
  assert len(payload['hw'])==15 and len(payload['link_fit'])==2
  assert all(type(x)is int for x in payload['hw']+payload['link_fit'])
  assert len(payload['fault_record'])==16 and len(payload['owner'])==5 and len(payload['watchdog'])==7
  assert len(payload['coordination'])==4 and type(payload['rx_suppressed']) is bool
  assert len(payload['launch'])==5 and all(type(x)is int for x in payload['launch'])
  if mode==36:assert payload['hw'][12:]==[9,7,0x83]
 ui=a.out/'ui';subprocess.run([*flags,'-DR2_LORA=1',str(APP/'tests/lora_ui_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),'-o',str(ui)],check=True);subprocess.run([str(ui),str(a.out)],check=True)
 for name in ['zephyr/kernel.h','zephyr/sys/atomic.h','zephyr/random/random.h']:
  h=a.out/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text('/* Fake UI boundary. */\n')
 uiworker=a.out/'uiworker';subprocess.run([*flags,'-DR2_LORA=1','-I'+str(a.out.resolve()),str(APP/'tests/lora_ui_worker_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),*map(str,common),'-o',str(uiworker)],check=True)
 for mode in range(42,52):subprocess.run([str(uiworker),str(mode)],check=True)
 (a.out/'runtime.log').write_text(''.join(logs))
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FAKE_BOUNDARIES_ONLY',modes=67,json_hw_fields=15,json_launch_fields=5,json_payload_limit=3072-96,json_nonzero_abort_fields=[9,7,0x83],source_parity=True,source_hashes={str(x.relative_to(APP)):hashlib.sha256(x.read_bytes()).hexdigest()for x in [APP/'src/lora_service.c',APP/'src/lora_source.c',APP/'include/lora_service.h',APP/'include/lora_watchdog.h',APP/'tests/lora_service_test.c',APP/'tests/lora_source_test.c']}),indent=2)+'\n')
if __name__=='__main__':main()
