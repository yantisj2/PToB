#!/usr/bin/env python3
"""Actual N30 input and off/wake API paths, offline only.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
A=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
flags=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(A/'tests/power_fakes'),'-I'+str(A/'main'),'-I'+str(A.parent/'w12/main')]
for label,sources in [('input',['tests/power_input_test.cpp','main/ui_model.cpp']),('power',['tests/power_control_test.cpp','main/n30_power.cpp'])]:
 cmd=[*flags,*[str(A/x) for x in sources],'-o',str(out/label)];subprocess.run(cmd,check=True);subprocess.run([out/label],check=True)
s=(A/'main/display.cpp').read_text();start=s.index('  if(stopping){');end=s.index('  const auto now=now_us();',start)
(out/'actual_display_power_worker.inc').write_text(s[start:end])
start=s.index('bool n30_display_take_poweroff(){');(out/'actual_display_power_api.inc').write_text(s[start:])
cmd=[*flags,'-I'+str(out),str(A/'tests/power_display_test.cpp'),'-o',str(out/'display-stop')];subprocess.run(cmd,check=True);subprocess.run([out/'display-stop'],check=True)
paths=['main/ui.hpp','main/ui_model.cpp','main/n30_power.hpp','main/n30_power.cpp','main/native_main.cpp','main/display.cpp','main/n30_display.hpp','tests/power_input_test.cpp','tests/power_control_test.cpp','tests/power_display_test.cpp','tools/power_tests.py']
(out/'receipt.json').write_text(json.dumps({'status':'PASS_N30_POWER_INPUT_AND_API_ASAN_UBSAN','hardware_access':False,'sources':{x:hashlib.sha256((A/x).read_bytes()).hexdigest() for x in paths}},indent=2)+'\n')
