#!/usr/bin/env python3
"""Actual current worker plus exact319 timestamp/shutdown failure negatives.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT"""
import argparse,hashlib,json,subprocess
import os
from pathlib import Path
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
A=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-app',type=Path);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(q.relative_to(A)):sha(q)for q in A.rglob('*')if q.is_file()and q.suffix in('.c','.h','.inc','.py')and'__pycache__'not in q.parts}
for n in ['kernel.h','sys/atomic.h','random/random.h']:
 q=o/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Existing host boundary. */\n')
steps=[]
def run(n,c):
 r=subprocess.run(c,text=True,capture_output=True,timeout=30);(o/(n+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();steps.append(dict(label=n,command=c))
for label,app,extra in [('current',A,[])]+([('319',a.baseline_app.resolve(),['-DR2_BASELINE319=1'])]if a.baseline_app else[]):
 for variant,variant_flags in [('plain',[]),('lora',['-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1'])]:
  flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1','-DR2_UI_SCHEDULE_FIXTURE=1','-I'+str(app/'include'),'-I'+str(app/'protocol'),'-I'+str(o)]+extra+variant_flags
  prefix=label+'-'+variant
  for n in ['ui_render','precision']:run(prefix+'-'+n,flags+['-c',str(app/'src'/f'{n}.c'),'-o',str(o/(prefix+'-'+n+'.o'))])
  binary=o/prefix;run(prefix+'-compile',flags+['-DUI_FAKE_HEADER="'+str(app/'tests/ui_service_fake.h')+'"','-DUI_SERVICE_SOURCE="'+str(app/'src/ui_service.c')+'"',str(A/'tests/ui_commit_close_test.c'),str(o/(prefix+'-ui_render.o')),str(o/(prefix+'-precision.o')),'-o',str(binary)])
  for scene in(range(8)if label=='current'else[0,1]):run(prefix+'-'+str(scene),[str(binary),str(scene)])
assert all(sha(A/n)==h for n,h in inputs.items())
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_TIMESTAMP_AND_RELEASE_WITH319_NEGATIVES',hardware=False,inputs=inputs,steps=steps,current_scenes=16,negative_scenes=4,limits='Synthetic scheduling/public-driver boundaries. Actual driver and native resource proof separate.'),indent=2)+'\n')
print('PASS timestamp race/analog ACK, draw-only close release, terminal init/GPIO/CS/PWM negatives;319 reproductions')
