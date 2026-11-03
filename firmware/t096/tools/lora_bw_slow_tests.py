#!/usr/bin/env python3
"""Actual finite SLOW-derived bandwidth owner; synthetic boundaries only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
 owner=a.out.resolve()/'owner.c';owner.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');admission=a.out.resolve()/'admission.c';admission.write_text(hw[start:hw.index('\n#endif\nstatic int ',start)])
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_BW_EXPERIMENT=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(APP/'tests'),'-I'+str(tiny/'include')]
 sources=[APP/'tests/lora_bw_slow_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in('hmac','sha256','utils')]]
 binary=a.out.resolve()/'owner';subprocess.run([*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(owner)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(binary)],check=True)
 logs=[]
 for mode in (1,2,3,4,5,6,7,8,9,10):
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(a.out/'owner.log').write_text(''.join(logs));r.check_returncode();print(r.stdout.strip())
 body=a.out.resolve()/'hw.c';body.write_text(re.sub(r'^#include[^\n]*\n','',hw,flags=re.M))
 fixture=a.out.resolve()/'driver.c';fixture.write_text('#define main original_main\n#include "'+str(APP/'tests/lora_hw_test.c')+'"\n#undef main\nint main(void){assert(!r2_lora_hw_start(4,false));const unsigned phys[]={2,4,5,6,3},codes[]={10,3,4,5,6},sync[]={231951,157968,83984,46992,28496},follow[]={268814,182544,96272,53136,31568};for(unsigned i=0;i<5;i++){for(unsigned j=0;j<2;j++){unsigned channel=j?48:16;assert(!r2_lora_hw_passive_configure(phys[i],channel));uint64_t hz=902400000+400000*channel;uint32_t expected=(uint32_t)((hz*((uint64_t)1<<25)+16000000)/32000000);uint32_t actual=((uint32_t)configured_frequency[0]<<24)|((uint32_t)configured_frequency[1]<<16)|((uint32_t)configured_frequency[2]<<8)|configured_frequency[3];assert(actual==expected&&configured_modulation[0]==7&&configured_modulation[1]==codes[i]&&configured_modulation[2]==2&&!configured_modulation[3]);assert(r2_lora_hw_budget_us(24)==sync[i]&&r2_lora_hw_budget_us(32)==follow[i]);regs[0x0889]=0xa1;assert(!r2_lora_hw_prepare(payload,24,5));assert(regs[0x0889]==(i==4?0xa1:0xa5));}assert(!r2_lora_hw_passive_configure(R2_LORA_DTS,0));assert(!atomic_get(&prepared_budget));}return 0;}\n')
 driver=a.out.resolve()/'driver';subprocess.run([*flags,'-DR2_LORA_OWNER_BODY="'+str(body)+'"',str(fixture),'-o',str(driver)],check=True);subprocess.run([str(driver)],check=True)
 print('PASS actualdriver5SF7BWs/exactfixedRFregisters/CR/preamble-independentbudgets/TxModbit2/tokenretirement')
 files=[*sources,APP/'src/lora_passive_service.c',APP/'src/lora_hw.c',APP/'include/lora_hw.h',APP/'protocol/lora_passive_schedule.h',Path(__file__)]
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_FINITE_SLOW_BW_LAB',hardware=False,cases=10,lab_source_hold_us=5000000,driver_arms=5,schedule=7,period_us=30000000,arm_us=600000000,audit_bytes=48,source_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
if __name__=='__main__':main()
