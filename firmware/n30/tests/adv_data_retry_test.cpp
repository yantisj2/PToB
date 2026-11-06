// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// The actual capability owner, with deterministic public API return values.
#include "adv_data_retry.hpp"
#ifdef PTOB_W12_BLE_RECOVERY
#include "w12_adv_data_retry.hpp"
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
int result=0;bool fault=false,relay_cap_dirty=true,relay_pa=false;
struct Peer {unsigned handle=None;bool connecting=false;}peer,relay_session;
struct Path{};struct Cap {Path path;r2_tz tz{};uint64_t next_challenge=0;};
Path relay_cap_path;
struct Map {Cap capability(uint64_t,uint64_t,uint64_t,bool){return {};}}relay_map;
struct Provision {uint64_t identity=2;unsigned group=1;}provision;
struct Service {uint8_t value[16]{};}rc_service;
r2_tz_lease coarse_tz{};uint8_t relay_cap[216]{};
enum class R2Api {AdvData};
uint64_t micros(){return now;}
void inc(uint32_t& n){++n;}
void note(int rc,R2Api){assert(rc==0x207);++notes;}
void error(int,R2Api,uint8_t detail=0){fault=true;details=detail;}
bool relay_api(int rc,R2Api site){if(rc)error(rc,site);return !rc;}
void relay_pause_cap(){++pauses;}
int rc7_control_encode(uint8_t* out,const Cap* c,unsigned*){r2_tz_put(out+196,c->tz);return 0;}
int rc7_capability_encode(uint8_t*,const Cap*,unsigned*){return 0;}
void* ble_hs_mbuf_from_flat(const void*,unsigned n){assert(n==171);return reinterpret_cast<void*>(1);}
int ble_gap_ext_adv_set_data(unsigned instance,void* data){assert(instance==1&&data);++calls;return result;}
#include "actual_adv_cap_update.inc"
int main(int argc,char** argv){
 (void)argv;
 assert(argc==2);coarse_tz.value={2,-360};coarse_tz.until_us=now+2000000;
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
  now+=100000;assert(!relay_cap_update(now)&&fault&&details==61&&calls==3&&!relay_cap_at&&relay_cap_dirty);
 }
#else
 assert(fault&&calls==1&&setup_retries==0&&details==0);
#endif
 std::puts("PASS actual paused capability owner: success-only publication, bounded capacity retry, fatal other/persistent errors and unchanged shared targets");
}
