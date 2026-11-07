#!/usr/bin/env python3
"""Actual label publication and optional metadata commit; no hardware.
Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT"""
import os
from pathlib import Path
llvm=Path('/opt/homebrew/opt/llvm/bin/clang')
cc=os.environ.get('CC') or (str(llvm) if llvm.is_file() else 'clang')
import argparse,hashlib,json,subprocess
A=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();o=a.out.resolve();o.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs={str(q.relative_to(A)):sha(q)for q in A.rglob('*')if q.is_file()and q.suffix in('.c','.h','.inc','.py')and'__pycache__'not in q.parts}
for n in ['kernel.h','sys/atomic.h','random/random.h']:
 q=o/'zephyr'/n;q.parent.mkdir(parents=True,exist_ok=True);q.write_text('/* Existing host boundary. */\n')
(o/'ptob_registry.h').write_text('#include <stdint.h>\nstatic const char *ptob_registry_name(uint64_t id){static const char *const names[]={"--","TC-01","TC-02","TC-03","TC-04","TC-05","TC-06"};return id<7?names[id]:0;}\n')
flags=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1','-DR2_GM7_NATIVE=1','-DR2_LORA=1','-DR2_LORA_PASSIVE_SLOW=1','-DR2_UI_SCHEDULE_FIXTURE=1','-DPTOB_NAME_REGISTRY=1','-I'+str(A/'include'),'-I'+str(A/'protocol'),'-I'+str(o)]
def run(n,c):
 r=subprocess.run(c,text=True,capture_output=True,timeout=30);(o/(n+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
for n in ['ui_render','precision']:run(n,flags+['-c',str(A/'src'/f'{n}.c'),'-o',str(o/(n+'.o'))])
run('compile',flags+['-include',str(A/'tests/ui_service_fake.h'),str(A/'tests/ui_label_cache_test.c'),str(o/'ui_render.o'),str(o/'precision.o'),'-o',str(o/'test')]);run('run',[str(o/'test')])
assert all(sha(A/n)==h for n,h in inputs.items());(o/'receipt.json').write_text(json.dumps(dict(status='PASS_ACTUAL_FACTORY_LABEL_CACHE',hardware=False,inputs=inputs),indent=2)+'\n');print('PASS actual durable descriptive cache and metadata race/source replacement')
