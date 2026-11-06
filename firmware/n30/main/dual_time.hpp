// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ui.hpp"
#include <algorithm>
#include <cstring>
namespace n30 {
inline bool lora_time(const n30_lora_view& l,uint64_t now,uint64_t& utc){
 if(!l.requested||!l.active||l.fault||l.state!=2||!l.utc_us||now<l.sampled_us||now-l.sampled_us>=SnapshotMaxUs||now>=l.until_us||now-l.sampled_us>UINT64_MAX-l.utc_us)return false;
 utc=l.utc_us+now-l.sampled_us;return true;
}
// Human presentation only. The fresh worker snapshot is not a renewed source
// lease; this path never participates in synchronized_until or comparison.
inline bool lora_watch_time(const n30_lora_view& l,uint64_t now,uint64_t& utc){
 if(!l.requested||!l.active||l.fault||!l.watch_valid||!l.watch_utc_us||now<l.watch_sampled_us||now-l.watch_sampled_us>=SnapshotMaxUs||now-l.watch_sampled_us>UINT64_MAX-l.watch_utc_us)return false;
 utc=l.watch_utc_us+now-l.watch_sampled_us;return true;
}
inline bool ble_time(const Model& m,uint64_t now,uint64_t& utc){
 const auto& b=m.ble;if(!current(m,now)||!b.valid||now<b.at_us||now>=b.until_us||now-b.at_us>UINT64_MAX-b.utc_us)return false;
 utc=b.utc_us+now-b.at_us;return true;
}
// The return dwell consumes qualified BLE time in one admitted scope. A brief
// permission-only gap pauses that evidence; it never authorizes output. A new
// scope, quality loss, stale observer or longer gap starts a new dwell.
class ClockSelection {
 public:
 static constexpr unsigned Capacity=8;
 static constexpr uint64_t DwellUs=30000000;
 static constexpr uint64_t PermissionGapUs=1500000;
 enum Reason:uint8_t {Initial=1,Refused,Scope,ObserverGap,ClockBack,Qualified,Selection};
 struct Event {
  uint64_t at=0,root=0,boot=0,origin=0,since=0;
  uint32_t sequence=0,epoch=0,publication=0,quality_scope=0,refusal=0;
  uint8_t reason=0;bool qualified=false,lora=false;
 };
 struct View {
  uint64_t observed=0,since=0,max_gap=0,root=0,boot=0,origin=0,permission_gap=0;
  uint32_t epoch=0,publication=0,quality_scope=0,refusal=0,sequence=0,overwritten=0;
  uint32_t losses=0,scopes=0,gaps=0,backs=0,remaining_us=0;
  bool qualified=false,lora=false,presentation=false,external=false;
  Event first_problem{};
 };
 private:
 uint64_t since_=0,last_=0,root_=0,boot_=0,origin_=0,max_gap_=0,pause_since_=0;
 uint32_t epoch_=0,publication_=0,quality_scope_=0,refusal_=0,sequence_=0;
 uint32_t losses_=0,scopes_=0,gaps_=0,backs_=0;
 bool active_=false,lora_selected_=false,external_=false,presentation_=false;
 Event events_[Capacity]{},first_problem_{};
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void record(Reason reason,uint64_t now){
  if(sequence_==UINT32_MAX)return;
  ++sequence_;events_[(sequence_-1)%Capacity]={now,root_,boot_,origin_,since_,sequence_,epoch_,publication_,quality_scope_,refusal_,uint8_t(reason),active_,lora_selected_};
 }
 void update(bool qualified,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t origin,
             uint32_t publication,uint32_t quality,uint64_t now,uint32_t refusal){
  const bool backwards=last_&&now<last_;
  const uint64_t gap=last_&&now>=last_?now-last_:0;
  if(gap>max_gap_)max_gap_=gap;
  const bool different=root_!=root||boot_!=boot||epoch_!=epoch||origin_!=origin||publication_!=publication||quality_scope_!=quality;
  const bool interrupted=last_&&gap>=SnapshotMaxUs;
  // R2 withdraws the public scope at signed permission expiry. Remember the
  // last qualified scope only as return evidence, and require an exact match
  // when current authorized quality comes back. OFF/fault/drain/replay/quality
  // refusals cannot enter this path. The gap is finite even with1ms polling.
  constexpr uint32_t PermissionMask=8|16|64|128;
  const bool compatible=(!root||root==root_)&&(!boot||boot==boot_)&&(!epoch||epoch==epoch_)&&
    (!origin||origin==origin_)&&(!publication||publication==publication_)&&(!quality||quality==quality_scope_);
  const bool permission_only=!qualified&&(refusal&(64|128))&&!(refusal&~PermissionMask);
  const bool may_pause=permission_only&&(active_||pause_since_)&&compatible&&!backwards&&!interrupted;
  if(may_pause&&!pause_since_)pause_since_=last_;
  const bool paused=may_pause&&now>=pause_since_&&now-pause_since_<PermissionGapUs;
  const bool changed=different&&!paused;
  if(active_&&!qualified)inc(losses_);
  Reason reason=Initial;bool event=!last_;
  if(backwards){inc(backs_);reason=ClockBack;event=true;}
  else if(interrupted){inc(gaps_);reason=ObserverGap;event=true;}
  else if(changed){inc(scopes_);reason=Scope;event=true;}
  else if(!qualified&&(active_||refusal_!=refusal)){reason=Refused;event=true;}
  else if(qualified&&!active_){reason=Qualified;event=true;}
  const bool first_problem=!first_problem_.sequence&&active_&&lora_selected_&&(backwards||interrupted||changed||!qualified);
  const Event previous{now,root_,boot_,origin_,since_,0,epoch_,publication_,quality_scope_,refusal,uint8_t(reason),false,lora_selected_};
  if(!qualified)active_=false;
  else{
   if(pause_since_&&!changed&&!backwards&&!interrupted&&now>=pause_since_&&now-pause_since_<PermissionGapUs)
    since_+=now-pause_since_; // Missing time earns no qualification credit.
   else if(!active_||backwards||interrupted||changed)since_=now;
   active_=true;
  }
  if(!paused){pause_since_=0;root_=root;boot_=boot;epoch_=epoch;origin_=origin;publication_=publication;quality_scope_=quality;}
  last_=now;refusal_=refusal;
  if(event)record(reason,now);
  if(first_problem){first_problem_=previous;first_problem_.sequence=sequence_;}
 }
 public:
 // Main-owner source observations are independent of display publication.
 // These are scheduling evidence only; select() still checks current output.
 void observe(bool qualified,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t origin,
              uint32_t publication,uint32_t quality,uint64_t now,uint32_t refusal=0){
  external_=true;update(qualified,root,boot,epoch,origin,publication,quality,now,refusal);
 }
 View view(uint64_t now)const{
  const bool fresh=last_&&now>=last_&&now-last_<SnapshotMaxUs;
  const auto elapsed=active_&&now>=since_?now-since_:0;
  return {last_,since_,max_gap_,root_,boot_,origin_,pause_since_&&now>=pause_since_?now-pause_since_:0,epoch_,publication_,quality_scope_,refusal_,sequence_,sequence_>Capacity?sequence_-Capacity:0,
    losses_,scopes_,gaps_,backs_,uint32_t(active_&&fresh?(elapsed>=DwellUs?0:DwellUs-elapsed):DwellUs),active_&&fresh,lora_selected_,presentation_,external_,first_problem_};
 }
 bool next(uint32_t after,Event& out)const{
  if(after>=sequence_)return false;
  const auto oldest=sequence_>Capacity?sequence_-Capacity+1:1;
  const auto next=after<oldest?oldest:after+1;out=events_[(next-1)%Capacity];return out.sequence==next;
 }
 void select(Model& m,const n30_lora_view& l,uint64_t now,bool authorized_quality=true){
  m.lora=l;uint64_t ignored;
  bool qualified=authorized_quality&&!m.mode_pending&&m.locked&&m.state==State::Follow&&
   (m.source==Source::Ble||m.source==Source::Rc1||m.source==Source::Rc2)&&
   m.ble.valid&&m.ble.following&&m.ble.root&&m.ble.boot&&m.ble.epoch&&m.precision.valid&&m.precision.scope&&
   analog_time(m,now,ignored);
  presentation_=qualified;
  if(!external_)update(qualified,m.ble.root,m.ble.boot,m.ble.epoch,m.precision.scope,0,m.precision.quality_scope,now,qualified?0:1);
  const bool dwell_ready=view(now).qualified&&root_==m.ble.root&&boot_==m.ble.boot&&epoch_==m.ble.epoch&&
    origin_==m.precision.scope&&quality_scope_==m.precision.quality_scope&&now>=since_&&now-since_>=DwellUs;
  const bool old=lora_selected_,live_lora=lora_time(l,now,ignored);
  if(qualified&&(!lora_selected_||!live_lora||dwell_ready))lora_selected_=false;
  else if(live_lora){lora_selected_=true;apply(m,l);}
  else if((lora_selected_||!time(m,now,ignored))&&lora_watch_time(l,now,ignored))apply_watch(m,l);
  if(old!=lora_selected_)record(Selection,now);
 }
 static void apply(Model& m,const n30_lora_view& l){
  m.utc_at_us=l.sampled_us;m.utc_us=l.utc_us;m.until_us=l.until_us;
  m.valid=true;m.locked=l.age_ms<35000;m.state=m.locked?State::Follow:State::Hold;m.source=Source::LoRa;m.hz=0;m.serving=false;m.delivery=UINT16_MAX;m.rms_ns=l.watch_valid?l.watch_prediction_n?l.watch_rms_ns:UINT32_MAX:l.model_kind?l.prediction_recent_rms_ns:l.link_samples>=4?l.link_rms_ns:UINT32_MAX;
 }
 static void apply_watch(Model& m,const n30_lora_view& l){
  m.utc_at_us=l.watch_sampled_us;m.utc_us=l.watch_utc_us;
  m.until_us=l.watch_sampled_us>UINT64_MAX-SnapshotMaxUs?UINT64_MAX:l.watch_sampled_us+SnapshotMaxUs;
  m.valid=true;m.locked=false;m.state=l.watch_age_ms>=35000?State::Hold:State::Acquire;m.source=Source::LoRa;m.hz=0;
  m.serving=false;m.delivery=UINT16_MAX;m.rms_ns=l.watch_prediction_n?l.watch_rms_ns:UINT32_MAX;m.precision={};
 }
};
static_assert(sizeof(ClockSelection)<=1024,"Bounded dual-source state journal");
// Stateless helper retained for synthetic fixtures and diagnostic compositions.
inline void select_lora(Model& m,const n30_lora_view& l,uint64_t now){
 m.lora=l;uint64_t ignored;
 if(!m.mode_pending&&analog_time(m,now,ignored))return;
 if(lora_time(l,now,ignored))ClockSelection::apply(m,l);
 else if(!time(m,now,ignored)&&lora_watch_time(l,now,ignored))ClockSelection::apply_watch(m,l);
}
class DualComparison {
 uint8_t context_[16]{};uint64_t at_=0,epoch_=0;uint32_t sequence_=0,generation_=0;int64_t delta_=0;
 bool seen_=false;
 public:
 void update(Model& m,uint64_t now){
  m.delta_valid=m.drift_valid=false;uint64_t ble,lora;
  const auto& l=m.lora;const auto& b=m.ble;
  if(!ble_time(m,now,ble)||!lora_time(l,now,lora)||!b.following||!l.metadata_known||!b.root||b.root!=l.root||!b.boot||b.boot!=l.root_boot||!b.epoch||b.epoch!=l.source_epoch){seen_=false;return;}
  const bool positive=ble>=lora;const uint64_t difference=positive?ble-lora:lora-ble;
  if(difference>uint64_t(INT64_MAX)/1000-1){seen_=false;return;}
  const int64_t delta=(positive?int64_t(difference)*1000:-int64_t(difference)*1000)-int64_t(l.event_ns%1000);
  m.delta_ns=delta;m.delta_valid=true;
  const bool same=seen_&&generation_==l.generation&&epoch_==l.capture_epoch&&!std::memcmp(context_,l.context,16)&&now>=at_;
  if(same&&sequence_==l.corrected_sequence){m.drift_valid=drift_valid_;m.drift_ppb=drift_;return;}
  drift_valid_=false;
  if(same&&l.corrected_sequence>sequence_&&now-at_>=1000000&&now-at_<120000000){
   const long double rate=(static_cast<long double>(delta)-delta_)*1000000/(now-at_);
   if(rate>=INT32_MIN&&rate<=INT32_MAX){drift_=int32_t(rate);drift_valid_=true;}
  }
  std::memcpy(context_,l.context,16);at_=now;epoch_=l.capture_epoch;generation_=l.generation;sequence_=l.corrected_sequence;delta_=delta;seen_=true;
  m.drift_ppb=drift_;m.drift_valid=drift_valid_;
 }
 private:bool drift_valid_=false;int32_t drift_=0;
};
}
