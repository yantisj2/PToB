#!/usr/bin/env python3
"""Focused watch loss/rediscovery regressions against the actual shared owner. No hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
W12=APP.parent/'w12'
N30=APP.parent/'n30'
sys.path.insert(0,str(W12/'tools'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--idf',type=Path,default=os.environ.get('IDF_PATH'));a=p.parse_args()
 if not a.idf:p.error('--idf or IDF_PATH is required for public TinyCrypt headers')
 os.environ['IDF_PATH']=str(a.idf.resolve())
 from rc7_permission_e2e import native_fixture,sha
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else 'clang');cxx=os.environ.get('CXX',cc+'++')
 crypto=a.idf/'components/bt/host/nimble/nimble/ext/tinycrypt'
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all']
 includes=[out,APP/'main',N30/'main',W12/'main/gm7',W12/'main',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include',crypto/'include']
 flags+=['-I'+str(x) for x in includes]
 flags+=['-DR2_TZ_WIRE=1']
 records=[]
 def run(cmd,name):
  with (out/(name+'.log')).open('w') as f:subprocess.run(list(map(str,cmd)),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
  records.append(name)
 src=W12/'main/r2/r2-g1'
 files=[src/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[W12/'main/r2/rc7/protocol/rc_dynamic.c',W12/'main/r2/rc7/src/rc_boundary.c',W12/'main/r2/rc7/src/rc_slots.c',W12/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c',W12/'main/gm7/gm7_codec.c']
 objects=[]
 for file in files:
  obj=out/(file.stem+'.o');run([cc,'-std=c11',*flags,'-c',file,'-o',obj],file.stem+'-compile');objects.append(obj)
 run([cxx,'-std=c++20',*flags,APP/'tests/ble_roaming_test.cpp','-o',out/'policy'],'policy-build');run([out/'policy'],'policy')
 prefix=(out/'native.cpp').read_text().split('#define V12_RC7_PERMISSION_NATIVE 1')[0]
 # This fixture owns the receiver, while ble_power_transport_tests exercises the
 # real transport admission gate separately against SDK fakes.
 prefix+='\nbool watch_fixture_can_admit=true,watch_fixture_cancel_at_start=false; namespace v12 { bool transport_watch_ble_admit_allowed(bool){return watch_fixture_can_admit;} bool transport_watch_ble_admit_begin(bool){if(watch_fixture_cancel_at_start){watch_fixture_cancel_at_start=false;watch_fixture_can_admit=false;}return watch_fixture_can_admit;} void transport_watch_ble_admit_end(){} }\n'
 support=(W12/'tests/precision_priority_fixture.inc').read_text().split('int main(')[0]
 (out/'successor.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+'\n#include "ble_roaming_native.inc"\n')
 native_flags=['-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DPTOB_GM_SUCCESSOR=1']
 native_flags+=['-DPTOB_N30=1','-DV12_G1_FAILED_PARENT_RETRY_US=60000000']
 native_flags+=['-I'+str(x) for x in (APP/'tests',W12/'tests/r2_fakes',W12/'tests/console_fakes',W12/'legacy',W12/'legacy/reference')]
 run([cxx,'-std=c++20',*flags,*native_flags,out/'successor.cpp',*objects,'-o',out/'native'],'native-build')
 for case in ('loss','busy','off','fault','drain-failure'):
  run([out/'native',case],'native-'+case)
 (out/'recovery.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+'\n#include "ble_fault_recovery_native.inc"\n')
 run([cxx,'-std=c++20',*flags,*native_flags,'-DPTOB_TWATCH=1',out/'recovery.cpp',*objects,'-o',out/'recovery'],'recovery-build')
 for case in ('operating','pin','rebind','rebind-late','drain-timeout','off','explicit-on','aux','residual-scan','new-fault','cancelled','epoch','epoch-failed','sid','interval','address','adopt','unknown','host-reset','init-off-on','sync-off-race','host-cycle'):
  run([out/'recovery',case],'recovery-'+case)
 run([cxx,'-std=c++20',*flags,'-DPTOB_N30=1','-DPTOB_TWATCH=1',APP/'tests/roaming_clock_test.cpp',*objects,'-o',out/'clock'],'clock-build');run([out/'clock'],'clock')
 (out/'clock-owner.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+'\n#include "roaming_clock_native.inc"\n')
 run([cxx,'-std=c++20',*flags,*native_flags,'-DPTOB_TWATCH=1',out/'clock-owner.cpp',*objects,'-o',out/'clock-owner'],'clock-owner-build')
 for case in ('hold','off','rebind','chain'):run([out/'clock-owner',case],'clock-owner-'+case)
 # Exercise the real receiver's Sync error forwarding, independently of the
 # transport suite's SDK fakes, including retry of a failed address setup.
 address_stub='int ble_hs_id_set_rnd(const uint8_t*){return 0;}'
 assert prefix.count(address_stub)==1
 sync_prefix=prefix.replace(address_stub,'int watch_fixture_address_rc=0; int ble_hs_id_set_rnd(const uint8_t*){return watch_fixture_address_rc;}')
 sync_case='''
int main(int argc,char** argv){assert(argc==2);const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init());uint8_t site=0;
 if(std::string(argv[1])=="address"){
  watch_fixture_address_rc=77;assert(!v12::r2_host_sync()&&!v12::fault);
  assert(v12::r2_watch_host_sync_error(&site)==77&&site==2&&!v12::boot_client_attempted);
  watch_fixture_address_rc=0;assert(v12::r2_host_sync());
  assert(!v12::r2_watch_host_sync_error(&site)&&!site&&v12::client.generation==1);
 }else{v12::fault=true;assert(!v12::r2_host_sync());assert(v12::r2_watch_host_sync_error(&site)==-16&&site==3);}
}
'''
 (out/'sync-error.cpp').write_text(sync_prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+sync_case)
 run([cxx,'-std=c++20',*flags,*native_flags,'-DPTOB_TWATCH=1',out/'sync-error.cpp',*objects,'-o',out/'sync-error'],'sync-error-build')
 for case in ('address','admission'):run([out/'sync-error',case],'sync-error-'+case)
 sources=[APP/'main/ble_acquisition_recovery.hpp',APP/'main/roaming_clock.hpp',APP/'tests/ble_roaming_test.cpp',APP/'tests/ble_roaming_native.inc',APP/'tests/ble_fault_recovery_native.inc',APP/'tests/roaming_clock_test.cpp',APP/'tests/roaming_clock_native.inc',Path(__file__),W12/'main/g1_corrected_clock.hpp',W12/'main/native_g1.cpp',W12/'main/native_r2.hpp']
 (out/'receipt.json').write_text(json.dumps({'status':'PASS','checks':records,'sources':{str(p):sha(p) for p in sources},'hardware_access':False,'scope':'SYNTHETIC_CONTROLLER_ACTUAL_RECEIVER_OWNER'},indent=2)+'\n')
 print('PASS watch loss watchdog and actual bounded rebind/fresh admission owner; no hardware')
if __name__=='__main__':main()
