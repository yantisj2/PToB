#!/usr/bin/env python3
"""Finite non-timing TX owner at fake boundaries; no physical claims.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');body=a.out/'body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 sources=[APP/'tests/lora_tx_diagnostic_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 hwbody=a.out/'hw_admission_body.c';hw=(APP/'src/lora_hw.c').read_text();start=hw.index('static struct r2_lora_hw_rejection rejected;');end=hw.index('\n#endif\nstatic int tx_before(',start);hwbody.write_text(hw[start:end])
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_FAST=1','-DR2_LORA_TX_DIAGNOSTIC=1','-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"','-DR2_HW_ADMISSION_BODY="'+str(hwbody)+'"','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')];binary=a.out/'diagnostic'
 subprocess.run([*flags,*map(str,sources),'-o',str(binary)],check=True);logs=[]
 for mode in [1,2,4,5,6,7,8,9,10]:
  r=subprocess.run([str(binary),str(mode)],text=True,capture_output=True);logs.append(r.stdout+r.stderr);(a.out/'diagnostic.log').write_text(''.join(logs));r.check_returncode();print(r.stdout.strip())
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FAKE_BOUNDARIES_ONLY',cases=9,hardware=False,sources={str(x.relative_to(APP)):hashlib.sha256(x.read_bytes()).hexdigest()for x in [*sources,APP/'src/lora_passive_service.c',Path(__file__)]if x.is_relative_to(APP)}),indent=2)+'\n')
if __name__=='__main__':main()
