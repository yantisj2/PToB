#!/usr/bin/env python3
"""Actual GNSS controller with deterministic SDK/PMU/PPS boundaries. No hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
cmd=[cxx,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DPTOB_TWATCH=1','-DPTOB_N30=1','-DV12_R2_ENABLED=1']
for d in ['twatch/tests/shadow_fakes','twatch/main','n30/main','w12/main','w12/legacy','w12/main/r2/r2-g1','w12/main/r2/rc7/include']:cmd+=['-I'+str(APP.parent/d)]
cmd+=[str(APP/'tests/shadow_runtime_test.cpp'),'-o',str(out/'controller')]
with (out/'compile.log').open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
with (out/'controller.log').open('w') as log:subprocess.run([str(out/'controller')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=20)
# Run the real strict field lexer over actual C++ formatter output, so duplicated
# or malformed keys cannot hide behind a successful C++ bounds assertion.
import sys
sys.path.insert(0,str(APP/'tools'));import framed_console as console
for line in (out/'controller.log').read_text().splitlines():
 if line.startswith(('TWATCH_UI ','TWATCH_SHADOW ')):console.base.fields(line)
files=[Path(__file__),APP/'tests/shadow_runtime_test.cpp',APP/'main/shadow_gnss.cpp',APP/'main/shadow_gnss.hpp',APP/'main/shadow_model.hpp',APP/'main/shadow_nmea.hpp',REPO/'firmware/w12/main/g1_pps_diagnostic.hpp',REPO/'firmware/w12/main/g1_gps_shadow.hpp',REPO/'firmware/w12/main/g1_clock_prediction.hpp',*sorted((APP/'tests/shadow_fakes').rglob('*.hpp')),*sorted((APP/'tests/shadow_fakes').rglob('*.h'))]
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,scope='ACTUAL_CONTROLLER_PMULIFECYCLE_REAL_G1_WITNESS_FORMAT_ASAN_UBSAN',sources={str(q.relative_to(REPO)):hashlib.sha256(q.read_bytes()).hexdigest() for q in files}),indent=2)+'\n')
print((out/'controller.log').read_text().splitlines()[-1])
