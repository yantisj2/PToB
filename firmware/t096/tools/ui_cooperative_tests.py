#!/usr/bin/env python3
"""Initial cooperative raster mechanism proof, no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--baseline-app',type=Path,required=True);p.add_argument('--event-delay-ns',type=int,default=0);a=p.parse_args();assert 0<=a.event_delay_ns<=0xffffffff
o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
files=[APP/x for x in ['src/ui_render.c','legacy_ui/draw.inc','include/ui.h','tests/ui_cooperative_test.c','tools/ui_cooperative_tests.py']]
files+=[APP/x for x in ['src/time_zone_database.c','include/time_zone_config.h',
 'include/time_zone_selector.h','protocol/time_zone.h','data/time_zone_data.h','legacy_ui/legacy.h']]
before={str(p.relative_to(APP)):sha(p)for p in files}
old=(a.baseline_app/'src/ui_render.c').read_text()+'\n'+(a.baseline_app/'legacy_ui/draw.inc').read_text()+'\n'+(a.baseline_app/'src/time_zone_database.c').read_text()
names=sorted(set(re.findall(r'(?m)^(?!static\b)[^#\n;{}]*\b((?:r2_|ui_)\w+)\([^;{}]*\)\s*\{',old)))
assert 'r2_ui_render'in names
(o/'rename.h').write_text(''.join('#define '+n+' baseline_'+n+'\n'for n in names))
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
flags=[cc,"-DR2_UI_PARTIAL_JOBS=1",'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all']
def run(name,args):
 r=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=60);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
production=['-DR2_GM7_NATIVE=1','-DR2_NATIVE_BLE=1','-DR2_RC_PROFILE=1','-DR2_SWITCHABLE_FIELD=1',
 '-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-DR2_LORA_FIXED500_EXPERIMENT=1',
 '-DR2_LORA_FIXED500_EVENT_DELAY_NS='+str(a.event_delay_ns),'-DR2_TZ_WIRE=1','-DR2_TZ_AUTO=1',
 '-DR2_TZ_MODE=2','-DR2_TZ_MANUAL_ID=2','-DR2_TZ_MANUAL_MIN=-300']
for variant,extra in [('plain',[]),('lora',['-DR2_LORA=1']),('production',production)]:
 objects=[]
 for name,app,source,include in [('baseline',a.baseline_app,'src/ui_render.c',['-include',o/'rename.h']),('current',APP,'src/ui_render.c',[]),('precision',APP,'src/precision.c',[])]:
  obj=o/(variant+'-'+name+'.o');objects.append(obj)
  run(variant+'-'+name+'-build',flags+extra+['-I'+str(app/'include'),'-I'+str(app/'protocol'),*include,'-c',app/source,'-o',obj])
 if variant=='production':
  obj=o/'production-tz.o';objects.append(obj)
  run('production-tz-build',flags+extra+['-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-c',APP/'src/time_zone_database.c','-o',obj])
  obj=o/'production-baseline-tz.o';objects.append(obj)
  run('production-baseline-tz-build',flags+extra+['-I'+str(a.baseline_app/'include'),'-I'+str(a.baseline_app/'protocol'),'-include',o/'rename.h','-c',a.baseline_app/'src/time_zone_database.c','-o',obj])
 binary=o/variant;run(variant+'-build',flags+extra+['-I'+str(APP/'include'),'-I'+str(APP/'protocol'),APP/'tests/ui_cooperative_test.c',*objects,'-o',binary]);run(variant,[binary])
assert before=={str(p.relative_to(APP)):sha(p)for p in files}
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_COOPERATIVE_AND_CRITICAL_POLICY_PARITY',hardware=False,inputs=before,external_primary_sources={str(a.baseline_app/'src/ui_render.c'):sha(a.baseline_app/'src/ui_render.c'),str(a.baseline_app/'legacy_ui/draw.inc'):sha(a.baseline_app/'legacy_ui/draw.inc')},scenes=600,abort_cases=9600,critical_histories=1800,production_flags=production,maximum_pixel_attempts_between_callbacks=80,callback_context='ordinary sole UI thread; no IRQ/SPI/mutex ownership',new_pixel_or_model_buffers=0),indent=2)+'\n')
