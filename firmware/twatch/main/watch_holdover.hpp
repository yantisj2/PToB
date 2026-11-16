// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "empirical_holdover.hpp"
#include "g1_ble_eligibility.hpp"
#include "n30_timing_probe.hpp"
namespace watch {
struct ServoView {
 int64_t phase_ns=0;int32_t rate_mppb=INT32_MIN;
 uint32_t rms_ns=UINT32_MAX,max_ns=UINT32_MAX,samples=0,accepted=0,refused=0,predictions=0;
 uint32_t fit_points=0,fit_span_ms=0,original_age_us=UINT32_MAX;
 uint32_t service_left_us=0,corrected_left_us=0,replay_left_us=0,refusal=0;
 uint8_t fit_reason=0;bool trained=false;
};
struct HoldoverStatus {
 HoldoverView view{};
 ServoView servo{};
 uint32_t resets=0,ignored=0,dropped=0,drained=0,work_max_us=0;
 uint32_t generation=0,source=0,train=0,capture_epoch=0;
 uint64_t root=0,parent=0;
 uint8_t queued=0,peak=0;
 const char* reason="WAIT_SOURCE";
};
// Pure observer state, also exercised by the offline production-pair tests.
class HoldoverMonitor {
 struct Scope {
  uint64_t root=0,boot=0,parent=0,parent_boot=0;
  uint32_t source=0,train=0,generation=0,capture_epoch=0;uint8_t depth=0;
  bool same(const Scope& other)const{return root==other.root&&boot==other.boot&&parent==other.parent&&
   parent_boot==other.parent_boot&&source==other.source&&train==other.train&&generation==other.generation&&capture_epoch==other.capture_epoch&&depth==other.depth;}
 } scope_{};
 EmpiricalHoldover horizon_;
 ServoView servo_{};
 uint64_t evaluated_=0,pause_since_us_=0;
 uint32_t resets_=0,ignored_=0;
 bool bound_=false,live_=false,observing_=false;
 const char* reason_="WAIT_SOURCE";
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void clear(){horizon_.clear();bound_=live_=observing_=false;pause_since_us_=0;scope_={};inc(resets_);}
 public:
 HoldoverStatus status()const{
  HoldoverStatus status;status.view=horizon_.view(evaluated_);status.resets=resets_;status.ignored=ignored_;status.servo=servo_;
  if(!live_)status.view.ready=false;
  status.root=scope_.root;status.parent=scope_.parent;status.source=scope_.source;
  status.train=scope_.train;status.generation=scope_.generation;status.capture_epoch=scope_.capture_epoch;
  status.reason=live_?(status.view.ready?(status.view.duration_ms?"READY":"OVER_1US"):"WAIT_SAMPLES"):reason_;
  return status;
 }
 HoldoverView update(const v12::R2Status& s,uint64_t now,bool parked){
  const auto left=[now](uint64_t until){return uint32_t(std::min<uint64_t>(UINT32_MAX,until>now?until-now:0));};
  const auto at20=s.g1_relative_quality.evaluated20;
  servo_={};servo_.trained=s.g1_frequency_trained;servo_.phase_ns=s.g1_clock_model.phase_remaining(at20);
  if(std::isfinite(s.g1_clock_model.rate)&&std::abs(s.g1_clock_model.rate)<=50000)
   servo_.rate_mppb=int32_t(std::llround(s.g1_clock_model.rate*1000));
  servo_.rms_ns=s.g1_relative_quality.rms_ns;servo_.max_ns=s.g1_relative_quality.max_ns;servo_.samples=s.g1_relative_quality.samples;
  servo_.accepted=s.g1_corrected_accepted;servo_.refused=s.g1_corrected_refused;servo_.predictions=s.g1_clock_predictions;
  servo_.fit_points=s.g1_fit_points;servo_.fit_span_ms=s.g1_fit_span_ms;servo_.fit_reason=s.g1_fit_reason;
  if(s.g1_corrected_original20&&at20>=s.g1_corrected_original20)
   servo_.original_age_us=uint32_t(std::min<uint64_t>(UINT32_MAX,(at20-s.g1_corrected_original20)/20));
  servo_.service_left_us=left(s.service_until_us);servo_.corrected_left_us=left(s.g1_corrected_until_us);servo_.replay_left_us=left(s.g1_anchor_deadline_us);
  servo_.refusal=ptob_ble::ble_lock_refusal(s,now);
  const Scope next{s.root_identity,s.root_boot,s.rc7_bound?s.joined_peer:0,s.rc7_bound?s.joined_boot:0,
   s.source_epoch,s.publication,s.g1_relative_quality.scope_generation,s.rx_capture.epoch,uint8_t(s.rc7_bound?s.rc7_depth:0)};
  if(!s.requested||s.fault||s.rx_capture.fault||parked){
   if(bound_)clear();
   reason_=s.fault||s.rx_capture.fault?"FAULT":"BLE_OFF";return status().view;
  }
  // A new receiver/clock incarnation cannot validate an old frozen trial,
  // including while the public snapshot has withdrawn its source fields.
  if(bound_&&((s.rx_capture.epoch&&s.rx_capture.epoch!=scope_.capture_epoch)||
     (next.generation&&next.generation!=scope_.generation))){clear();reason_="SOURCE_SCOPE";}
  const auto refusal=ptob_ble::ble_lock_refusal(s,now)&~uint32_t(ptob_ble::BleQuality|ptob_ble::BleForecast);
  if(refusal||!next.root||!next.boot||!next.source||!next.train||!next.generation||!next.capture_epoch){
   live_=false;
   constexpr uint32_t pause_mask=ptob_ble::BleNotFollowing|ptob_ble::BleCoarseExpiry|
    ptob_ble::BleCorrectedExpiry|ptob_ble::BleRetained|ptob_ble::BleContext;
   const bool short_pause=bound_&&refusal&&!(refusal&~pause_mask)&&
    s.g1_clock_origin==scope_.root&&s.g1_clock_model.tick&&
    (!next.root||(next.root==scope_.root&&next.boot==scope_.boot&&next.source==scope_.source&&next.train==scope_.train));
   if(short_pause){
    if(!pause_since_us_)pause_since_us_=now;
    if(now>=pause_since_us_&&now-pause_since_us_<500000){reason_="SOURCE_PAUSE";return status().view;}
   }
   horizon_.cancel();observing_=false;reason_=refusal?ptob_ble::ble_lock_reason(refusal):"NO_CONTEXT";
   return status().view;
  }
  if(!bound_||!scope_.same(next)){clear();scope_=next;bound_=true;}
  pause_since_us_=0;observing_=true;
  const auto at=s.g1_relative_quality.evaluated20;
  if(!at||at<s.g1_clock_model.tick||(evaluated_&&at<evaluated_)){
   clear();evaluated_=0;reason_="CLOCK_DOMAIN";return status().view;
  }
  evaluated_=at;horizon_.tick(at);
  live_=s.g1_frequency_trained&&s.g1_clock_model.tick&&s.g1_relative_quality.samples&&
   s.g1_relative_quality.rms_ns<1000;
  // A quality dip stops new starts. Existing frozen clocks must still see
  // later physical failures; cancelling them here would censor those tails.
  if(!live_)reason_=s.g1_frequency_trained?"RAW_QUALITY":"TRAINING";
  else if(std::abs(double(s.g1_clock_model.phase_remaining(at)))>=1000){live_=false;reason_="PHASE_SETTLING";}
  else horizon_.start(s.g1_clock_model,at);
  return status().view;
 }
 void pair(const v12::n30_probe::Pair& pair){
  if(!bound_||!observing_){inc(ignored_);return;}
  if(pair.root!=scope_.root||pair.boot!=scope_.boot||pair.source!=scope_.source||pair.train!=scope_.train){
   clear();reason_="SOURCE_SCOPE";inc(ignored_);return;
  }
  if(pair.epoch!=scope_.capture_epoch){inc(ignored_);return;}
  if(!pair.local20||pair.available20<pair.local20||pair.available20-pair.local20>5000000||
     !pair.remote_ns||pair.remote_ns<pair.created_ns||pair.remote_ns>=pair.expires_ns){inc(ignored_);return;}
  horizon_.observe(pair.local20,pair.remote_ns,pair.available20);
 }
 void missing(){horizon_.cancel();live_=observing_=false;reason_="PAIR_OVERFLOW";}
};
void holdover_init();
HoldoverView holdover_update(const v12::R2Status&,uint64_t now,bool parked);
HoldoverStatus holdover_status();
}
