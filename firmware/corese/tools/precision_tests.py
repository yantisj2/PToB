#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Compile the exact receiver overlay and current TZ codecs against public fakes."""
import argparse,json,os,subprocess
from pathlib import Path
from live_build import APP,REPO,LEADER_REF,freeze_shared
from prepare_runtime import prepare
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);root=out/'source';freeze_shared(root);prepare(out/'runtime',root)
(out/'ptob_device.h').write_text('#define PTOB_FACTORY_MAC {2,0,0,0,0,5}\n#define PTOB_DEVICE_LABEL "C-1"\n#define PTOB_LEGACY_WIRE_ID 3\n')
(out/'r2_bench_enrollment.h').write_text('static const r2_group_enrollment provision={{{'+','.join(map(str,range(32)))+'},UINT64_C(0x123456789abcdef0),1},UINT64_C(0x020000000005),"C-1",40000};\n')
fixture=subprocess.check_output(['git','-C',str(REPO),'show',LEADER_REF+':firmware/w12/tests/native_g1_test.cpp'],text=True)
fixture=fixture.replace('#include "../main/native_g1.cpp"','#include "'+str(root/'firmware/w12/main/native_g1.cpp')+'"')
fixture=fixture.replace('v12::provision.group','v12::corese_group').replace('r2_group_put(b+11,1,4)','r2_group_put(b+11,v12::corese_group.epoch,4)')
fixture=fixture.replace('int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*){return BLE_HS_ENOTCONN;}','''bool corese_rc_write=false;
int ble_gattc_write_flat(uint16_t h,uint16_t,const void* bytes,uint16_t n,int(*done)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* arg){
 if(!corese_rc_write)return BLE_HS_ENOTCONN;
 assert(n==216);auto* p=static_cast<const uint8_t*>(bytes);output.emplace_back(p,p+n);
 ble_gatt_error ok{};done(h,&ok,nullptr,arg);return 0;
}''')
fixture=fixture.replace('v12::discoveries.push(d);step(21);assert(v12::peer.connecting);','''if(v12::corese_group.id!=v12::provision.group.id){
   for(unsigned i=0;i<4000&&!v12::peer.connecting;++i){d.at=fake_now;v12::discoveries.push(d);step();}
  }else{v12::discoveries.push(d);step(21);}
  if(!v12::peer.connecting)std::fprintf(stderr,"fresh JOIN did not start: fault=%u scan=%u count=%u attempts=%u reason=%u api=%u rc=%d group=%llu epoch=%u now=%llu\\n",v12::fault,v12::scan,v12::discovery_count,v12::join_attempts,unsigned(v12::parent_selection.selection_view(fake_now).reason),unsigned(v12::first_error_api),v12::first_error_rc,(unsigned long long)v12::corese_group.id,v12::corese_group.epoch,(unsigned long long)fake_now);
  assert(v12::peer.connecting);''')
