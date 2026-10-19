#!/usr/bin/env python3
"""PC production estimator/scheduler tests with ASan/UBSan, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
source=(app/'src/rc_boundary.c').read_text()
arithmetic=source[source.index('static bool map_value('):source.index('bool rc5_clock_discipline(')]
arithmetic+=source[source.index('bool rc5_map_project('):source.index('bool rc5_clock_capture(')]
(out/'pc_projection_under_test.h').write_text(arithmetic)
cc=os.environ.get('CC') or '/opt/homebrew/opt/llvm/bin/clang'
cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-DR2_PC_PROFILE=1','-DR2_RC_PROFILE=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(app/'protocol'),
 str(app/'src/pc_experiment.c'),str(app/'src/core.c'),str(app/'tests/pc_experiment_test.c'),'-o',str(out/'test')]
with (out/'test.log').open('x') as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
(out/'receipt.json').write_text(json.dumps({'status':'INCOMPLETE','hardware_access':False,'command':cmd,'sources':{str(x.relative_to(app)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [app/'src/pc_experiment.c',app/'src/rc_boundary.c',app/'src/core.c',app/'tests/pc_experiment_test.c',Path(__file__).resolve()]}},indent=2)+'\n')
print((out/'test.log').read_text(),end='')
source=(app/'src/pc_profile_service.c').read_text();start=source.index('int r2_service_configure(');end=source.index('{',start)+1;depth=1
while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
(out/'pc_identity_under_test.h').write_text(source[start:end])
identity_cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-I'+str(out),str(app/'tests/pc_identity_test.c'),'-o',str(out/'identity')]
with (out/'identity.log').open('x') as log:
 subprocess.run(identity_cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 subprocess.run([str(out/'identity')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
print((out/'identity.log').read_text(),end='')
r=json.loads((out/'receipt.json').read_text());r['identity_command']=identity_cmd
subprocess.run([sys.executable,str(app/'tests/test_pc_analyze.py')],check=True,timeout=30)
for name in ('src/pc_profile_service.c','tests/pc_identity_test.c','tools/pc_analyze.py','tests/test_pc_analyze.py'):r['sources'][name]=hashlib.sha256((app/name).read_bytes()).hexdigest()
r['status']='PASS'
(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n')
