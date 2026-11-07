#!/usr/bin/env python3
"""Actual main RF comparison binding; synthetic owner data, no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();A=Path(__file__).resolve().parents[1];O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
files=[A/'src/ui_service.c',A/'include/ui.h',A/'include/rf_clock.h',A/'tests/ui_rf_compare_test.c',Path(__file__)]
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();inputs={str(f):sha(f)for f in files}
main=(A/'src/ui_service.c').read_text();start=main.index('static void __attribute__((noinline)) optional_rf_view(');end=main.index('\n}\n',start)+3;(O/'rf_ui_under_test.h').write_text(main[start:end].replace('optional_rf_view(', 'rf_ui_view('))
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-I'+str(A/'include'),'-I'+str(A/'protocol'),'-I'+str(O),str(A/'tests/ui_rf_compare_test.c'),'-o',str(O/'test')]
r=dict(status='INCOMPLETE',hardware=False,inputs=inputs,limits='Actual main binding with fake capture/owner responses. Backend model projection and hardware/native bounds are separate checks.')
try:
 for label,args in [('compile',cmd),('run',[str(O/'test')])]:
  with(O/(label+'.log')).open('x')as out:subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=60)
 assert all(sha(f)==h for f,h in inputs.items());r['status']='PASS'
finally:(O/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
print('PASS actual main RF binding:',O)
