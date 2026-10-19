// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "td_role_update.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
int main(){
 TdRoleUpdate updates;TdSnapshot::Role role;
 r2_role_record r{};r.root=r.sender=1;r.root_boot=r.sender_boot=2;r.source_epoch=3;
 r.hot=r.coordinator=4;r.hot_boot=5;r.members=3;r.sequence=1;
 auto observe=[&](uint64_t at,bool routine=false){++r.sequence;return updates.observe(role,r,at,at,routine);};
 assert(updates.ready(1)&&!updates.ready(1000000)&&updates.ready(1000001));
 assert(observe(1000000)&&role.known&&!role.hot&&role.events==1);
 assert(!observe(10999999)&&!role.hot);assert(observe(11000000)&&role.hot==4&&role.events==2);
 const auto due=role.next_us,seen=role.seen_us;
 assert(!observe(12000000)&&role.next_us==due&&role.seen_us==seen); //1Hz heartbeats do not become events.
 assert(!updates.observe(role,r,13000000,13000000,true)); // Same signed report cannot refresh state.
 r.hot=0;r.hot_boot=0;assert(observe(14000000)&&!role.hot); // Withdrawal is immediate.
 r.hot=6;r.hot_boot=7;assert(!observe(15000000));assert(!observe(24999999));
 r.hot=8;r.hot_boot=9;assert(!observe(25000000));assert(!observe(34999999));
 assert(observe(35000000)&&role.hot==8); // Flapping restarts the10s dwell.
 ++r.hot_boot;assert(observe(36000000)&&!role.hot);assert(observe(46000000)&&role.hot_boot==10);
 ++r.members;assert(observe(47000000)&&role.members==4);
 ++r.source_epoch;assert(observe(48000000)&&role.source_epoch==4&&!role.hot);
 assert(!observe(49000000));assert(observe(58000000)&&role.hot==8);
 ++r.root_boot;r.sender_boot=r.root_boot;r.sequence=1;
 assert(observe(59000000)&&role.root_boot==3&&!role.hot); // New scope is prompt and restarts HOT.
 assert(observe(69000000)&&role.hot==8);
 r.root=r.sender=11;r.hot=0;r.hot_boot=0;r.coordinator=11;r.sequence=1;
 assert(observe(70000000)&&role.root==11&&!role.hot);
 assert(observe(71000000,true)&&role.checked_us==71000000); // Routine fallback retained.
 puts("PASS SS3 role event updates,10s observed HOT dwell,withdrawal,flapping,boot/source/member changes,replay and unchanged heartbeat");
}
