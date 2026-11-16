#!/usr/bin/env python3
"""Offline physical retirement proof and first/latest fault capsule checks.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
sources=[Path(__file__),APP/'main/maintenance_proof.hpp',APP/'main/radio_power_fault.hpp',APP/'main/ble_power.hpp',APP/'main/radio_watchdog.hpp',APP.parent/'w12/main/update_gate.hpp',APP/'tools/framed_console.py',APP.parent/'w12/tools/framed_console.py']
for name in ('maintenance_proof','radio_power_fault'):
 source=APP/'tests'/f'{name}_test.cpp';sources.append(source)
 with (out/(name+'.log')).open('w') as log:
  subprocess.run([cxx,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(APP.parent/'w12/main'),'-I'+str(APP/'main'),str(source),'-o',str(out/name)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
  subprocess.run([str(out/name)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
source=APP/'tests/update_off_test.py';sources.append(source)
with (out/'update_off.log').open('w') as log:subprocess.run([sys.executable,str(source)],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,scope='OFFLINE_ORIGINAL_DEADLINE_PHYSICAL_RETIREMENT_AND_FAULT_CAPSULE',sources={str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}),indent=2)+'\n')
print('PASS original deadlines, physical proof, immutable first fault, latest error and V2 USB guard')