fixture=fixture.replace('#include "'+str(root/'firmware/w12/main/native_g1.cpp')+'"','''#ifdef V12_R2_RX_SHADOW
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 8192
using StackType_t=uint8_t;
inline unsigned uxTaskGetStackHighWaterMark(void*){return 8192;}
#endif
#include "'''+str(root/'firmware/w12/main/native_g1.cpp')+'''"
#ifdef V12_R2_RX_SHADOW
extern "C" bool clock_reference_read(uint64_t* count){*count=fake_now*20;return true;}
namespace v12::periodic_rx {
bool start(){return true;}void poll(){}uint32_t scope(uint16_t,bool){return 1;}
bool adopt(uint16_t,uint32_t){return true;}bool stop(){return true;}
bool match(uint16_t,const uint8_t*,size_t,uint64_t,Stamp& stamp){stamp={};return false;}
Stats status(){return {};}RfReference rf_reference(){return {};}
}
#endif''')
fixture=fixture.replace('  if(which=="boot-empty"){',(APP/'tests/gm_preference_owner.inc').read_text()+'\n  if(which=="boot-empty"){')
fixture=fixture.replace('  if(which=="boot-empty"){',(APP/'tests/time_zone_owner.inc').read_text()+'\n  if(which=="boot-empty"){')
fixture=fixture.replace('  if(which=="boot-empty"){',(APP/'tests/mesh_discovery_owner.inc').read_text()+'\n  if(which=="boot-empty"){')
# Historical lifecycle schedules start in the same group, learned through a
# real locator instead of an enrollment pin. The mesh schedule tests cold AUTO.
fixture=fixture.replace('  assert(v12::r2_status().client.running);','''  assert(v12::r2_status().client.running);
  if(which!="corese-mesh"){
   v12::Discovery seed{};seed.props=1;seed.length=25;seed.at=fake_now;seed.address.type=1;
   seed.bytes[0]=24;seed.bytes[1]=255;auto* p=seed.bytes+2;p[0]='G';p[1]='1';p[2]=1;
   r2_group_put(p+3,v12::provision.group.id,8);r2_group_put(p+11,v12::provision.group.epoch,4);r2_group_put(p+15,111,8);
   corese::mesh_address(seed.address.val,v12::provision.group,111);v12::corese_select_mesh(seed);
   assert(v12::corese_group.id==v12::provision.group.id&&!v12::member&&!v12::anchor.source);
   v12::admission_policy={}; // Isolate each historical lifecycle schedule's own recovery budget.
  }''')
(out/'native.cpp').write_text(fixture)
crypto=a.sdk.resolve()/'components/bt/host/nimble/nimble/ext/tinycrypt';w=root/'firmware/w12/main'
flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_TZ_WIRE=1']
for path in (out,APP/'tests/stubs',APP/'main',APP/'protocol',w,w/'r2/r2-g1',w/'r2/rc7/include',w/'r2/rc7/protocol',crypto/'include',REPO/'firmware/w12/tests/r2_fakes',REPO/'firmware/w12/tests/console_fakes',root/'firmware/w12/legacy/reference'):flags+=['-I'+str(path)]
cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang');cxx=os.environ.get('CXX',cc+'++')
files=[w/'r2/r2-g1'/(n+'.c') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[w/'r2/rc7/protocol/rc_dynamic.c',w/'r2/rc7/src/rc_boundary.c',w/'r2/rc7/src/rc_slots.c',w/'g1_hmac.c',APP/'protocol/gm7_codec.c',crypto/'src/sha256.c',crypto/'src/utils.c'];objects=[]
def run(cmd,name):
 with (out/(name+'.log')).open('w') as log:subprocess.run(list(map(str,cmd)),stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
for file in files:
 obj=out/(file.stem+'.o');run([cc,'-std=c11',*flags,'-c',file,'-o',obj],file.stem+'-compile');objects.append(obj)
run([cc,'-std=c11',*flags,REPO/'firmware/w12/tests/time_zone_codec_test.c',*objects,'-o',out/'tz-codec'],'tz-codec-compile')
run([out/'tz-codec'],'tz-codec')
run([cxx,'-std=c++20',*flags,'-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100',out/'native.cpp',*objects,'-o',out/'native'],'native-compile')
run([cxx,'-std=c++20',*flags,'-DV12_R2_RX_SHADOW=1','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100',out/'native.cpp',*objects,'-o',out/'native-fine'],'native-fine-compile')
cases=['corese-gm-preference','corese-mesh','corese-rc-tz','corese-tz','boot-empty','leader-incarnation-boot','leader-incarnation-epoch','leader-incarnation-train','leader-loss','leader-loss-boundary','leader-loss-pin','leader-loss-switch','sync-term-ledger','sync-term-ack-delayed','sync-term-inline','sync-term-error']
for case in cases:run([out/('native-fine' if case in ('corese-mesh','corese-rc-tz') else 'native'),case],case)
(out/'receipt.json').write_text(json.dumps({'status':'PASS','cases':cases,'hardware_access':False,'scope':'ACTUAL_CORESE_PRECISION_TZ_OVERLAY_PUBLIC_FAKES'},indent=2)+'\n')
print('PASS actual CoreSE TZ/receiver owner; '+str(len(cases))+' schedules, ASan/UBSan; no hardware')
