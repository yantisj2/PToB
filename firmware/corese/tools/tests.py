#!/usr/bin/env python3
"""Host verification only; no USB or hardware access.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
p.add_argument('--address-sanitizer',action='store_true',help='Also enable ASan on hosts with a working ASan runtime')
p.add_argument('--dependencies',type=Path,help='Pinned M5 dependency directory for actual power-button logic test')
p.add_argument('--sdk',type=Path,help='Pinned public ESP-IDF checkout for actual GS/HMAC codec tests')
a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
compat=a.out/'compat';compat.mkdir();(compat/'time_zone.h').write_text('#include "'+str(APP.parent/'w12/main/r2/r2-g1/time_zone.h')+'"\n')
sanitizers='address,undefined' if a.address_sanitizer else 'undefined'
flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize='+sanitizers,'-fno-omit-frame-pointer','-I'+str(APP/'main'),'-I'+str(compat),'-DR2_TZ_WIRE=1']
cmd=[os.environ.get('CXX','clang++'),'-std=c++20',*flags,str(APP/'tests/ui_test.cpp'),str(APP/'main/ui.cpp'),str(APP/'main/mesh_diagnostics.cpp'),'-o',str(a.out/'ui_test')]
subprocess.run(cmd,check=True,timeout=60);subprocess.run([str(a.out/'ui_test')],check=True,timeout=30)
print('PASS: expiry, UTC-phase pulse boundaries, RC9/HOLD precision exclusion, nonrenewing500ms retained display, offset scope, overflow, render-state combinations, multi-page touch navigation / cancellation; '+sanitizers)
from live_build import freeze_shared
from prepare_runtime import prepare
frozen=a.out/'source';freeze_shared(frozen);prepare(a.out/'runtime',frozen)
for test in ('shadow_holdover',):
 cmd=[os.environ.get('CXX','clang++'),'-std=c++20',*flags,'-I'+str(frozen/'firmware/w12/main'),str(APP/('tests/'+test+'_test.cpp')),'-o',str(a.out/test)]
 subprocess.run(cmd,check=True,timeout=60);subprocess.run([str(a.out/test)],check=True,timeout=30)
if a.sdk:
 crypto=a.sdk.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt';protocol=APP.parent/'t096/protocol'
 sources=[protocol/'group_auth.c',protocol/'role_announcement.c',protocol/'gnss_diagnostics.c',APP/'protocol/gm7_codec.c',APP.parent/'w12/main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
 for source in sources:
  obj=a.out/(source.stem+'.o');objects.append(obj)
  subprocess.run([os.environ.get('CC','clang'),'-std=c11',*flags,'-I'+str(crypto/'include'),'-I'+str(protocol),'-c',str(source),'-o',str(obj)],check=True,timeout=60)
 for test in ('mesh_wire_test','reference_wire_test','gm7_wire_test','mesh_owner_test'):
  extra=[str(APP/'main/mesh_owner.cpp')] if test=='mesh_owner_test' else []
  includes=['-I'+str(APP/'tests/stubs'),'-I'+str(APP.parent/'w12/main'),'-I'+str(protocol),'-I'+str(APP.parent/'w12/main/r2/r2-g1')]
  subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20',*flags,*includes,str(APP/('tests/'+test+'.cpp')),str(APP/'main/mesh_diagnostics.cpp'),str(APP/'main/mesh_wire.cpp'),str(APP/'main/gm7_wire.cpp'),*extra,*map(str,objects),'-o',str(a.out/test)],check=True,timeout=60)
  subprocess.run([str(a.out/test)],check=True,timeout=30)
else:print('GS/HMAC codec tests not run; supply --sdk with pinned public ESP-IDF.')

if a.dependencies:
 m5=a.dependencies.resolve()/'M5Unified/src'
 subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20',*flags,'-I'+str(m5),str(APP/'tests/button_test.cpp'),str(m5/'utility/Button_Class.cpp'),'-o',str(a.out/'button_test')],check=True,timeout=60)
 subprocess.run([str(a.out/'button_test')],check=True,timeout=30)
