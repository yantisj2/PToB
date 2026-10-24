#!/usr/bin/env python3
"""Offline causal GNSS shadow-clock checks; never opens hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False);out=a.out.resolve()
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'legacy')]
for fine in (False,True):
 binary=out/('fine' if fine else 'legacy')
 for phase,cmd in [('build',[cxx,*flags,*(['-DPTOB_W12_FINE_CLOCK=1'] if fine else []),str(APP/'tests/gps_shadow_prediction_test.cpp'),'-o',str(binary)]),('run',[str(binary)])]:
  with (out/(binary.name+'-'+phase+'.log')).open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=40)
paths=[APP/'legacy/ptob_gps_shadow_clock.hpp',APP/'tests/gps_shadow_prediction_test.cpp',Path(__file__)]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS_SYNTHETIC_CAUSAL_SHADOW_CLOCK',hardware_access=False,sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}),indent=2)+'\n')
print('PASS causal fine/legacy shadow prediction, analytic rate drift, expiry, missing-PPS reset and acceleration refusal')
