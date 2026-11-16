#!/usr/bin/env python3
"""Actual watch receiver: signed permission expiry repaired within one owner poll.
SDK radio/counter are fakes. No hardware or physical precision qualification.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1];W12=APP.parent/'w12';N30=APP.parent/'n30'
BODY=r'''
int main(){
 const uint8_t mac[]={2,3,4,5,6,7};assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
 fake_now=100000000;fresh_direct(501,601,2,3);step(25);
 v12::Discovery locator{};locator.props=1;locator.sid=12;locator.interval=50;locator.at=fake_now;locator.address=v12::canonical(501);locator.rssi=-30;locator.length=25;
 locator.bytes[0]=24;locator.bytes[1]=255;auto* ad=locator.bytes+2;ad[0]='G';ad[1]='1';ad[2]=1;
 r2_group_put(ad+3,v12::provision.group.id,8);r2_group_put(ad+11,v12::provision.group.epoch,4);r2_group_put(ad+15,501,8);
 v12::discoveries.push(locator);step();
 // A real fixture JOIN authorized the direct source. Complete its PA owner.
 assert(v12::sync_pending&&fixture_sync_arg);
 ble_gap_event e{};e.type=BLE_GAP_EVENT_PERIODIC_SYNC;e.periodic_sync.sid=12;e.periodic_sync.sync_handle=9;
 e.periodic_sync.per_adv_ival=50;e.periodic_sync.adv_addr=v12::sync_owner_address;
 v12::gap(&e,fixture_sync_arg);assert(v12::synced&&!v12::fault);
 const auto source_offset=int64_t(v12::anchor.utc-v12::anchor.at)*1000;const uint64_t issued=fake_now*1000+9000000000ULL-1000000;
 uint64_t previous_raw=0;uint16_t previous_event=0;unsigned publication=0;
 const auto frame=[&](bool correct,uint64_t life){
  ++publication;v12::Periodic row{};row.at=fake_now;row.identity=501;row.length=190;row.signal_epoch=v12::sync_capture_epoch;
  auto& stamp=row.capture;stamp.actual=stamp.association_observed=true;stamp.tick20=(fake_now-100)*20;stamp.available20=stamp.tick20+200;
  stamp.epoch=v12::sync_capture_epoch;stamp.sequence=publication;stamp.event=uint16_t(publication);stamp.channel=4;stamp.map_enclosure_ns=1000;
  const auto raw=(fake_now-100)*1000+9000000000ULL;
  r2_relay_packet p{};p.origin={0,1,601,2,issued,raw+life,issued,source_offset-9000000000LL,0,0};
  p.route={0,0,0,0,601,3,publication,10,3,UINT32_MAX};
  if(correct){p.count=1;p.correction[0]={publication-1,previous_event,4,previous_raw*2/125,3};}
  const uint64_t ids[4]={501,0,0,0};uint8_t proof[32];assert(!r2_group_origin_sign3(proof,&p.origin,&v12::provision.group,ids));assert(!r2_group_relay_encode3(row.bytes,&p,proof,&v12::provision.group,ids));
  assert(v12::periodic.push(row));previous_raw=raw;previous_event=uint16_t(publication);
 };
 frame(false,200000000);v12::active_poll(fake_now);pump_cleanup();v12::publish(fake_now);
 fake_now+=62500;frame(true,200000000);v12::active_poll(fake_now);pump_cleanup();v12::publish(fake_now);
 assert(v12::corrected_clock.accepted()==1&&v12::following(fake_now)&&v12::receive_loss.generation);
 const auto deadline=v12::corrected_clock.until();assert(deadline>fake_now+20000);
 fake_now=deadline-20000;frame(false,650000000);v12::active_poll(fake_now);pump_cleanup();v12::publish(fake_now);
 assert(v12::corrected_was_live&&v12::corrected_clock.covers(fake_now,fake_now));
 const auto accepted=v12::corrected_clock.accepted(),losses=v12::receive_loss.expiries,epoch=v12::sync_capture_epoch;
 const auto original=v12::corrected_clock.original20();
 fake_now=deadline+1;assert(!v12::corrected_clock.covers(fake_now,fake_now)&&!v12::following(fake_now));
 // The next authenticated report carries the pending original's unique physical correction.
 frame(true,650000000);v12::active_poll(fake_now);pump_cleanup();v12::publish(fake_now);
 const auto status=v12::r2_status();
 assert(v12::receive_loss.expiries==losses+1&&v12::corrected_clock.accepted()==accepted+1);
 assert(status.following&&!status.fault&&!status.g1_retained_estimate&&v12::synced&&v12::sync_capture_epoch==epoch);
 assert(v12::corrected_clock.original20()>original&&v12::corrected_clock.covers(fake_now,fake_now));
 printf("PASS actual authenticated direct JOIN + GR3 codec/physical pair: expiry counted before same-poll repair; scope/capture/following restored without radio fault; pair gap=%llu us\n",(unsigned long long)(fake_now-original/20));
}
'''

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--idf',type=Path,default=os.environ.get('IDF_PATH'));a=p.parse_args()
 if not a.idf:p.error('--idf or IDF_PATH is required for public TinyCrypt headers')
 os.environ['IDF_PATH']=str(a.idf.resolve())
 sys.path.insert(0,str(W12/'tools'));from rc7_permission_e2e import native_fixture
 out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);native_fixture(out)
 cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else 'clang');cxx=os.environ.get('CXX',cc+'++')
 crypto=a.idf/'components/bt/host/nimble/nimble/ext/tinycrypt'
 includes=[out,APP/'main',N30/'main',W12/'main/gm7',W12/'main',W12/'main/r2/r2-g1',W12/'main/r2/rc7/include',crypto/'include',APP/'tests',W12/'tests/r2_fakes',W12/'tests/console_fakes',W12/'legacy',W12/'legacy/reference']
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(q)for q in includes],'-DR2_TZ_WIRE=1']
 files=[W12/'main/r2/r2-g1'/(n+'.c')for n in ('group_auth','group_join','group_precision','precision_codec','relay_codec')]+[W12/'main/r2/rc7/protocol/rc_dynamic.c',W12/'main/r2/rc7/src/rc_boundary.c',W12/'main/r2/rc7/src/rc_slots.c',W12/'main/g1_hmac.c',crypto/'src/sha256.c',crypto/'src/utils.c',W12/'main/gm7/gm7_codec.c']
 def run(cmd,name):
  with (out/(name+'.log')).open('w')as log:subprocess.run(list(map(str,cmd)),check=True,stdout=log,stderr=subprocess.STDOUT,timeout=60)
 objects=[]
 for source in files:
  obj=out/(source.stem+'.o');run([cc,'-std=c11',*flags,'-c',source,'-o',obj],source.stem+'-compile');objects.append(obj)
 prefix=(out/'native.cpp').read_text().split('#define V12_RC7_PERMISSION_NATIVE 1')[0]
 prefix+='\nbool watch_fixture_can_admit=true,watch_fixture_cancel_at_start=false; namespace v12 { bool transport_watch_ble_admit_allowed(bool){return watch_fixture_can_admit;} bool transport_watch_ble_admit_begin(bool){return watch_fixture_can_admit;} void transport_watch_ble_admit_end(){} }\n'
 support=(W12/'tests/precision_priority_fixture.inc').read_text().split('int main(')[0]
 (out/'expiry.cpp').write_text(prefix+'\nbool n30_fine_backend_selected(){return false;}\nint n30_fine_backend_receive(v12::periodic_rx::Stamp&){return -1;}\n'+support+BODY)
 defines=['-Wno-missing-field-initializers','-DV12_R2_ENABLED=1','-DV12_R2_FIELD_CLIENT=1','-DV12_R2_CONFIG=13','-DV12_UNIT="C1"','-DV12_R2_G1_LISTENING_PERCENT=100','-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192','-DPTOB_GM_SUCCESSOR=1','-DPTOB_N30=1','-DV12_G1_FAILED_PARENT_RETRY_US=60000000','-DPTOB_TWATCH=1']
 run([cxx,'-std=c++20',*flags,*defines,out/'expiry.cpp',*objects,'-o',out/'expiry'],'compile');run([out/'expiry'],'runtime')
 files+=[Path(__file__),W12/'main/native_g1.cpp',W12/'main/g1_corrected_clock.hpp',W12/'main/g1_receive_loss.hpp',W12/'main/g1_client_policy.hpp',W12/'tools/rc7_permission_e2e.py',W12/'tests/native_g1_test.cpp',W12/'tests/precision_priority_fixture.inc',APP/'main/roaming_clock.hpp',APP/'main/ble_acquisition_recovery.hpp']
 (out/'receipt.json').write_text(json.dumps(dict(status='PASS',scope='ACTUAL_AUTHENTICATED_JOIN_GR3_CODEC_AND_RECEIVER_OWNER_ASAN_UBSAN_SDK_COUNTER_FAKES',hardware_access=False,core_modified=False,sources={str(q.resolve()):hashlib.sha256(q.read_bytes()).hexdigest()for q in files}),indent=2)+'\n')
 print((out/'runtime.log').read_text(),end='')
if __name__=='__main__':main()
