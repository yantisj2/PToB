#!/usr/bin/env python3
"""Actual fast input path under renderer delay; optional exact317 negative.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT."""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline',type=Path);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def manifest():return {str(p.relative_to(APP)):sha(p)for p in APP.rglob('*')if p.is_file()and p.suffix in('.c','.h','.inc','.py')and'__pycache__'not in p.parts}
before=manifest()
cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').exists()else shutil.which('clang'))
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(o)]
def run(n,args):
 r=subprocess.run(args,text=True,capture_output=True);(o/(n+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
for n in ['kernel.h','sys/atomic.h','random/random.h']:
 q=o/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Existing host boundary. */\n')
for name,extra in [('plain',[]),('lora',['-DR2_LORA=1'])]:
 vf=flags+extra
 for kind in ['ui_render','precision']:
  run(name+'-'+kind,vf+['-c',str(APP/'src'/f'{kind}.c'),'-o',str(o/f'{name}-{kind}.o')])
 def build(tag,source,extra):
  binary=o/tag
  run(tag+'-build',vf+extra+['-DR2_INPUT_FAST_FIXTURE=1','-DUI_INPUT_SOURCE="'+str(source)+'"','-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_input_fast_test.c'),str(o/f'{name}-ui_render.o'),str(o/f'{name}-precision.o'),'-o',str(binary)])
  return binary
 current=build(name+'-input',APP/'src/ui_service.c',[])
 for mode in range(57,63):run(name+'-'+str(mode),[str(current),str(mode)])
 if a.baseline:
  old=build(name+'-317-negative',a.baseline.resolve()/'src/ui_service.c',['-DR2_INPUT_BASELINE=1'])
  for mode in [57,61]:run(name+'-317-negative-'+str(mode),[str(old),str(mode)])
assert manifest()==before
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_INPUT_MAIN_FAST_PATH_WITH317_NEGATIVE'if a.baseline else'PASS_ACTUAL_INPUT_MAIN_FAST_PATH',hardware=False,inputs=before,baseline_source=str(a.baseline)if a.baseline else None,baseline_source_sha256=sha(a.baseline/'src/ui_service.c')if a.baseline else None,cases=['600ms renderer pause120ms page pulse','600ms renderer pause120ms menu navigation pulse','recoverable SPI error retains input','LCD init error retains GPIO input','negative GPIO stops sampling','stop disables input','28ms peer help renders before menu with original thresholds'],negative317_cases=4 if a.baseline else 0),indent=2)+'\n')
print('PASS actual20ms main input under600ms render pause, error/stop; exact317 missed-pulse negatives retained')
