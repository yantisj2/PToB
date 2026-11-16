#!/usr/bin/env python3
"""Offline mobile source policy regression; no hardware or precision qualification.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
cxx=os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++')
sources=[APP/'main/mobile_time.hpp',APP/'main/radio_candidates.hpp',APP/'main/watch.hpp',APP/'tests/mobile_time_test.cpp',APP.parent/'n30/main/ui_model.cpp',APP.parent/'n30/main/ui.hpp',APP.parent/'n30/main/status_adapter.hpp',APP.parent/'n30/main/dual_time.hpp',Path(__file__)]
cmd=[cxx,'-std=c++20','-DPTOB_N30=1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all']
cmd+=['-I'+str(APP.parent/q) for q in ['twatch/main','n30/main','w12/main','w12/legacy','w12/main/r2/r2-g1','w12/main/r2/rc7/include']]
cmd += [str(APP/'tests/mobile_time_test.cpp'),str(APP.parent/'n30/main/ui_model.cpp'),'-o',str(out/'mobile')]
with (out/'compile.log').open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
with (out/'runtime.log').open('w') as log:subprocess.run([str(out/'mobile')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware_access=False,scope='SYNTHETIC_ACTUAL_MOBILE_POLICY_ASAN_UBSAN',sources={str(q):hashlib.sha256(q.read_bytes()).hexdigest() for q in sources}),indent=2)+'\n')
print('PASS repaired-expiry return, isolated/bursty/long RF gaps, stale evidence, genuine failure fallback, retained clock and instrument motion')
