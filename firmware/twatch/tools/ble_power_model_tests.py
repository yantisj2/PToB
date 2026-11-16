#!/usr/bin/env python3
"""Offline BLE power transition model checks, not hardware/controller proof.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
files=[APP/'main/ble_power.hpp',APP/'tests/ble_power_test.cpp',Path(__file__)]
cc='/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++';exe=a.out/'test'
subprocess.run([cc,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'main'),str(files[1]),'-o',str(exe)],check=True)
r=subprocess.run([str(exe.resolve())],capture_output=True,text=True);(a.out/'test.log').write_text(r.stdout+r.stderr);r.check_returncode()
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':'SYNTHETIC_BLE_POWER_LIFECYCLE_MODEL','sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}},indent=2)+'\n');print(r.stdout.strip())
