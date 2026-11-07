#!/usr/bin/env python3
"""Production LoRa acquisition/RF pixels and button path; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,shutil,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();o=a.out.resolve();o.mkdir(parents=True,exist_ok=False)
cc=os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang'))
def source_manifest():return {str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in APP.rglob('*')if f.is_file()and f.suffix in('.c','.h','.inc','.py')and '__pycache__'not in f.parts}
before=source_manifest()
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),'-I'+str(APP/'protocol'),'-I'+str(o)]
def run(name,args):
 r=subprocess.run(args,capture_output=True,text=True);(o/(name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.strip())
run('sync-build',[*flags,'-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1',str(APP/'tests/lora_sync_ui_test.c'),str(APP/'src/precision.c'),'-o',str(o/'sync')]);run('sync',[str(o/'sync'),str(o)])
run('watch-build',[*flags,'-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-DR2_TZ_WIRE=1',str(APP/'tests/lora_watch_ui_test.c'),str(APP/'src/precision.c'),'-o',str(o/'watch')]);run('watch',[str(o/'watch'),str(o)])
run('menu-build',[*flags,'-DR2_LORA=1',str(APP/'tests/ui_menu_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),str(APP/'src/core.c'),'-o',str(o/'menu')]);run('menu',[str(o/'menu'),str(o)])
for name in ['kernel.h','sys/atomic.h','random/random.h']:
 f=o/'zephyr'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('/* Existing ui_service_fake boundary. */\n')
run('render-object',[*flags,'-c',str(APP/'src/ui_render.c'),'-o',str(o/'render.o')]);run('precision-object',[*flags,'-c',str(APP/'src/precision.c'),'-o',str(o/'precision.o')])
run('worker-build',[*flags,'-include',str(APP/'tests/ui_service_fake.h'),str(APP/'tests/ui_service_test.c'),str(o/'render.o'),str(o/'precision.o'),'-o',str(o/'worker')])
for mode in (2,4,5,14,15,16,17,54,55,56):run('worker-'+str(mode),[str(o/'worker'),str(mode)])
# Registry names use complete synthetic identities; no private factory inputs.
(o/'ptob_registry.h').write_text('#include <stdint.h>\nstatic const char *ptob_registry_name(uint64_t id){return id==UINT64_C(0x1111111100004990)?"TC-03":id==UINT64_C(0x2222222200004990)?"TC-09":0;}\n')
main=(APP/'src/main.c').read_text();start=main.index('static void lora_ui_view(');end=main.index('\n}\n',start)+3;(o/'lora_ui_binding_under_test.h').write_text(main[start:end])
run('binding-build',[*flags,'-DPTOB_NAME_REGISTRY=1','-DR2_LORA=1',str(APP/'tests/lora_ui_binding_test.c'),str(APP/'src/ui_render.c'),str(APP/'src/precision.c'),'-o',str(o/'binding')]);run('binding',[str(o/'binding')])
files=[APP/'src/ui_render.c',APP/'src/ui_service.c',APP/'src/main.c',APP/'legacy_ui/draw.inc',APP/'legacy_ui/legacy.h',APP/'include/ui_menu.h',APP/'tests/lora_sync_ui_test.c',APP/'tests/lora_watch_ui_test.c',APP/'tests/lora_ui_binding_test.c',APP/'tests/ui_menu_test.c',APP/'tests/ui_service_fake.h',APP/'tests/ui_service_test.c',Path(__file__)]
assert before==source_manifest(),'T096 source changed during test; receipt refused'
(o/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_NATIVE_UI',hardware=False,renderer_cases='waitMETA/waitPAIR/training/gap/cache/saver/independentinputs/TX/decodedactivity/help/BLEsync-over-HOLD-EST/RFcomparison',worker_cases=10,source_hashes={str(f.relative_to(APP)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files}),indent=2)+'\n')
