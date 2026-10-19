// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "ss3_td_model.hpp"
extern "C" {
#include "r2/r2-g1/role_announcement.h"
}
namespace v12 {
// Diagnostic worker only. No scanner, timing, election or task-priority control.
struct TdRoleUpdate {
  static constexpr uint64_t CheckUs=1000000, HotUs=10000000, PollUs=120000000;
  r2_role_record previous{};
  uint64_t next_check_us=0,hot_since_us=0;
  bool ready(uint64_t now){
    if(now<next_check_us)return false;
    next_check_us=now+CheckUs;return true;
  }
  // Caller has authenticated, age-checked and bound this report to the selected
  // GM/address/boot/source. Only distinct signed sequences count as observations.
  bool observe(TdSnapshot::Role& role,const r2_role_record& report,uint64_t at,uint64_t now,bool routine){
    const bool scope=previous.root!=report.root||previous.root_boot!=report.root_boot||previous.source_epoch!=report.source_epoch;
    if(previous.root==report.root&&previous.root_boot==report.root_boot&&report.sequence<=previous.sequence)return false;
    if(report.hot){
      if(scope||previous.hot!=report.hot||previous.hot_boot!=report.hot_boot||!hot_since_us||at<hot_since_us)hot_since_us=at;
    }else hot_since_us=0;
    previous=report;
    const bool stable=report.hot&&at>=hot_since_us&&at-hot_since_us>=HotUs;
    const uint64_t hot=stable?report.hot:0,hot_boot=stable?report.hot_boot:0;
    const bool changed=!role.known||role.root!=report.root||role.root_boot!=report.root_boot||
      role.source_epoch!=report.source_epoch||role.hot!=hot||role.hot_boot!=hot_boot||
      role.coordinator!=report.coordinator||role.members!=report.members;
    if(!routine&&!changed)return false;
    role.root=report.root;role.root_boot=report.root_boot;role.source_epoch=report.source_epoch;
    role.hot=hot;role.hot_boot=hot_boot;role.coordinator=report.coordinator;role.members=report.members;
    role.sequence=report.sequence;role.seen_us=at;role.checked_us=now;role.next_us=now+PollUs;
    role.known=role.available=true;role.result=TdSnapshot::Role::Report;role.until_us=0;
    TdModel::inc(role.checks);if(changed)TdModel::inc(role.events);return true;
  }
};
}
