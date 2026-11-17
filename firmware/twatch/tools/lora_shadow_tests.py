#!/usr/bin/env python3
"""Offline receive scheduling checks; never operates a radio.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
sources=[APP/'main/lora_shadow.hpp',APP/'main/lora_shadow_input.h',APP/'main/lora_shadow_diagnostics.cpp',APP/'tests/lora_shadow_test.cpp',Path(__file__)]
exe=a.out/'shadow';cxx='/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'
subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),str(sources[3]),'-o',str(exe)],check=True)
r=subprocess.run([str(exe.resolve())],check=True,capture_output=True,text=True);(a.out/'test.log').write_text(r.stdout+r.stderr)
# Verify firmware row admission and the actual diagnostics at maximum widths.
for name,extra in [('console_shape',[]),('lora_sleep_diagnostics',[APP/'main/lora_shadow_diagnostics.cpp'])]:
 source=APP/'tests'/f'{name}_test.cpp';sources.append(source);target=a.out/name
 subprocess.run([cxx,'-std=c++17','-DV12_R2_ENABLED=1','-Wall','-Wextra','-Werror','-I'+str(APP/'main'),'-I'+str(APP.parent/'w12/main'),str(source),*map(str,extra),'-o',str(target)],check=True)
 subprocess.run([str(target.resolve())],check=True)
# The current and historical host parsers select their exact receipt-bound rows.
import importlib.util,copy
path=APP/'tools/framed_console.py';spec=importlib.util.spec_from_file_location('watch_console',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);sources.append(path);sources.append(APP/'main/console_shape.hpp')
for contract,count in [('SHADOW_ONLY_V1',11),('GUARDED_SLEEP_MISS_RELEARN_V2',12),('EARLY_SLEEP_COMPLETE_DATA_V3',12)]:
 parser=module.Parser('0000000000000001',dict(r2_profile='r2-g1',watch_frame='LORA',lora_shadow_schedule=contract))
 assert len(parser.expected_rows)==count
rows={'TWATCH_LORA_SHADOW':dict(version='2',shadow_only='0'),'TWATCH_LORA_SLEEP':dict(version='1',enabled='1',sleeping='1',active='1',sleeps='1',wakes='0',refused='0',sleep_us='100',rx_us='100',transition_us='200',wake_lead_us='0',poll_ms='50')}
module.validate_lora_sleep(rows)
for key in rows['TWATCH_LORA_SLEEP']:
 bad=copy.deepcopy(rows);del bad['TWATCH_LORA_SLEEP'][key]
 try:module.validate_lora_sleep(bad)
 except (RuntimeError,ValueError,KeyError):pass
 else:raise AssertionError('Missing sleep field accepted: '+key)
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'SYNTHETIC_AUTHENTICATED_WINDOW_POLICY_NO_HARDWARE','sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}},indent=2)+'\n')
print(r.stdout.strip())
