#!/usr/bin/env python3
"""Actual scanner/owner/service startup lifetime, original negative and WD bounds.
MIT. Copyright(c)2026 Jonathan Yantis. No hardware.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);a=p.parse_args();O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fn(s,name):
 m=re.search(r'^[^\n]*\b'+name+r'\([^;]*?\)\n\{',s,re.M);assert m,name
 start=s.index('{',m.start());depth=1;i=start+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
def noincludes(s):return re.sub(r'^#(?:include|define R2_LOCK_UNIT)[^\n]*\n','',s,flags=re.M)
# Compose actual C translation units; only rename private generation symbol to
# avoid a collision. Struct fields and all executable bodies otherwise remain.
hw=noincludes((APP/'src/lora_hw.c').read_text());hw=re.sub(r'(?<![.])(?<!->)\bgeneration\b','hw_generation',hw);(O/'actual_hw.inc').write_text(hw)
fixture=(APP/'tests/lora_hw_test.c').read_text().split('static void inject(int flags)')[0].replace('#include R2_LORA_OWNER_BODY','#include "actual_hw.inc"')
(O/'actual_hw_fixture.inc').write_text(fixture)
diagnostic=(APP/'src/diagnostic.c').read_text();selected='\n'.join(fn(diagnostic,n)for n in ['blank','binding','valid5','valid6','valid_record','valid_crash','event_binding_any_source','valid_event_any_source','protected_page','select_target','r2_diag_init'])
for old,new in [('phase','diag_phase'),('generation','diag_generation'),('binding','scan_binding')]:selected=re.sub(r'(?<![.])(?<!->)\b'+old+r'\b',new,selected)
# A deterministic high-priority observer at the flash-read and CRC-loop cuts;
# these probes are test-only, never generated into firmware.
anchor='crc^=bytes[k];';assert selected.count(anchor)==1;selected=selected.replace(anchor,'if(k==256)observer_probe(1);'+anchor)
(O/'actual_diag.inc').write_text('/* Selected diagnostic.c bodies retain SPDX-License-Identifier: Apache-2.0. */\n'+selected+'\n')
(O/'zephyr').mkdir();(O/'zephyr/kernel.h').write_text('/* Host boundary is declared by fixture. */\n')
main=(APP/'src/main.c').read_text();watchdog=(APP/'src/watchdog.c').read_text()
assert main.index('r2_watchdog_boot(&identity)')<main.index('r2_diag_init(&identity)')<main.index('r2_lora_init(')
assert 'r2_lora_watchdog_check();'in fn(watchdog,'watchdog_step')
service=noincludes((APP/'src/lora_service.c').read_text())
assert service.index('if(!atomic_get(&observer_ready))return;')<service.index('uint32_t beat=r2_diag_scratch.lora.beat')
assert service.index('atomic_set(&observer_ready,1);')<service.index('k_thread_create(')
service=service.replace('atomic_set(&observer_ready,0);','atomic_set(&observer_ready,0);init_probe(1);',1).replace(' binding=(struct rc8_enrollment)',' init_probe(2);\n binding=(struct rc8_enrollment)',1).replace(' atomic_set(&observer_ready,1);',' init_probe(3);\n atomic_set(&observer_ready,1);',1)
C=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(C/'include'),'-I'+str(O)]
passed=[]
for fixed in [False,True]:
 name='fixed'if fixed else'original';body=service
 if not fixed:
  for statement in ['atomic_set(&observer_ready,0);','atomic_set(&observer_ready,1);','if(!atomic_get(&observer_ready))return;']:
   assert body.count(statement)==1;body=body.replace(statement,'',1)
 (O/'actual_service.inc').write_text(body);shutil.copyfile(O/'actual_service.inc',O/('actual_service-'+name+'.inc'))
 cmd=[*flags,*(['-DFIXED=1']if fixed else[]),str(APP/'tests/lora_init_gate_test.c'),str(APP/'src/core.c'),str(APP/'src/lora_rc8_policy.c'),str(APP/'protocol/lora_rc8.c'),str(APP/'protocol/group_auth.c'),*[str(C/'source'/(n+'.c'))for n in ['hmac','sha256','utils']],'-o',str(O/name)]
 with(O/(name+'-build.log')).open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 for mode in [1,2,3]:
  with(O/(name+'-'+str(mode)+'.log')).open('x')as log:subprocess.run([str(O/name),str(mode)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
  passed.append(name+'-'+str(mode))
(O/'receipt.json').write_text(json.dumps({'hardware':False,'passed':passed,'readiness_storage':'one atomic_t outside shared512B union; native RAM delta measured separately','production_bodies':['r2_diag_init, valid schema/CRC and select_target','r2_lora_init, service_step, begin_stop, watchdog_check/poll','full lora_hw body including first-fault and sticky hardware fault'],'test_only_changes':['private hardware generation symbol renamed for TU composition','explicit observer calls at fake flash-read, actual CRC-loop cut and initialization cuts','original negative removes only readiness clear/publish/read gate'],'inputs':{str(p.relative_to(APP)):sha(p)for p in [APP/'src/lora_service.c',APP/'src/lora_hw.c',APP/'src/diagnostic.c',APP/'src/main.c',APP/'src/watchdog.c',APP/'tests/lora_hw_test.c',APP/'tests/lora_init_gate_test.c',APP/'tools/lora_init_gate_tests.py']},'limits':['Synthetic valid schema5 record only; no private flash/raw data','Scheduled observer/peripheral/kernel/flash/reset model boundaries; not exact prior physical scanner record proof','Controller/RF behavior and existing watchdog30s bound unchanged; native/field gates separate']},indent=2)+'\n')
print('PASS composed actual scanner/LoRa initialization: old negative, preinit no-side-effect gate, publication ordering and original30s observer bounds')
