// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Synthetic public test enrollment only. Exercise actual unchanged GS codec/HMAC.
#include "mesh_diagnostics.hpp"
extern "C" {
#include "../../t096/protocol/role_announcement.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace corese::mesh;
int main(){
 r2_group_config group{{1,2,3},17,9};RoleMonitor monitor;
 r2_role_record r{};r.sender=r.root=1;r.sender_boot=r.root_boot=2;r.source_epoch=3;
 r.hot=r.coordinator=4;r.hot_boot=5;r.members=3;r.flags=3;r.valid_us=2000000;
 uint8_t bytes[R2_ROLE_AD_BYTES]{};Scope selected{1,2,3};
 auto send=[&](uint64_t at){++r.sequence;r.created_us=at;assert(!r2_role_encode(bytes,&r,&group));return monitor.receive(bytes,sizeof bytes,group,r.sender,selected,at,at);};
 assert(send(1000000));auto v=monitor.snapshot(1000000);assert(v.primary_since_us==1000000&&v.hot_since_us==1000000);
 assert(send(2000000));v=monitor.snapshot(2000000);assert(v.primary_since_us==1000000&&v.hot_since_us==1000000);
 const auto until=v.until_us;assert(v.created_us==2000000&&v.epoch_since_us==1000000);
 assert(!monitor.receive(bytes,sizeof bytes,group,1,selected,2200000,2200000));
 assert(monitor.snapshot(2200000).until_us==until); // Duplicate bytes cannot renew arrival/TTL.
 r.hot=r.coordinator=6;r.hot_boot=7;assert(send(2500000));v=monitor.snapshot(2500000);
 assert(v.primary_since_us==1000000&&v.hot_since_us==2500000);
 r.hot=r.hot_boot=0;r.coordinator=1;r.flags=1;assert(send(3000000));assert(monitor.snapshot(3000000).hot_since_us==0);
 // Busy GM / missed reports: retain descriptive roles and event timers, but
 // never extend the signed GS validity or grant any timing authority.
 const auto cached=monitor.snapshot(3600000000ULL);
 assert(cached.known&&cached.root==1&&cached.primary_since_us==1000000&&cached.hot==0);
 assert(cached.received_us==3000000&&cached.until_us==5000000&&!live(cached,3600000000ULL));
 assert(send(5000000));assert(monitor.snapshot(5000000).primary_since_us==1000000&&monitor.snapshot(5000000).epoch_since_us==1000000);
 bytes[96]^=1;assert(!monitor.receive(bytes,sizeof bytes,group,1,selected,5100000,5100000));bytes[96]^=1;
 assert(monitor.snapshot(5100000).rejected_auth==1);
 assert(!monitor.receive(bytes,sizeof bytes,group,9,selected,5100000,5100000));
 assert(!monitor.receive(bytes,sizeof bytes,group,1,{1,9,3},5100000,5100000));
 assert(!monitor.receive(bytes,sizeof bytes,group,1,{1,2,4},5100000,5100000));
 assert(!monitor.receive(bytes,sizeof bytes,group,1,selected,6000000,8000000));
 assert(!monitor.receive(bytes,sizeof bytes,group,1,selected,6000000,5999999));
 r.source_epoch=selected.epoch=4;assert(send(6000000));assert(monitor.snapshot(6000000).primary_since_us==1000000&&monitor.snapshot(6000000).epoch_since_us==6000000);
 r.hot=r.coordinator=6;r.hot_boot=8;r.flags=3;assert(send(6500000));assert(monitor.snapshot(6500000).hot_since_us==6500000);
 ++r.hot_boot;assert(send(7000000));assert(monitor.snapshot(7000000).hot_since_us==7000000);
 uint8_t earlier[R2_ROLE_AD_BYTES];std::memcpy(earlier,bytes,sizeof bytes);
 r.root=r.sender=10;r.root_boot=r.sender_boot=20;r.source_epoch=3;r.sequence=0;selected={10,20,3};assert(send(7500000));
 v=monitor.snapshot(7500000);assert(v.primary_since_us==7500000&&v.hot_since_us==7000000);
 assert(!monitor.receive(earlier,sizeof earlier,group,1,{1,2,4},7600000,7600000)); // Scope switches keep replay fences.
 // Wrong enrollment, malformed length, reserved-byte mutation and MAC corruption.
 auto wrong=group;++wrong.epoch;assert(!monitor.receive(bytes,sizeof bytes,wrong,10,selected,7600000,7600000));
 assert(!monitor.receive(bytes,sizeof bytes-1,group,10,selected,7600000,7600000));
 bytes[97]=1;assert(!monitor.receive(bytes,sizeof bytes,group,10,selected,7600000,7600000));
 // Same identities after a long omission preserve both running timers.
 assert(send(4000000000ULL));v=monitor.snapshot(4000000000ULL);
 assert(v.primary_since_us==7500000&&v.hot_since_us==7000000&&live(v,4000000000ULL));
 r.members=7;assert(send(4001000000ULL));v=monitor.snapshot(4001000000ULL);
 assert(v.primary_since_us==7500000&&v.hot_since_us==7000000);
 // A primary reboot is a mesh event; only the affected timer restarts.
 r.root_boot=r.sender_boot=selected.boot=21;r.sequence=0;assert(send(4002000000ULL));
 v=monitor.snapshot(4002000000ULL);assert(v.primary_since_us==4002000000ULL&&v.hot_since_us==7000000&&v.epoch_since_us==7500000);
 // Same mesh label across successive new roots retains its local first-seen age.
 r.root=r.sender=30;r.root_boot=r.sender_boot=31;selected={30,31,3};r.sequence=0;assert(send(4002500000ULL));
 v=monitor.snapshot(4002500000ULL);assert(v.primary_since_us==4002500000ULL&&v.epoch_since_us==7500000);
 r.root=r.sender=40;r.root_boot=r.sender_boot=41;selected={40,41,3};r.sequence=0;assert(send(4002750000ULL));
 v=monitor.snapshot(4002750000ULL);assert(v.primary_since_us==4002750000ULL&&v.epoch_since_us==7500000);
 r.source_epoch=selected.epoch=987654321;assert(send(4002800000ULL));
 assert(monitor.snapshot(4002800000ULL).epoch_since_us==4002800000ULL);
 r.hot=r.hot_boot=0;r.coordinator=40;r.flags=1;assert(send(4003000000ULL));
 assert(monitor.snapshot(4003000000ULL).hot_since_us==0);
 std::puts("PASS actual GS v1/HMAC: scope, replay, original expiry, indefinite descriptive cache, independent role identity/boot timers, HOT withdrawal; GS gaps and source epoch do not reset stability; no authority");
}
