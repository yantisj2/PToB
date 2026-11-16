#!/usr/bin/env python3
"""Offline shadow scheduling checks; never operates a radio.
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
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'SYNTHETIC_SCHEDULE_SHADOW_NO_RADIO_SLEEP_NO_HARDWARE','sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}},indent=2)+'\n')
print(r.stdout.strip())
