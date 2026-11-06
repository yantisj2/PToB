#!/usr/bin/env python3
"""Offline W12 BLE acquisition/recovery production-owner regressions. No hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
W12=APP
sys.path.insert(0,str(W12/'tools'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--tz',action='store_true');p.add_argument('--idf',type=Path,default=os.environ.get('IDF_PATH'));a=p.parse_args()
 if not a.idf:p.error('--idf or IDF_PATH is required for public TinyCrypt headers')
 os.environ['IDF_PATH']=str(a.idf.resolve())
 from rc7_permission_e2e import native_fixture,sha
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else 'clang');cxx=os.environ.get('CXX',cc+'++')
 crypto=a.idf/'components/bt/host/nimble/nimble/ext/tinycrypt'
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 includes=[out,APP/'main',W12/'main/gm7',W12/'main',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include',crypto/'include']
 flags+=['-I'+str(x) for x in includes]
 if a.tz:flags+=['-DR2_TZ_WIRE=1']
 records=[]
 def run(cmd,name):
  with (out/(name+'.log')).open('w') as f:subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
  records.append(name)
 run([sys.executable,'-B',APP/'tests/receiver_gate_import_test.py'],'receiver-gate-import')
 run([sys.executable,'-B',APP/'tests/console_version_test.py'],'normal-console-version')
 run([sys.executable,'-B',APP/'tools/sync_watchdog_tests.py','--out',out/'watchdog'],'watchdog')
 run([sys.executable,'-B',APP.parent/'n30/tools/adv_data_retry_tests.py','--out',out/'paused-capability'],'paused-capability')
 src=W12/'main/r2/r2-g1'
 files=[src/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[W12/'main/r2/rc7/protocol/rc_dynamic.c',W12/'main/r2/rc7/src/rc_boundary.c',W12/'main/r2/rc7/src/rc_slots.c',W12/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c',W12/'main/gm7/gm7_codec.c']
 objects=[]
 for file in files:
  obj=out/(file.stem+'.o');run([cc,'-std=c11',*flags,'-c',file,'-o',obj],file.stem+'-compile');objects.append(obj)
 for profile,definitions in (('n30',['-DPTOB_N30=1']),('w12',['-DPTOB_W12_BLE_RECOVERY=1','-DPTOB_W12_FINE_CLOCK=1'])):
  binary=out/('source-renewal-'+profile)
  run([cxx,'-std=c++20',*flags,*definitions,APP/'tests/g1_source_renewal_test.cpp',*objects,'-o',binary],'source-renewal-'+profile+'-build')
  run([binary],'source-renewal-'+profile)
 run([cxx,'-std=c++20',*flags,APP/'tests/coarse_join_test.cpp',*objects,'-o',out/'coarse-join'],'coarse-join-build');run([out/'coarse-join'],'coarse-join')
 run([cxx,'-std=c++20',*flags,APP/'tests/ble_acquisition_recovery_test.cpp','-o',out/'policy'],'policy-build');run([out/'policy'],'policy')
 run([cxx,'-std=c++20',*flags,'-Wno-sign-compare','-DPTOB_W12_BLE_RECOVERY=1','-I'+str(W12/'legacy'),'-I'+str(W12/'legacy/reference'),W12/'tests/g1_diagnostic_test.cpp','-o',out/'diagnostic-width'],'diagnostic-width-build');run([out/'diagnostic-width'],'diagnostic-width')
 prefix=(out/'native.cpp').read_text().split('#define V12_RC7_PERMISSION_NATIVE 1')[0]
 support=(W12/'tests/precision_priority_fixture.inc').read_text().split('int main(')[0]
 (out/'successor.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+'\n#include "ble_recovery_native.inc"\n')
 native_flags=['-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DPTOB_GM_SUCCESSOR=1']
 native_flags+=['-DPTOB_W12_BLE_RECOVERY=1']
 native_flags+=['-I'+str(x) for x in (APP/'tests',W12/'tests/r2_fakes',W12/'tests/console_fakes',W12/'legacy/reference')]
 run([cxx,'-std=c++20',*flags,*native_flags,out/'successor.cpp',*objects,'-o',out/'native'],'native-build')
 for case in ('permission-gap-parent','startup-unusable','startup-slow','startup-slow-expired','startup-slow-short','startup-slow-mac','pa-authenticated','pa-bad-mac','pa-old-capture'):run([out/'native',case],'native-'+case)
 for case in ('startup','stall','progress','busy','off','fault','absence','pin','relay-only','drain-failure'):run([out/'native','ble-acquisition-'+case],'native-ble-acquisition-'+case)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','checks':records,'native_source_sha256':sha(W12/'main/native_g1.cpp'),'policy_sha256':sha(W12/'main/g1_gm_successor.hpp'),'wire_revision':3,'target':'W12','hardware_access':False,'precision':'UNQUALIFIED'},indent=2)+'\n')
 print('PASS W12 delayed admission, actual-owner recovery/drain/PA authentication, policy and diagnostic width; no hardware access')
if __name__=='__main__':main()
