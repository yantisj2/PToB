// Monitor adapted from CoreSE mesh_wire.cpp; same authenticated wire guards.
// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "td_mesh.hpp"
extern "C" {
#include "r2/r2-g1/role_announcement.h"
#include "r2/r2-g1/gnss_diagnostics.h"
}
namespace v12::td_mesh {
namespace {void count(uint32_t& n){if(n!=UINT32_MAX)++n;}}
static_assert(Fix==R2_GNSS_K_FIX&&Pps==R2_GNSS_K_PPS&&Qualified==R2_GNSS_K_QUALIFIED&&Satellites==R2_GNSS_K_SATS&&Ptp==R2_GNSS_K_PTP&&Hdop==R2_GNSS_K_HDOP);
bool RoleMonitor::receive(const uint8_t* bytes,size_t size,const r2_group_config& group,uint64_t advertiser,
 Scope selected,uint64_t at,uint64_t now){
 r2_role_record r{};
 if(r2_role_decode(&r,bytes,size,&group)){count(state_.rejected_auth);state_.result=Result::Auth;return false;}
 if(!selected.root||!selected.boot||!selected.epoch||advertiser!=r.sender||
    selected.root!=r.root||selected.boot!=r.root_boot||selected.epoch!=r.source_epoch){
  count(state_.rejected_scope);state_.result=Result::Scope;return false;
 }
 if(now<at||now-at>=r.valid_us||at>UINT64_MAX-r.valid_us){
  count(state_.rejected_age);state_.result=Result::Expired;return false;
 }
 ReplayFence* fence=nullptr;unsigned replacement=0;
 for(unsigned i=0;i<8;++i){
  if(fences_[i].root==r.root&&fences_[i].boot==r.root_boot){fence=&fences_[i];break;}
  if(!fences_[i].root||fences_[i].seen<fences_[replacement].seen)replacement=i;
 }
 if(fence&&(r.sequence<=fence->sequence||r.created_us<fence->created)){
  count(state_.replayed);state_.result=Result::Replay;return false;
 }
 if(!fence)fence=&fences_[replacement];
 *fence={r.root,r.root_boot,r.created_us,now,r.sequence};
 const bool same_incarnation=state_.known&&state_.root==r.root&&state_.root_boot==r.root_boot;
 // A source-epoch change never resets the sequence replay fence within a boot.
 if(same_incarnation&&(r.sequence<=state_.sequence||r.created_us<remote_created_)){
  count(state_.replayed);state_.result=Result::Replay;return false;
 }
 const bool scope=same_incarnation&&state_.epoch==r.source_epoch;
 // Locally first-seen mesh-label age survives replacement root incarnations.
 if(!state_.known||state_.epoch!=r.source_epoch)state_.epoch_since_us=at;
 // Stability means unchanged reported role identities, not packet cadence.
 // The GM may omit GS while busy. Receipt TTL still expires independently;
 // neither this cache nor these timers grant timing or election authority.
 const bool primary_same=same_incarnation;
 const bool hot_same=state_.known&&state_.hot==r.hot&&state_.hot_boot==r.hot_boot;
 if(!primary_same)state_.primary_since_us=at;
 if(!hot_same)state_.hot_since_us=r.hot?at:0;
 if(!scope||state_.hot!=r.hot||state_.hot_boot!=r.hot_boot||state_.members!=r.members)count(state_.changes);
 state_.root=r.root;state_.root_boot=r.root_boot;state_.hot=r.hot;state_.hot_boot=r.hot_boot;
 state_.coordinator=r.coordinator;state_.epoch=r.source_epoch;state_.sequence=r.sequence;state_.members=r.members;
 state_.received_us=at;state_.until_us=at+r.valid_us;state_.known=true;
 state_.created_us=remote_created_=r.created_us;
 state_.result=Result::Accepted;count(state_.accepted);return true;
}
bool ReferenceMonitor::receive(const uint8_t* bytes,size_t size,const r2_group_config& group,uint64_t advertiser,
 Scope selected,uint64_t at,uint64_t now){
 r2_gnss_record r{};
 if(r2_gnss_decode(&r,bytes,size,&group)){count(state_.rejected_auth);state_.result=Result::Auth;return false;}
 if(!selected.root||!selected.boot||!selected.epoch||advertiser!=r.root||
    selected.root!=r.root||selected.boot!=r.root_boot||selected.epoch!=r.mesh_epoch){
  count(state_.rejected_scope);state_.result=Result::Scope;return false;
 }
 if(now<at||now-at>=r.valid_us||at>UINT64_MAX-r.valid_us){
  count(state_.rejected_age);state_.result=Result::Expired;return false;
 }
 ReplayFence* fence=nullptr;unsigned replacement=0;
 for(unsigned i=0;i<8;++i){
  if(fences_[i].root==r.root&&fences_[i].boot==r.root_boot){fence=&fences_[i];break;}
  if(!fences_[i].root||fences_[i].seen<fences_[replacement].seen)replacement=i;
 }
 if(fence&&(r.sequence<=fence->sequence||r.sampled_us<fence->sampled)){
  count(state_.replayed);state_.result=Result::Replay;return false;
 }
 if(!fence)fence=&fences_[replacement];
 *fence={r.root,r.root_boot,r.sampled_us,now,r.sequence};
 state_.scope={r.root,r.root_boot,r.mesh_epoch};state_.received_us=at;state_.until_us=at+r.valid_us;
 state_.remote_sampled_us=r.sampled_us;state_.sequence=r.sequence;state_.fields=r.known;state_.values=r.values;
 state_.sats=r.sats;state_.source_kind=r.source_kind;state_.wire_version=bytes[6];state_.hdop_centi=r.hdop_centi;state_.rmc_age_ms=r.rmc_age_ms;
 state_.pps_age_ms=r.pps_age_ms;state_.gga_age_ms=r.gga_age_ms;
 state_.priority1=r.priority1;state_.clock_class=r.clock_class;state_.accuracy=r.accuracy;
 state_.priority2=r.priority2;state_.variance=r.variance;
 state_.reported=true;state_.result=Result::Accepted;count(state_.accepted);return true;
}
}

namespace v12::td_mesh {
Roles RoleMonitor::snapshot(uint64_t)const{return state_;}
Reference ReferenceMonitor::snapshot(uint64_t)const{return state_;}
}
