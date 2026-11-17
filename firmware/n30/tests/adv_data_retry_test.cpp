// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// The actual capability owner, with deterministic public API return values.
#include "adv_data_retry.hpp"
#ifdef PTOB_W12_BLE_RECOVERY
#include "w12_adv_data_retry.hpp"
w12::AdvDataRetry relay_cap_retry;
#endif
#include "g1_time_zone.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using v12::G1TimeZoneOrder;
constexpr unsigned None=65535,RC_DYNAMIC_CAPABILITY_BYTES=148;
uint64_t now=1000000,relay_cap_at=0,relay_challenge=7,boot_id=3;
unsigned calls=0,pauses=0,notes=0,details=0;uint32_t setup_retries=0;
bool pause_owner_race=false,data_owner_race=false,remove_owner_race=false,closing=false,relay_stopping=false;int result=0;bool fault=false,relay_cap_dirty=true,relay_pa=false,relay_advertising=false;
struct Peer {unsigned handle=None;bool connecting=false;}peer,relay_session;
bool control_acl_owned(){return peer.connecting||peer.handle!=None||relay_session.handle!=None;}
struct Path{};struct Cap {Path path;r2_tz tz{};uint64_t next_challenge=0;};
Path relay_cap_path;
struct Map {Cap capability(uint64_t,uint64_t,uint64_t,bool){return {};}}relay_map;
struct Provision {uint64_t identity=2;unsigned group=1;}provision;
struct Service {uint8_t value[16]{};}rc_service;
r2_tz_lease coarse_tz{};uint8_t relay_cap[216]{};
enum class R2Api {AdvData,AdvRemove};
unsigned removes=0,configures=0;int remove_result=0,configure_result=0;
int ble_gap_ext_adv_remove(unsigned instance){assert(instance==1);++removes;if(remove_owner_race)relay_session.handle=7;return remove_result;}
bool relay_cap_configure(){++configures;if(configure_result)fault=true;return !configure_result;}
uint64_t micros(){return now;}
void inc(uint32_t& n){++n;}
void note(int rc,R2Api){assert(rc==0x207);++notes;}
void error(int,R2Api,uint8_t detail=0){fault=true;details=detail;}
bool relay_api(int rc,R2Api site){if(rc)error(rc,site);return !rc;}
void relay_pause_cap(){++pauses;if(pause_owner_race)relay_session.handle=7;}
int rc7_control_encode(uint8_t* out,const Cap* c,unsigned*){r2_tz_put(out+196,c->tz);return 0;}
int rc7_capability_encode(uint8_t*,const Cap*,unsigned*){return 0;}
void* ble_hs_mbuf_from_flat(const void*,unsigned n){assert(n==171);return reinterpret_cast<void*>(1);}
int ble_gap_ext_adv_set_data(unsigned instance,void* data){assert(instance==1&&data);++calls;if(data_owner_race)relay_session.handle=7;return result;}
#include "actual_adv_cap_update.inc"
int main(int argc,char** argv){
 (void)argv;
 assert(argc==2);coarse_tz.value={2,-360};coarse_tz.until_us=now+2000000;
#ifdef PTOB_W12_BLE_RECOVERY
 if(!std::strcmp(argv[1],"pause-owner-race")){pause_owner_race=true;assert(!relay_cap_update(now)&&!calls&&!fault&&relay_session.handle==7);return 0;}
 if(!std::strcmp(argv[1],"remove-owner-race")){result=0x207;assert(!relay_cap_update(now));now+=100000;assert(!relay_cap_update(now));now+=100000;remove_owner_race=true;assert(!relay_cap_update(now)&&!fault&&!configures&&removes==1);now+=100000;assert(!relay_cap_update(now)&&!configures&&!fault);relay_session.handle=None;result=0;assert(relay_cap_update(now)&&configures==1&&!fault);return 0;}
 if(!std::strcmp(argv[1],"deadline-success")){result=0x207;assert(!relay_cap_update(now));now+=500000;result=0;assert(!relay_cap_update(now)&&fault&&calls==1&&!removes&&!configures);return 0;}
 if(!std::strcmp(argv[1],"deadline")){result=0x207;assert(!relay_cap_update(now));now+=500000;assert(!relay_cap_update(now)&&fault&&!removes&&!configures);return 0;}
 if(!std::strcmp(argv[1],"data-owner-race")){result=0x207;assert(!relay_cap_update(now));now+=100000;assert(!relay_cap_update(now));now+=100000;data_owner_race=true;assert(!relay_cap_update(now)&&!fault&&!removes&&!configures&&relay_session.handle==7);now+=300000;relay_session.handle=None;data_owner_race=false;result=0;assert(!relay_cap_update(now)&&fault&&calls==3&&!removes);return 0;}
#endif
 result=0x207;assert(!relay_cap_update(now));assert(calls==1&&!relay_cap_at&&relay_cap_dirty);
#if defined(PTOB_N30) || defined(PTOB_W12_BLE_RECOVERY)
 assert(!fault&&setup_retries==1&&notes==1);
 const auto paused=pauses;now+=99999;assert(!relay_cap_update(now)&&calls==1&&pauses==paused);
 now++;if(!std::strcmp(argv[1],"recover"))result=0;
 if(!std::strcmp(argv[1],"other"))result=12;
 const bool accepted=relay_cap_update(now);
 if(!std::strcmp(argv[1],"recover")){
  assert(accepted&&!fault&&calls==2&&relay_cap_at==now&&!relay_cap_dirty);
 }else if(!std::strcmp(argv[1],"other")){
  assert(!accepted&&fault&&details==61&&calls==2&&!relay_cap_at&&relay_cap_dirty);
 }else{
  assert(!accepted&&!fault&&calls==2&&setup_retries==2);
  now+=100000;
#ifdef PTOB_W12_BLE_RECOVERY
  if(!std::strcmp(argv[1],"remove-fail"))remove_result=12;
  if(!std::strcmp(argv[1],"configure-fail"))configure_result=12;
  assert(!relay_cap_update(now)&&calls==3&&!relay_cap_at&&relay_cap_dirty&&removes==1);
  if(remove_result){assert(fault);return 0;}
  assert(!fault&&configures==0);now+=99999;assert(!relay_cap_update(now)&&calls==3);
  ++now;if(!std::strcmp(argv[1],"recreate"))result=0;
  const bool recovered=relay_cap_update(now);if(configure_result){assert(!recovered&&fault&&calls==3&&configures==1);return 0;}assert(calls==4&&removes==1&&configures==1);
  if(!std::strcmp(argv[1],"recreate")){assert(recovered&&!fault&&relay_cap_at==now&&!relay_cap_dirty);}
  else{
    assert(!recovered&&!fault&&!relay_cap_at&&relay_cap_dirty);
    if(!std::strcmp(argv[1],"recreate-late")){
      now+=100000;result=0;assert(relay_cap_update(now)&&!fault&&calls==5&&removes==1&&configures==1);
      assert(relay_cap_at==now&&!relay_cap_dirty);return 0;
    }
    // Persistent capacity uses the whole original500ms budget after the
    // single recreation. No additional remove/configure is admitted.
    now+=100000;assert(!relay_cap_update(now)&&!fault&&calls==5&&removes==1&&configures==1);
    now+=100000;assert(!relay_cap_update(now)&&fault&&details==61&&calls==5&&removes==1&&configures==1);
  }
#else
  assert(!relay_cap_update(now)&&fault&&details==61&&calls==3&&!relay_cap_at&&relay_cap_dirty);
#endif
 }
#else
 assert(fault&&calls==1&&setup_retries==0&&details==0);
#endif
 std::puts("PASS actual paused capability owner: success-only publication, bounded capacity retry, fatal other/persistent errors and unchanged shared targets");
}
