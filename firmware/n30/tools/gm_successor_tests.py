#!/usr/bin/env python3
"""Offline GM7 successor and production N30 owner regressions. No hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
W12=APP.parent/'w12'
sys.path.insert(0,str(W12/'tools'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--w12',action='store_true');p.add_argument('--tz',action='store_true');p.add_argument('--idf',type=Path,default=os.environ.get('IDF_PATH'));a=p.parse_args()
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
 src=W12/'main/r2/r2-g1'
 files=[src/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[W12/'main/r2/rc7/protocol/rc_dynamic.c',W12/'main/r2/rc7/src/rc_boundary.c',W12/'main/r2/rc7/src/rc_slots.c',W12/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c',W12/'main/gm7/gm7_codec.c']
 objects=[]
 for file in files:
  obj=out/(file.stem+'.o');run([cc,'-std=c11',*flags,'-c',file,'-o',obj],file.stem+'-compile');objects.append(obj)
 run([cxx,'-std=c++20',*flags,APP/'tests/gm_successor_test.cpp',*objects,'-o',out/'policy'],'policy-build');run([out/'policy'],'policy')
 if not a.w12:
  run([cxx,'-std=c++20',*flags,'-DPTOB_N30=1',APP/'tests/clock_handoff_test.cpp',*objects,'-o',out/'clock-handoff'],'clock-handoff-build');run([out/'clock-handoff'],'clock-handoff')
 prefix=(out/'native.cpp').read_text().split('#define V12_RC7_PERMISSION_NATIVE 1')[0]
 support=(W12/'tests/precision_priority_fixture.inc').read_text().split('int main(')[0]
 (out/'successor.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+'\n#include "gm_successor_native.inc"\n')
 native_flags=['-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DPTOB_GM_SUCCESSOR=1']
 if not a.w12:native_flags+=['-DPTOB_N30=1','-DV12_G1_FAILED_PARENT_RETRY_US=60000000']
 native_flags+=['-I'+str(x) for x in (APP/'tests',W12/'tests/r2_fakes',W12/'tests/console_fakes',W12/'legacy/reference')]
 run([cxx,'-std=c++20',*flags,*native_flags,out/'successor.cpp',*objects,'-o',out/'native'],'native-build')
 for case in ('withdraw','yield','switchover','loss','fallback','pin','relay-only','bad-mac','bad-address','scan'):run([out/'native',case],'native-'+case)
 if a.tz:run([out/'native','tz'],'native-tz')
 if not a.w12:
  for case in ('handoff-join','handoff-clock-step'):run([out/'native',case],'native-'+case)
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','checks':records,'native_source_sha256':sha(W12/'main/native_g1.cpp'),'policy_sha256':sha(W12/'main/g1_gm_successor.hpp'),'wire_revision':3,'target':'W12' if a.w12 else 'N30','hardware_access':False,'precision':'UNQUALIFIED'},indent=2)+'\n')
 print('PASS N30 GM7 successor policy and production owner (10 schedules); no hardware access')
if __name__=='__main__':main()
