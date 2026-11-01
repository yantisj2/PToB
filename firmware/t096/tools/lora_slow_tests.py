#!/usr/bin/env python3
"""Slow production owner/core checks at fake boundaries. MIT, Jonathan Yantis."""
import argparse,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
 crypto=a.deps.resolve()/'modules/crypto/tinycrypt/lib';cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
 body=a.out/'body.c';body.write_text(re.sub(r'^#include[^\n]*\n','',(APP/'src/lora_passive_service.c').read_text(),flags=re.M))
 flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-default-const-init-var-unsafe','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_BACKUP_TRANSMITTER_FACTORY=3','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(crypto/'include'),'-DR2_PASSIVE_SERVICE_BODY="'+str(body)+'"']
 sources=[APP/'tests/lora_slow_test.c',APP/'src/lora_passive_core.c',APP/'protocol/lora_passive.c',APP/'src/lora_rc8_policy.c',APP/'protocol/lora_rc8.c',APP/'protocol/group_auth.c',*[crypto/'source'/(n+'.c') for n in ('hmac','sha256','utils')]]
 binary=a.out/'slow';subprocess.run([*flags,*map(str,sources),'-o',str(binary)],check=True)
 logs=[]
 for mode in range(100,115):
  r=subprocess.run([str(binary),str(mode)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(a.out/'tests.log').write_text(''.join(logs));r.check_returncode()
  for line in r.stdout.splitlines():
   if line.startswith('DIAG '):assert json.loads(line[5:])['profile']=='PROD_60_300'
  print('PASS slow',mode)
 (a.out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_BOUNDARIES',cases=15,hardware=False),indent=2)+'\n')
if __name__=='__main__':main()
