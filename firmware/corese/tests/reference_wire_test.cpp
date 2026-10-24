// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Public synthetic enrollment. Actual GD2/legacy GD1 codec/HMAC and observer.
#include "mesh_diagnostics.hpp"
extern "C" {
#include "../../t096/protocol/gnss_diagnostics.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace corese::mesh;
int main(){
 // Public independently computed known-answer vectors (synthetic enrollment).
 {
  r2_group_config g{{1},UINT64_C(0x1122334455667788),7};
  const char* hex1="57ffffff4744010088776655443322110700000008070605040302018070605040302010090000000300000040420f000000000080c3c9011f070c0101000200030080f8fe80ffff1ff9e7181881b782d8684b1cab58536a";uint8_t packet1[88]{};
  for(unsigned i=0;i<88;++i){unsigned v=0;assert(std::sscanf(hex1+2*i,"%2x",&v)==1);packet1[i]=uint8_t(v);}
  r2_gnss_record decoded1{};assert(!r2_gnss_decode(&decoded1,packet1,sizeof packet1,&g));
  ReferenceMonitor v1;Scope scope1{decoded1.root,decoded1.root_boot,decoded1.mesh_epoch};
  assert(v1.receive(packet1,sizeof packet1,g,decoded1.root,scope1,1000000,1000000));
  const auto report1=v1.snapshot(1000000);assert(report1.wire_version==1);
  assert(report1.hdop_centi==UINT16_MAX&&!(report1.fields&Hdop));
  const char* hex2="59ffffff4744020088776655443322110700000008070605040302018070605040302010090000000300000040420f000000000080c3c9013f070c0101000200030080f8fe80ffff7b0037106eedd8a991b4aa02df09ca6db990";uint8_t packet2[90]{};
  for(unsigned i=0;i<90;++i){unsigned v=0;assert(std::sscanf(hex2+2*i,"%2x",&v)==1);packet2[i]=uint8_t(v);}
  r2_gnss_record decoded2{};assert(!r2_gnss_decode(&decoded2,packet2,sizeof packet2,&g));
  ReferenceMonitor v2;Scope scope2{decoded2.root,decoded2.root_boot,decoded2.mesh_epoch};
  assert(v2.receive(packet2,sizeof packet2,g,decoded2.root,scope2,1000000,1000000));
  const auto report2=v2.snapshot(1000000);assert(report2.wire_version==2);
  assert(report2.hdop_centi==123&&(report2.fields&Hdop)&&report2.sats==12);
 }
 r2_group_config group{{1,2,3},17,9};ReferenceMonitor monitor;
 r2_gnss_record r{};r.root=1;r.root_boot=2;r.mesh_epoch=3;r.sequence=1;r.sampled_us=1000000;r.valid_us=30000000;
 r.known=63;r.hdop_centi=80;r.values=7;r.sats=14;r.source_kind=1;r.rmc_age_ms=10;r.pps_age_ms=20;r.gga_age_ms=30;
 r.priority1=128;r.clock_class=6;r.accuracy=254;r.priority2=129;r.variance=UINT16_MAX;
 uint8_t wire[R2_GNSS_AD_BYTES]{};Scope selected{1,2,3};
 auto encode=[&](){assert(!r2_gnss_encode(wire,&r,&group));};
 auto receive=[&](uint64_t at){encode();return monitor.receive(wire,sizeof wire,group,r.root,selected,at,at);};
 assert(receive(1000000));auto v=monitor.snapshot(1000000);
 assert(v.reported&&v.accepted==1&&v.scope.root==1&&v.scope.boot==2&&v.scope.epoch==3);
 assert(v.fields==63&&v.hdop_centi==80&&v.wire_version==2&&v.values==7&&v.sats==14&&v.source_kind==1);
 assert(v.rmc_age_ms==10&&v.pps_age_ms==20&&v.gga_age_ms==30);
 assert(v.priority1==128&&v.clock_class==6&&v.accuracy==254&&v.variance==UINT16_MAX&&v.priority2==129);
 assert(v.received_us==1000000&&v.until_us==31000000&&v.remote_sampled_us==1000000&&v.sequence==1);
 assert(current(v,selected,1000000)&&current(v,selected,30999999));
 assert(!current(v,selected,999999)&&!current(v,selected,31000000));
 assert(!current(v,{1,9,3},1000000)&&!current(v,{9,2,3},1000000)&&!current(v,{1,2,9},1000000));
 assert(!monitor.receive(wire,sizeof wire,group,1,selected,2000000,2000000));
 v=monitor.snapshot(2000000);assert(v.replayed==1&&v.received_us==1000000&&v.until_us==31000000);
 uint8_t first[sizeof wire];std::memcpy(first,wire,sizeof wire);
 // Decode-only GD1 compatibility: old authenticated layout cannot manufacture
 // HDOP, and the version/length/domain combination remains strict.
 uint8_t legacy[R2_GNSS_LEGACY_AD_BYTES]{};std::memcpy(legacy,wire,R2_GNSS_LEGACY_AD_BODY);
 legacy[0]=R2_GNSS_LEGACY_AD_BYTES-1;legacy[6]=1;legacy[56]&=31;
 assert(!r2_group_mac(legacy+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",legacy,R2_GNSS_LEGACY_AD_BODY));
 ReferenceMonitor old;assert(old.receive(legacy,sizeof legacy,group,1,selected,1000000,1000000));auto previous_version=old.snapshot(1000000);
 assert(previous_version.hdop_centi==UINT16_MAX&&!(previous_version.fields&Hdop)&&previous_version.wire_version==1);
 legacy[6]=2;assert(!r2_group_mac(legacy+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",legacy,R2_GNSS_LEGACY_AD_BODY));
 assert(!old.receive(legacy,sizeof legacy,group,1,selected,2000000,2000000));
 legacy[6]=1;legacy[56]|=32;assert(!r2_group_mac(legacy+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",legacy,R2_GNSS_LEGACY_AD_BODY));
 assert(!old.receive(legacy,sizeof legacy,group,1,selected,2000000,2000000));
 // Authentication, exact selected scope, original TTL, future receipt and overflow.
 wire[58]^=1;assert(!monitor.receive(wire,sizeof wire,group,1,selected,2000000,2000000));encode();
 assert(!monitor.receive(wire,sizeof wire-1,group,1,selected,2000000,2000000));
 auto wrong=group;++wrong.epoch;assert(!monitor.receive(wire,sizeof wire,wrong,1,selected,2000000,2000000));
 assert(!monitor.receive(wire,sizeof wire,group,9,selected,2000000,2000000));
 assert(!monitor.receive(wire,sizeof wire,group,1,{1,9,3},2000000,2000000));
 assert(!monitor.receive(wire,sizeof wire,group,1,{1,2,9},2000000,2000000));
 assert(!monitor.receive(wire,sizeof wire,group,1,selected,2000000,32000000));
 assert(!monitor.receive(wire,sizeof wire,group,1,selected,2000000,1999999));
 assert(!monitor.receive(wire,sizeof wire,group,1,selected,UINT64_MAX-1,UINT64_MAX-1));
 v=monitor.snapshot(UINT64_MAX);assert(v.accepted==1&&v.received_us==1000000&&v.until_us==31000000);
 // Mesh labels are runtime source context, never a compiled-in epoch. A scope
 // change within an incarnation cannot reset that root's independent fence.
 selected.epoch=r.mesh_epoch=678901234;r.sequence=2;r.sampled_us=3000000;assert(receive(3000000));
 assert(!monitor.receive(first,sizeof first,group,1,{1,2,3},4000000,4000000));
 uint8_t previous[sizeof wire];std::memcpy(previous,wire,sizeof wire);
 r.sequence=3;r.sampled_us=2999999;encode();assert(!monitor.receive(wire,sizeof wire,group,1,selected,4000000,4000000));
 r.root=10;r.root_boot=20;r.sequence=1;r.sampled_us=5000000;selected={10,20,r.mesh_epoch};assert(receive(5000000));
 assert(!monitor.receive(previous,sizeof previous,group,1,{1,2,r.mesh_epoch},6000000,6000000));
 // A new timing/profile incarnation has a new sequence namespace.
 ++r.root_boot;selected.boot=r.root_boot;r.sampled_us=6000000;assert(receive(6000000));
 for(unsigned i=0;i<5;++i){r.root=30+i;r.root_boot=40+i;r.sampled_us+=1000000;selected={r.root,r.root_boot,r.mesh_epoch};assert(receive(r.sampled_us));}
 // Known NO differs from unknown. Unknown satellites/PTP retain wire sentinels.
 r.known=Fix|Pps|Qualified;r.hdop_centi=UINT16_MAX;r.values=0;r.sats=UINT8_MAX;
 r.rmc_age_ms=UINT16_MAX;r.pps_age_ms=UINT16_MAX-1;r.gga_age_ms=UINT16_MAX;
 r.priority1=r.priority2=128;r.clock_class=248;r.accuracy=254;r.variance=UINT16_MAX;
 ++r.sequence;++r.sampled_us;assert(receive(r.sampled_us));v=monitor.snapshot(r.sampled_us);
 assert(v.fields==7&&!v.values&&v.sats==UINT8_MAX&&v.rmc_age_ms==UINT16_MAX&&v.pps_age_ms==UINT16_MAX-1);
 const auto accepted=v.accepted;v=monitor.snapshot(v.until_us);assert(v.reported&&!current(v,selected,v.until_us)&&v.accepted==accepted);
 std::puts("PASS actual GD2+legacyGD1/HMAC: exact timing incarnation/mesh scope, original TTL, duplicate nonrefresh, expiry, replay across selection/epoch changes, successive roots and explicit unknown/NO; no authority");
}
