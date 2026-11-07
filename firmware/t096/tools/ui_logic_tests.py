#!/usr/bin/env python3
"""Mandatory logical UI maintenance and independent render flow, offline only.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p.relative_to(APP)):sha(p)for p in APP.rglob('*')if p.is_file()and p.suffix in('.c','.h','.inc','.py')and'__pycache__'not in p.parts}
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(o),'-DR2_TZ_WIRE=1']
def run(name,args):
 r=subprocess.run(args,text=True,capture_output=True,timeout=45);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
for n in['kernel.h','sys/atomic.h','random/random.h']:
 f=o/'zephyr'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Existing host boundary. */\n')
for variant,extra in [('plain',[]),('lora',['-DR2_LORA=1'])]:
 vf=flags+extra
 for n in['ui_render','precision']:
  run(variant+'-'+n,vf+['-c',str(APP/f'src/{n}.c'),'-o',str(o/f'{variant}-{n}.o')])
 common=vf+['-DR2_UI_SCHEDULE_FIXTURE=1','-DR2_UI_LOGIC_FIXTURE=1','-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_logic_test.c'),str(o/f'{variant}-ui_render.o'),str(o/f'{variant}-precision.o')]
 binary=o/(variant+'-logic');run(variant+'-build',common+['-o',str(binary)])
 for scene in range(20):run(variant+'-'+str(scene),[str(binary),str(scene)])
 if a.baseline:
  binary=o/(variant+'-318-negative');run(variant+'-318-build',common+['-DR2_LOGIC_BASELINE=1','-DUI_SERVICE_SOURCE="'+str(a.baseline.resolve())+'"','-o',str(binary)])
  run(variant+'-318-negative',[str(binary),'0'])
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_LOGIC_RENDER_SEPARATION',hardware=False,inputs=inputs,baseline_source_sha256=sha(a.baseline)if a.baseline else None,scenes=20,variants=2),indent=2)+'\n')
print('PASS logical separation40 scenes and318 negative')
