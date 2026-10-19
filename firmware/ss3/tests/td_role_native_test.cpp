// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "td_role_update.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace v12;
struct Address {unsigned value=1;};
bool same_address(Address a,Address b){return a.value==b.value;}
struct Candidate {uint8_t wire[R2_ROLE_AD_BYTES]{};uint64_t id=0,at=0;Address address{};bool checked=false;};
struct Storage {TdModel model{};uint64_t ids[8]{};struct {Address address;}locators[8]{};Candidate roles[4]{};TdRoleUpdate role_update{};};
Storage storage,*td_storage=&storage;unsigned td_phase=0;uint64_t td_until=4000000;
struct {r2_group_config group{{1,2,3},17,9};} provision;
#include "td_role_poll_under_test.hpp"
int main(){
 auto& model=storage.model;auto& c=storage.roles[0];auto& role=model.view.role;
 r2_role_record r{};r.sender=r.root=1;r.sender_boot=r.root_boot=2;r.hot=r.coordinator=4;r.hot_boot=5;
 r.source_epoch=3;r.valid_us=2000000;r.flags=3;r.members=3;
 auto report=[&](uint64_t now){
  ++r.sequence;r.created_us=now;c.id=1;c.at=now;c.checked=false;assert(!r2_role_encode(c.wire,&r,&provision.group));
  model.seen(1,1,false,0,true,true,-30,1,now);model.peer(1).boot=2;model.peer(1).source_epoch=3;
  td_until=now+4000000;
 };
 report(1000000);td_role_poll(1000000);assert(!role.known); // Locator can arrive after role bytes.
 storage.ids[0]=1;td_role_poll(2000000);assert(role.known&&!role.hot&&role.events==1);
 report(10000000);td_role_poll(10000000);assert(!role.hot);
 report(11000000);td_role_poll(11000000);assert(role.hot==4&&role.events==2&&role.checked_us<120000000);
 auto checks=role.checks;report(12000000);td_role_poll(12000000);assert(role.checks==checks);
 r.hot=r.hot_boot=0;r.flags=1;report(13000000);td_role_poll(13000000);assert(!role.hot&&role.events==3);
 auto events=role.events;r.members=7;report(14000000);c.wire[96]^=1;td_role_poll(14000000);assert(role.events==events&&role.result==TdSnapshot::Role::Invalid);
 report(15000000);c.address.value=2;td_role_poll(15000000);assert(role.events==events&&role.result==TdSnapshot::Role::WrongScope);c.address.value=1;
 report(16000000);model.peer(1).boot=9;td_role_poll(16000000);assert(role.events==events&&role.result==TdSnapshot::Role::WrongScope);
 report(17000000);model.peer(1).source_epoch=9;td_role_poll(17000000);assert(role.events==events&&role.result==TdSnapshot::Role::WrongScope);
 report(18000000);c.at=15000000;td_role_poll(18000000);assert(role.events==events&&role.result==TdSnapshot::Role::Expired);
 report(19000000);c.at=20000000;td_role_poll(19000000);assert(role.events==events&&role.result==TdSnapshot::Role::Expired);
 report(20000000);r.sequence=role.sequence;r.created_us=20000000;assert(!r2_role_encode(c.wire,&r,&provision.group));td_role_poll(20000000);assert(role.events==events&&role.result==TdSnapshot::Role::Replay);
 r.sequence+=20;report(21000000);td_role_poll(21000000);assert(role.events==events+1&&role.members==7);
 // No valid report in the original routine window: one miss, no retry burst.
 const auto due=role.next_us;td_until=due+4000000;td_role_poll(due);assert(role.until_us==td_until);
 td_role_poll(td_until);assert(role.misses==1&&!role.until_us&&role.next_us==td_until+TdRoleUpdate::PollUs);
 puts("PASS actual SS3 role worker with signed codec: events before120s,HOT dwell,MAC,address,boot,epoch,age,replay,late locator and bounded fallback");
}
