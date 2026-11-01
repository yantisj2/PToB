#!/usr/bin/env python3
"""Actual fast owner with fake hardware/flash/capture; no physical claims.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,re,subprocess,hashlib
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib'
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_FAST=1','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include')]
 body=a.out/'body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 binary=a.out/'owner';sources=[APP/'tests/lora_passive_service_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c')for n in ('hmac','sha256','utils')]]
 subprocess.run([*flags,'-Wno-default-const-init-var-unsafe','-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"',*map(str,sources),'-o',str(binary)],check=True)
 logs=[]
 for mode in [1,2,3,4,5,6,7,10,11,12,13,14]:
  r=subprocess.run([str(binary),str(mode)],text=True,capture_output=True);logs.append(r.stdout+r.stderr);(a.out/'owner.log').write_text(''.join(logs));r.check_returncode()
  result=json.loads(next(x[5:]for x in r.stdout.splitlines()if x.startswith('JSON ')));assert result['profile']=='LAB_FAST_4_12' and result['rc7_authority'] is False
  print(r.stdout.splitlines()[0])
 store_body=a.out/'store_body.c';store_body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_store.c').read_text(),flags=re.M))
 store=a.out/'store';subprocess.run([*flags,'-DR2_PASSIVE_STORE_BODY="'+str(store_body)+'"',str(APP/'tests/lora_passive_store_test.c'),str(APP/'src/core.c'),'-o',str(store)],check=True)
 for mode in range(1,9):subprocess.run([str(store),str(mode)],check=True)
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_FAKE_BOUNDARIES_ONLY',owner_cases=12,storage_cases=8,hardware=False,sources={str(x.relative_to(APP)):hashlib.sha256(x.read_bytes()).hexdigest()for x in sources if x.is_relative_to(APP)}),indent=2)+'\n')
if __name__=='__main__':main()
