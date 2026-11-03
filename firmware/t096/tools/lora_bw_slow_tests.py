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
 for mode in (1,2,3,4,5):
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(a.out/'owner.log').write_text(''.join(logs));r.check_returncode();print(r.stdout.strip())
 files=[*sources,APP/'src/lora_passive_service.c',APP/'protocol/lora_passive_schedule.h',Path(__file__)]
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_FINITE_SLOW_BW_LAB',hardware=False,cases=5,schedule=6,period_us=30000000,arm_us=480000000,audit_bytes=48,source_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
if __name__=='__main__':main()
