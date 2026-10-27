// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "td_mesh.hpp"
extern "C" {
#include "gm7/gm7_codec.h"
}
namespace v12::td_mesh {
namespace {
void count(uint32_t& n){if(n!=UINT32_MAX)++n;}
bool matches(Scope a,uint64_t root,uint64_t boot,uint32_t epoch){return a.root==root&&a.boot==boot&&a.epoch==epoch;}
}
bool Gm7Monitor::receive(const uint8_t* bytes,size_t size,const r2_group_config& group,uint64_t advertiser,
 Scope selected,uint64_t at,uint64_t now){
 gm7_packet p{};
 if(gm7_ad_decode(&p,bytes,size,&group)){count(roles_.rejected_auth);roles_.result=Result::Auth;return false;}
 uint64_t sender,boot,sequence,sampled,birth,founder,term,active,active_boot;uint32_t epoch,valid;
 const unsigned channel=p.kind<=GM7_WITHDRAW?0:p.kind==GM7_ROLE?1:2;
 if(!channel){const auto& c=p.body.control;sender=c.sender;boot=c.boot;sequence=c.sequence;sampled=c.sent_mono_us;
  birth=c.birth_us;founder=c.founder;term=c.term;active=c.active;active_boot=c.active_boot;epoch=c.timing_epoch;valid=c.max_age_us;
 }else if(channel==1){const auto& r=p.body.role;sender=r.sender;boot=r.boot;sequence=r.sequence;sampled=r.sampled_mono_us;
  birth=r.birth_us;founder=r.founder;term=r.term;active=r.active;active_boot=r.active_boot;epoch=r.timing_epoch;valid=r.valid_us;
 }else{const auto& g=p.body.gnss;sender=g.sender;boot=g.boot;sequence=g.sequence;sampled=g.sampled_mono_us;
  birth=g.birth_us;founder=g.founder;term=g.term;active=g.active;active_boot=g.active_boot;epoch=g.timing_epoch;valid=g.valid_us;
 }
 const bool selected_any=selected.root||selected.boot||selected.epoch;
 const bool primary=sender==active&&boot==active_boot;
 const bool peer_report=p.kind==GM7_STATUS||(p.kind==GM7_ROLE&&!primary);
 const bool leader_report=p.kind==GM7_ANNOUNCE||(p.kind==GM7_ROLE&&p.body.role.role==GM7_W_ACTIVE&&primary);
 const bool same=roles_.known&&roles_.root==active&&roles_.root_boot==active_boot&&roles_.epoch==epoch&&
  roles_.birth_utc_us==birth&&roles_.founder==founder&&roles_.term==term;
 // An empty timing selection may observe a reported leader, but it cannot
 // turn ANN/ROLE into admitted UTC or fine timing. Existing selection wins.
 if(advertiser!=sender||(p.kind==GM7_ROLE&&p.body.role.role==GM7_W_ACTIVE&&!primary)||
    (!peer_report&&(!birth||!founder||!term||!active||!active_boot||!epoch))||
    (!peer_report&&selected_any&&!matches(selected,active,active_boot,epoch))||
    (!peer_report&&!leader_report&&roles_.known&&!same)||
    (!peer_report&&roles_.known&&birth==roles_.birth_utc_us&&founder==roles_.founder&&term<roles_.term)||
    (!peer_report&&!selected_any&&!same&&!leader_report)||
    (!peer_report&&!selected_any&&roles_.known&&!same&&
     (birth>roles_.birth_utc_us||(birth==roles_.birth_utc_us&&founder>roles_.founder)||
      (birth==roles_.birth_utc_us&&founder==roles_.founder&&term<roles_.term)))||
    (p.kind==GM7_GNSS&&!primary)){
  count(roles_.rejected_scope);roles_.result=Result::Scope;return false;
 }
 if(now<at||now-at>=valid||at>UINT64_MAX-valid){count(roles_.rejected_age);roles_.result=Result::Expired;return false;}
 Peer* peer=nullptr;unsigned replace=0;
 for(unsigned i=0;i<8;++i){
  if(peers_[i].id==sender&&peers_[i].boot==boot){peer=&peers_[i];break;}
  if(!peers_[i].id||peers_[i].seen<peers_[replace].seen)replace=i;
 }
 if(peer&&(sequence<=peer->sequence[channel]||sampled<peer->sampled[channel])){
  count(roles_.replayed);roles_.result=Result::Replay;return false;
 }
 if(!peer){peer=&peers_[replace];*peer={};peer->id=sender;peer->boot=boot;}
 peer->seen=now;peer->sequence[channel]=sequence;peer->sampled[channel]=sampled;
 roles_.wire_version=3;roles_.result=Result::Accepted;count(roles_.accepted);
 roles_.last_peer=sender;roles_.last_boot=boot;roles_.last_sequence=sequence;roles_.last_kind=p.kind;roles_.last_flags=uint8_t(p.flags);
 roles_.last_role=p.kind==GM7_ROLE?p.body.role.role:0;roles_.last_blockers=p.kind==GM7_ROLE?p.body.role.blockers:0;
 const auto hot_report=[&](uint8_t role){
  // STATUS and ROLE have separate sequence spaces but the same sender boot
  // clock. A late older observation cannot undo the newer reported state.
  if(roles_.hot_report_known&&sampled<roles_.hot_remote_sampled_us)return;
  roles_.hot_report_known=true;roles_.hot_report_kind=p.kind;roles_.hot_role=role;
  roles_.hot_received_us=at;roles_.hot_remote_sampled_us=sampled;roles_.hot_until_us=at+valid;
  roles_.hot_confirmed=role==GM7_W_HOT;
 };
 if(p.kind==GM7_GNSS){
  const auto& g=p.body.gnss;reference_.scope={active,active_boot,epoch};reference_.received_us=at;reference_.until_us=at+valid;
  reference_.sequence=sequence;reference_.remote_sampled_us=sampled;reference_.wire_version=3;
  reference_.fields=uint8_t(g.known&63);reference_.values=uint8_t(g.values);reference_.sats=g.sats;
  reference_.source_kind=1;reference_.hdop_centi=g.hdop_centi;reference_.rmc_age_ms=g.rmc_age_ms;
  reference_.pps_age_ms=g.pps_age_ms;reference_.gga_age_ms=g.gga_age_ms;
  reference_.priority1=g.priority1;reference_.clock_class=g.clock_class;reference_.accuracy=g.accuracy;
  reference_.priority2=g.priority2;reference_.variance=g.variance;reference_.reported=true;
  reference_.utc_error_us=g.utc_error_us;
  reference_.result=Result::Accepted;count(reference_.accepted);return true;
 }
 if(p.kind==GM7_STATUS){
  count(roles_.statuses);
  if(sender==roles_.hot&&boot==roles_.hot_boot){
   const bool hot=same&&(p.flags&GM7_HOT)!=0&&roles_.train_gen&&p.body.control.train_gen==roles_.train_gen;
   if(hot)hot_report(GM7_W_HOT);
   else if((same||!birth)&&!(p.flags&GM7_HOT))hot_report((p.flags&GM7_CAPABLE)?((p.flags&GM7_SHADOW)?GM7_W_PREPARING:GM7_W_STANDBY):GM7_W_RC);
  }
  return true;
 }
 if(p.kind==GM7_ROLE&&!primary){
  count(roles_.role_reports);
  if(sender==roles_.hot&&boot==roles_.hot_boot&&(same||!birth)){
   const auto& role=p.body.role;
   if(role.role!=GM7_W_HOT||(same&&role.train_gen==roles_.train_gen&&role.hot==sender&&role.hot_boot==boot))hot_report(role.role);
  }
  return true;
 }
 if(!leader_report&&!(p.kind==GM7_WITHDRAW&&same))return true;
 if(!same){
  const bool same_identity=roles_.known&&roles_.root==active&&roles_.root_boot==active_boot;
  if(!same_identity)roles_.primary_since_us=at;
  if(!roles_.known||roles_.birth_utc_us!=birth||roles_.founder!=founder)roles_.epoch_since_us=at;
  roles_.hot_until_us=roles_.hot_received_us=roles_.hot_remote_sampled_us=0;
  roles_.hot_report_known=roles_.hot_confirmed=false;
  roles_.utc_sample_us=roles_.utc_received_us=roles_.created_us=0;count(roles_.changes);
 }
 roles_.known=true;roles_.root=active;roles_.root_boot=active_boot;roles_.epoch=epoch;
 roles_.birth_utc_us=birth;roles_.founder=founder;roles_.term=term;roles_.sequence=sequence;
 roles_.received_us=at;roles_.until_us=at+valid;roles_.kind=p.kind;roles_.flags=uint8_t(p.flags);
 roles_.withdrawn=p.kind==GM7_WITHDRAW;roles_.coordinator=0;roles_.members=0;
 if(p.kind==GM7_WITHDRAW){
  count(roles_.withdrawals);roles_.until_us=at;roles_.hot_confirmed=roles_.hot_report_known=false;
  roles_.hot=roles_.hot_boot=roles_.hot_since_us=0;
 }else{
  const auto hot=p.kind==GM7_ANNOUNCE?p.body.control.hot:p.body.role.hot;
  const auto hot_boot=p.kind==GM7_ANNOUNCE?p.body.control.hot_boot:p.body.role.hot_boot;
  if(roles_.hot!=hot||roles_.hot_boot!=hot_boot){roles_.hot_since_us=hot?at:0;roles_.hot_confirmed=roles_.hot_report_known=false;roles_.hot_until_us=roles_.hot_received_us=roles_.hot_remote_sampled_us=0;count(roles_.changes);}
  roles_.hot=hot;roles_.hot_boot=hot_boot;
  const auto train=p.kind==GM7_ANNOUNCE?p.body.control.train_gen:p.body.role.train_gen;
  if(roles_.train_gen!=train){roles_.hot_confirmed=roles_.hot_report_known=false;roles_.hot_until_us=roles_.hot_received_us=roles_.hot_remote_sampled_us=0;}
  roles_.train_gen=train;
  if(p.kind==GM7_ANNOUNCE){
   count(roles_.announcements);roles_.created_us=sampled;
   roles_.utc_sample_us=p.body.control.sent_utc_us;roles_.utc_received_us=at;roles_.reported_role=GM7_W_ACTIVE;
  }else{count(roles_.role_reports);roles_.reported_role=p.body.role.role;roles_.blockers=p.body.role.blockers;}
 }
 return true;
}
Roles Gm7Monitor::roles(uint64_t now)const{
 auto out=roles_;if(now<out.received_us||now>=out.hot_until_us)out.hot_confirmed=false;return out;
}
Reference Gm7Monitor::reference(uint64_t)const{return reference_;}
}
