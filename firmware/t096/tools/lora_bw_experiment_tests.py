#!/usr/bin/env python3
"""Finite SF7 bandwidth experiment, actual owner/driver and namespace guards.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
No physical operation or accuracy qualification.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
 tiny=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 owner=out/'owner.c';owner.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 hw=(APP/'src/lora_hw.c').read_text();admission=out/'admission.c';start=hw.index('static struct r2_lora_hw_rejection rejected;');admission.write_text(hw[start:hw.index('\n#endif\nstatic int ',start)])
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_FAST=1','-DR2_LORA_BW_EXPERIMENT=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(APP/'tests'),'-I'+str(tiny/'include')]
 sources=[APP/'tests/lora_bw_experiment_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[tiny/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 binary=out/'owner';subprocess.run([*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(owner)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(binary)],check=True)
 logs=[]
 for mode in (1,2,3,4,5,6,8,9,11,12):
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(out/'owner.log').write_text(''.join(logs));r.check_returncode();print(r.stdout.strip())
 negative=out/'phase200.c';negative.write_text('#undef LP_LAUNCH_LATE_US\n#define LP_LAUNCH_LATE_US UINT64_C(45000)\n#undef LP_LAUNCH_PAD_US\n#define LP_LAUNCH_PAD_US UINT64_C(50000)\n'+owner.read_text().replace('#define SOURCE_GRID_PHASE_NS UINT64_C(400000000)','#define SOURCE_GRID_PHASE_NS UINT64_C(200000000)'))
 old=out/'owner-phase200';subprocess.run([*flags,'-DR2_PASSIVE_SERVICE_BODY="'+str(negative)+'"','-DR2_HW_ADMISSION_BODY="'+str(admission)+'"',*map(str,sources),'-o',str(old)],check=True)
 r=subprocess.run([str(old),'7'],capture_output=True,text=True);(out/'phase200.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
 # Full real hardware driver; include a fixture-only extra main to inspect
 # both modulation configurations, exact packet budgets and token retirement.
 body=out/'hw.c';body.write_text(re.sub(r'^#include[^\n]*\n','',hw,flags=re.M))
 fixture=out/'driver.c';fixture.write_text('#define main original_main\n#include "'+str(APP/'tests/lora_hw_test.c')+'"\n#undef main\nint main(void){assert(!r2_lora_hw_start(4,false));for(unsigned i=0;i<4;i++){unsigned phy=i==1||i==2?R2_LORA_FHSS_BW500:R2_LORA_FHSS;assert(!r2_lora_hw_passive_configure(phy,i));assert(configured_modulation[0]==7&&configured_modulation[1]==(phy==3?6:10)&&configured_modulation[2]==2&&!configured_modulation[3]);assert(r2_lora_hw_budget_us(24)==(phy==3?28496:231951));assert(r2_lora_hw_budget_us(32)==(phy==3?31568:268814));regs[0x0889]=0xa5;assert(!r2_lora_hw_prepare(payload,24,5));assert(regs[0x0889]==(phy==3?0xa1:0xa5));assert(!r2_lora_hw_passive_configure(R2_LORA_DTS,0));assert(!atomic_get(&prepared_budget));}return 0;}\n')
 driver=out/'driver';subprocess.run([*flags,'-DR2_LORA_OWNER_BODY="'+str(body)+'"',str(fixture),'-o',str(driver)],check=True);subprocess.run([str(driver)],check=True)
 # Illegal build combinations fail at the portable header before any hardware.
 bad=out/'bad.c';bad.write_text('#include "lora_passive_schedule.h"\n')
 for define in ('R2_LORA_PASSIVE_SLOW=1','R2_LORA_TX_DIAGNOSTIC=1'):
  r=subprocess.run([cc,'-fsyntax-only','-DR2_LORA_BW_EXPERIMENT=1','-DR2_LORA_PASSIVE_FAST=1','-D'+define,'-I'+str(APP/'protocol'),str(bad)],capture_output=True,text=True);assert r.returncode
 # BW500 airtimes: explicit header, CRC, SF7, CR4/6, preamble12.
 def budget(n):return int((12+4.25+8+((8*n-4*7+28+16+27)//28)*6)*256)+10000
 assert [budget(n)for n in(24,32)]==[28496,31568]
 files=[*sources,APP/'src/lora_passive_service.c',APP/'src/lora_hw.c',APP/'include/lora_hw.h',APP/'protocol/lora_passive_schedule.h',Path(__file__)]
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS_FINITE_LAB_AB_NOT_PHYSICAL_QUALIFICATION',hardware=False,owner_cases=11,driver_arms=4,illegal_combinations=2,grant_model='Original1.312476s maps at337ms per second; modeled acquisition only, no physical feasibility claim; original1.1s room fails META without renewal',source_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
 print('PASS sameSF7 PHY/budget oracle, modulation bits, retune token invalidation and illegal builds')
if __name__=='__main__':main()
