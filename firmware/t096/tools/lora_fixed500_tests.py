#!/usr/bin/env python3
"""Fixed500 actual owner/core/driver with synthetic boundaries; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 def source_manifest():return {str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in APP.rglob('*')if f.is_file()and f.suffix in('.c','.h','.inc','.py')and '__pycache__'not in f.parts}
 before=source_manifest()
 owner=a.out.resolve()/'owner.c';owner.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');admission=a.out.resolve()/'admission.c';admission.write_text(hw[start:hw.index('\n#endif\nstatic int ',start)])
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(APP/'tests'),'-I'+str(tiny/'include')]
 sources=[APP/'tests/lora_fixed500_test.c',APP/'src/lora_passive_core.c',APP/'src/lora_receiver_watch.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in('hmac','sha256','utils')]]
 binary=a.out.resolve()/'owner';subprocess.run([*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(owner)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(binary)],check=True)
 logs=[]
 for mode in (1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32):
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(a.out/'owner.log').write_text(''.join(logs));r.check_returncode();print(r.stdout.strip())
 for correction in (123456789,-123456789):
  calibrated=a.out.resolve()/('owner-calibrated-'+str(correction))
  subprocess.run([*flags,'-DR2_LORA_FIXED500_EVENT_DELAY_NS='+str(correction),'-DR2_PASSIVE_SERVICE_BODY="'+str(owner)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(calibrated)],check=True)
  subprocess.run([str(calibrated),'4'],check=True)
 body=a.out.resolve()/'hw.c';body.write_text(re.sub(r'^#include[^\n]*\n','',hw,flags=re.M))
 fixture=a.out.resolve()/'driver.c';fixture.write_text('#define main original_main\n#include "'+str(APP/'tests/lora_hw_test.c')+'"\n#undef main\nint main(void){assert(!r2_lora_hw_start(4,false));for(unsigned p=1;p<=2;p++){assert(!r2_lora_hw_passive_configure(p,50));uint64_t hz=922400000;uint32_t expected=(uint32_t)((hz*((uint64_t)1<<25)+16000000)/32000000);uint32_t actual=((uint32_t)configured_frequency[0]<<24)|((uint32_t)configured_frequency[1]<<16)|((uint32_t)configured_frequency[2]<<8)|configured_frequency[3];assert(actual==expected&&configured_modulation[0]==11&&configured_modulation[1]==6&&configured_modulation[2]==4&&!configured_modulation[3]);assert(r2_lora_hw_budget_us(24)==273168&&r2_lora_hw_budget_us(32)==305936&&r2_lora_hw_budget_us(112)==797456&&!r2_lora_hw_budget_us(28));regs[0x0889]=0xa5;assert(!r2_lora_hw_prepare(payload,24,5));assert(regs[0x0889]==0xa1);}return 0;}\n')
 driver=a.out.resolve()/'driver';subprocess.run([*flags,'-DR2_LORA_OWNER_BODY="'+str(body)+'"',str(fixture),'-o',str(driver)],check=True);subprocess.run([str(driver)],check=True)
 meta_sources=[APP/'tests/lora_meta_processing_test.c',*sources[1:]]
 for fixed in (True,False):
  meta_flags=flags if fixed else [f for f in flags if f!='-DR2_LORA_FIXED500_EXPERIMENT=1']
  meta_binary=a.out.resolve()/('meta-fixed500'if fixed else'meta-normal-slow')
  subprocess.run([*meta_flags,*map(str,meta_sources),'-o',str(meta_binary)],check=True);subprocess.run([str(meta_binary)],check=True)
 ui=a.out.resolve()/'tx-indicator'
 subprocess.run([*flags,'-DR2_LORA=1',str(APP/'tests/lora_tx_indicator_test.c'),str(APP/'src/precision.c'),'-o',str(ui)],check=True)
 subprocess.run([str(ui),str(a.out.resolve())],check=True)
 files=[*sources,APP/'include/lora_receiver_watch.h',APP/'include/lora_passive_core.h',APP/'include/lora_service.h',APP/'tests/lora_meta_processing_test.c',APP/'tests/lora_tx_indicator_test.c',APP/'src/ui_render.c',APP/'legacy_ui/draw.inc',APP/'src/lora_passive_service.c',APP/'src/lora_hw.c',APP/'include/lora_hw.h',APP/'protocol/lora_passive_schedule.h',APP/'tests/lora_ram_fixture.h',Path(__file__)]
 assert before==source_manifest(),'T096 source changed during test; receipt refused'
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FIXED500_SYNTHETIC_OWNER_CORE_DRIVER',hardware=False,cases=31,tx_indicator_cases=18,schedule=8,frequency_hz=922400000,sf=11,cr='4/8',META_processing_us=1787456,normal_slow_META_processing_us=1000000,source_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
 print('PASS actualdriver fixed922.4MHz/SF11/CR8/exactguardedbudgets/TxModbit2')
if __name__=='__main__':main()
