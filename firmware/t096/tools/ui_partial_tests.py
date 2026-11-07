#!/usr/bin/env python3
"""Production partial-display driver/helper checks. No hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
p.add_argument('--baseline-app',type=Path,required=True);args=p.parse_args()
O=args.out.resolve();O.mkdir(parents=True,exist_ok=False);B=args.baseline_app.resolve()
files=['src/display.c','src/ui_render.c','legacy_ui/draw.inc','include/ui.h',
 'include/ui_framebuffer.h','tests/display_fake.h','tests/display_region_test.c',
 'tests/ui_helper_ownership_test.c','tools/ui_partial_tests.py']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={n:sha(A/n)for n in files};steps=[]
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
 '-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-DR2_UI_PARTIAL_JOBS=1','-I'+str(A/'include'),'-I'+str(A/'protocol')]
def run(name,command):
 result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=60)
 (O/(name+'.log')).write_text(result.stdout+result.stderr)
 steps.append({'name':name,'command':list(map(str,command)),'exit':result.returncode})
 result.check_returncode();print(result.stdout.strip(),flush=True)
for n in ['kernel.h','drivers/gpio.h','drivers/spi.h','drivers/sensor.h',
 'drivers/pwm.h','drivers/clock_control/nrf_clock_control.h']:
 q=O/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Fake public driver boundary. */\n')
run('region-build',flags+['-DR2_DISPLAY_REGION_FIXTURE=1','-I'+str(O),
 '-include',A/'tests/display_fake.h',A/'tests/display_region_test.c','-o',O/'region'])
run('region',[O/'region'])
old=(B/'src/ui_render.c').read_text()+'\n'+(B/'legacy_ui/draw.inc').read_text()
names=sorted(set(re.findall(r'(?m)^(?!static\b)[^#\n;{}]*\b((?:r2_|ui_)\w+)\([^;{}]*\)\s*\{',old)))
(O/'rename.h').write_text(''.join('#define '+n+' baseline_'+n+'\n'for n in names))
extra=['-DR2_LORA=1']
run('baseline-build',flags+extra+['-I'+str(B/'include'),'-I'+str(B/'protocol'),
 '-include',O/'rename.h','-c',B/'src/ui_render.c','-o',O/'baseline.o'])
run('precision-build',flags+extra+['-c',A/'src/precision.c','-o',O/'precision.o'])
run('helpers-build',flags+extra+['-DUI_RENDER_SOURCE="'+str(A/'src/ui_render.c')+'"',
 A/'tests/ui_helper_ownership_test.c',O/'baseline.o',O/'precision.o','-o',O/'helpers'])
run('helpers',[O/'helpers'])
assert inputs=={n:sha(A/n)for n in files}
(O/'receipt.json').write_text(json.dumps({'status':'PASS_PRODUCTION_REGION_AND_HELPER_BOUNDARIES',
 'hardware':False,'production_definition':'R2_UI_PARTIAL_JOBS=1','inputs':inputs,
 'baseline':{str(B/'src/ui_render.c'):sha(B/'src/ui_render.c'),
 str(B/'legacy_ui/draw.inc'):sha(B/'legacy_ui/draw.inc')},'steps':steps},indent=2)+'\n')
