#!/usr/bin/env python3
"""Actual N30/shared RC7 capability owner with synthetic controller results."""
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import argparse,json,os,subprocess
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
src=(APP.parent/'w12/main/rc7_relay_native.inc').read_text()
(a.out/'actual_adv_cap_update.inc').write_text(src[src.rfind('#if',0,src.index('bool relay_cap_blocked(')):src.index('bool relay_configure(')])
for name,profile in (('shared',[]),('n30',['-DPTOB_N30=1']),('w12',['-DPTOB_W12_BLE_RECOVERY=1'])):
 exe=a.out/name
 cmd=[os.environ.get('CXX','/opt/homebrew/opt/llvm/bin/clang++' if Path('/opt/homebrew/opt/llvm/bin/clang++').exists() else 'clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1',*profile,'-I'+str(a.out.resolve()),'-I'+str(APP/'main'),'-I'+str(APP.parent/'w12/main'),str(APP/'tests/adv_data_retry_test.cpp'),'-o',str(exe)]
 subprocess.run(cmd,check=True,timeout=30)
 for case in ('recover','persist','other',*(['recreate','recreate-late','remove-fail','configure-fail','pause-owner-race','data-owner-race','remove-owner-race','deadline','deadline-success'] if name=='w12' else [])):subprocess.run([exe,case],check=True,timeout=10)
(a.out/'receipt.json').write_text(json.dumps({'status':'PASS','hardware_access':False,'cases':['actual cap171 owner; no publication on failure','100ms/two deferred capacity attempts; W12 stopped CAP recreate once, other targets third fatal','other error fatal','successful retry publishes once','non-N30 unchanged fatal first error']},indent=2)+'\n')
