"""Exact G1/RX-shadow acquisition lifecycle with explicit public-controller/clock stubs; no hardware."""
import os
import json,subprocess,hashlib,sys
from pathlib import Path
app=Path(__file__).resolve().parents[1];work=app.parent
out=Path(sys.argv[1]).resolve();out.mkdir(exist_ok=False,parents=True);host=Path(sys.argv[2]).resolve()
# Exact actual native_g1.cpp compiled with RX_SHADOW; adapter lifecycle from actual candidate bodies.
def body(p,name):
 s=p.read_text();start=s.index(name+'(');begin=s.rfind('\n',0,start)+1;brace=s.index('{',start);depth=1;i=brace+1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[begin:i]
s=(app/'tests/native_g1_test.cpp').read_text();s=s.replace('#include "../main/native_g1.cpp"','''#define V12_R2_RX_SHADOW 1
#include "clock_ble_mapping.hpp"
using StackType_t=uint32_t;
inline unsigned uxTaskGetStackHighWaterMark(void*){return 2048;}
extern "C" bool clock_reference_read(uint64_t* t){*t=fake_now*20;return true;}
#include "'''+str(app/'main/native_g1.cpp')+'"')
s=s.replace('unsigned sync_cancel_calls=0;','unsigned sync_cancel_calls=0,sync_create_calls=0;void* sync_arg=nullptr;')
s=s.replace('int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t sid,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*){assert(sid==12);return 0;}','int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t sid,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void* arg){assert(sid==12);++sync_create_calls;sync_arg=arg;return 0;}')
s=s.replace('v12::gap(&e,nullptr);','v12::gap(&e,sync_arg);')
lifecycle='''namespace v12::periodic_rx {
Record records[Capacity]{};adv_rx::BleAnchor anchor{};
adv_rx::BleMapping clock_mapping{};int8_t refinement_ticks=0;bool refinement_ready=false;
uint32_t used[Capacity]{},head=0,epoch=0,fault=0,matched=0,missed=0,ambiguities=0,peak=0;
bool armed=false,installed=false,acquiring=false;uint16_t handle=UINT16_MAX;uint64_t anchor_us=0;
void barrier(){}bool pointers(){return true;}int xPortGetCoreID(){return 0;}int esp_clk_cpu_freq(){return 240000000;}
int64_t esp_timer_get_time(){return fake_now;}
adv_rx::BleTime r_rwip_time_get(){return {uint32_t(fake_now*2/625),uint32_t(fake_now*2%625)};}
bool start(){installed=true;return !fault;}
bool stop(){scope(UINT16_MAX,false);installed=false;return true;}
Stats status(){Stats s;s.installed=installed;s.armed=armed;s.fault=fault;s.epoch=epoch;s.matched=matched;s.missed=missed;return s;}
void descriptor(const uint8_t* bytes){if(!armed)return;auto& r=records[head++%Capacity];r={};r.anchor=anchor;r.observed_us=fake_now;r.length=Bytes;
 r.stamp.epoch=epoch;r.stamp.sequence=head;r.stamp.coarse=anchor.time.half_slots;r.stamp.fine=uint16_t(624-anchor.time.half_us);r.stamp.actual=r.stable=r.single=true;r.stamp.association_observed=true;r.stamp.event=10;r.stamp.channel=3;std::memcpy(r.bytes,bytes,Bytes);}
'''
for name in ('poll','scope','adopt','match'):lifecycle+=body(app/'main/periodic_rx.cpp',name)+'\n'
lifecycle+='}\n';pos=s.index('namespace {');s=s[:pos]+lifecycle+s[pos:]
cases='''  if(which.starts_with("capture-")){
    fake_now=450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active());
    v12::anchor={true,111,55,1700000000000000,0,2000000,2,3,40000};v12::joined_peer=111;v12::joined_boot=55;v12::sync_address=v12::canonical(111);
    if(which=="capture-exhaustion")v12::periodic_rx::epoch=UINT32_MAX;
    fake_now=1000000;v12::active_poll(fake_now);
    if(which=="capture-exhaustion"){assert(v12::fault&&!v12::sync_pending&&!sync_create_calls&&v12::periodic_rx::fault==4&&!v12::periodic_rx::armed);v12::r2_stop();step(2);assert(!v12::owners_active());return 0;}
    assert(v12::sync_pending&&sync_create_calls==1&&v12::sync_capture_epoch&&uintptr_t(sync_arg)==v12::sync_capture_epoch);
    const auto epoch=v12::sync_capture_epoch;const auto owner=v12::sync_owner_window_start;uint8_t packet[190]{};packet[0]=189;v12::periodic_rx::descriptor(packet);
    assert(v12::periodic_rx::head==1&&v12::periodic_rx::acquiring&&v12::periodic_rx::handle==UINT16_MAX);
    if(which=="capture-stale"){
      // Close one actual request, then create a newer pending owner through the public API.
      const auto old_arg=sync_arg;ble_gap_event e{};e.type=BLE_GAP_EVENT_PERIODIC_SYNC;e.periodic_sync.status=1;v12::gap(&e,old_arg);
      assert(!v12::sync_pending&&!v12::synced);fake_now+=70000;v12::active_poll(fake_now);
      const auto current=v12::sync_capture_epoch;const auto request=v12::sync_requested_at;
      assert(v12::sync_pending&&sync_create_calls==2&&current!=epoch&&uintptr_t(sync_arg)==current);
      v12::periodic_rx::descriptor(packet);assert(v12::periodic_rx::head==1);
      e={};e.type=BLE_GAP_EVENT_PERIODIC_SYNC;e.periodic_sync.per_adv_ival=50;e.periodic_sync.adv_addr=v12::sync_address;
      v12::gap(&e,old_arg);
      assert(v12::fault&&v12::sync_pending&&!v12::synced&&v12::sync_handle==UINT16_MAX&&v12::sync_capture_epoch==current&&v12::sync_requested_at==request&&v12::sync_owner_window_start==owner);
      v12::begin_close(fake_now);const auto deadline=v12::close_until;v12::close_poll(fake_now);pump_cleanup();
      fake_now+=v12::G1Window::SyncCancelGrace;v12::close_poll(fake_now);pump_cleanup();v12::close_poll(fake_now);
      assert(!v12::owners_active()&&v12::client.completed_us<deadline&&v12::fault);return 0;
    }
    if(which=="capture-cancel-repeated"||which=="capture-late-success"){
      fake_now=1450000;v12::stop_scan();v12::cancel_sync(fake_now);const auto fenced=v12::periodic_rx::epoch;const auto deadline=v12::sync_drain_until;
      assert(fenced!=epoch&&!v12::periodic_rx::armed&&!v12::periodic_rx::head);
      for(unsigned i=0;i<10;i++)v12::cancel_sync(fake_now+i);
      assert(v12::periodic_rx::epoch==fenced&&v12::sync_drain_until==deadline&&sync_cancel_calls==0);
      if(which=="capture-late-success"){sync_established();assert(v12::synced&&!v12::periodic_rx::acquiring&&!v12::periodic_rx::armed&&!v12::periodic_rx::head);}
      if(which=="capture-cancel-repeated"){pump_cleanup();assert(!sync_cancel_calls);fake_now+=v12::G1Window::SyncCancelGrace;v12::cancel_sync(fake_now);}
      pump_cleanup();assert(!v12::owners_active()&&v12::sync_drain_until==deadline&&!v12::fault);return 0;
    }
    sync_established();assert(v12::synced&&!v12::fault&&v12::periodic_rx::head==1&&!v12::periodic_rx::acquiring);
    v12::periodic_rx::Stamp stamp;assert(v12::periodic_rx::match(v12::sync_handle,packet,190,fake_now*20+100,stamp));
    assert(stamp.actual&&stamp.epoch==epoch&&!stamp.event_valid&&!stamp.channel_valid&&!stamp.edge_calibrated);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
'''
s=s.replace('  if(which=="rebootstrap-retains-acquisition"){',cases+'  if(which=="rebootstrap-retains-acquisition"){',1)
p=out/'native-acquisition.cpp';p.write_text(s)
flags=['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-missing-field-initializers','-fsanitize=address,undefined','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192']
(out/'r2_bench_enrollment.h').write_text('static const r2_group_enrollment provision={{{'+','.join(map(str,range(32)))+'},UINT64_C(0x123456789abcdef0),1},UINT64_C(0x0000000000000005),"C-1",40000};\n')
for path in [out,app/'tests/r2_fakes',app/'tests/console_fakes',app/'main',app/'legacy/reference',host,app/'main/r2/r2-g1',Path(os.environ['IDF_PATH'])/'components/bt/host/nimble/nimble/ext/tinycrypt/include']:flags.append('-I'+str(path))
objects=[host/(n+'.o') for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec','g1_hmac','sha256','utils')]
binary=out/'native-acquisition';subprocess.run([os.environ.get('CXX','clang++'),*flags,str(p),*[str(x) for x in objects],'-o',str(binary)],check=True)
checks=['capture-first','capture-stale','capture-cancel-repeated','capture-late-success','capture-exhaustion','known-train-first','rebootstrap-retains-acquisition','listening-profile','sync-cancel-grace']
for c in checks:subprocess.run([str(binary),c],check=True);print('PASS',c)
full_binary=out/'native-default100';subprocess.run([os.environ.get('CXX','clang++'),*flags,'-DV12_R2_G1_LISTENING_PERCENT=100',str(p),*[str(x) for x in objects],'-o',str(full_binary)],check=True)
subprocess.run([str(full_binary),'listening-profile'],check=True);print('PASS default100-listening-profile')
checks.append('default100-listening-profile')
(out/'native-acquisition-receipt.json').write_text(json.dumps({'status':'PASSED_RX_SHADOW_NATIVE_AND_EXACT_ADAPTER_LIFECYCLE','cases':checks,'native_source_sha256':hashlib.sha256((app/'main/native_g1.cpp').read_bytes()).hexdigest(),'adapter_source_sha256':hashlib.sha256((app/'main/periodic_rx.cpp').read_bytes()).hexdigest(),'stubs':'Public NimBLE/controller descriptors/clock/task are explicit fixtures; no physical RF/actual ISR pass','precision':'UNAVAILABLE;all strict flagsfalse'},indent=2)+'\n')
