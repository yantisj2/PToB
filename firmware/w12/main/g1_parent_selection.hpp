#pragma once
#include <cstdint>
#include "g1_parent_history.hpp"
#include "g1_recovery_budget.hpp"
extern "C" {
#include "r2/r2-g1/link_quality.h"
}
namespace v12 {
// Bounded advisory scoring. Discovery is a locator only; the controller owner
// authenticates a fresh admission. No score changes clock/source authority.
class G1ParentSelection {
 public:
  static constexpr uint64_t DwellUs=10000000,FreshUs=200000,CollectUs=40000,StartupCollectUs=3000000;
  static constexpr uint64_t StartupRelayWaitUs=15000000,StartupScanUs=3000000,StartupGmObserveUs=2000000;
  static constexpr int ActiveRelayRssiMargin=4;
  static constexpr uint64_t PendingRelayUs=5000000;
  static constexpr uint64_t GmRecoveryUs=3000000;
  static constexpr uint64_t LockDwellUs=60000000;
  static constexpr uint64_t LockConfirmUs=2000000,LockSampleGapUs=250000;
  enum class Decision:uint8_t{Collecting,NoCandidate,Best,Dwell,Margin,Incumbent,ReservedLock,Admission,RecoverGM,ReturnGM,HoldGM,LockTrial};
  struct SelectionView {uint8_t entries=0;int16_t current_score=INT16_MIN,best_score=INT16_MIN;Decision reason=Decision::Collecting;};
  struct Candidate {
    uint64_t identity=0,seen=0,consumed=0,cooldown=0;
    int16_t rssi=-127;bool relay=false,publishing=false;
  };
 private:
  Candidate candidates_[8]{};
  r2_link_quality quality_{};
  G1ParentHistory history_{};
#ifdef PTOB_N30
  G1RecoveryBudget lock_budget_;
#endif
  uint64_t collecting_=0,discovery_since_=0,parent_=0,renew_at_=0;
  uint64_t scan_at_=0,scan_owned_=0,target_=0;
  uint64_t startup_scan_owned_=0;
  uint64_t pending_relay_=0,pending_until_=0;
  uint64_t recovering_gm_=0,gm_recovery_until_=0;
  uint64_t bound_at_=0,usable_at_=0,usable_owned_=0,usable_good_=0;
  uint64_t failed_retry_us_=0,retry_after_=0;
  uint64_t trial_=0,trial_since_=0,locked_at_=0,lock_candidate_since_=0,lock_sample_at_=0;
  int16_t bound_rssi_=-127;
  uint8_t missed_=0;bool scanner_=false,force_relay_=false,cold_poor_=false,target_relay_=true;
  bool parent_relay_=false,usable_value_=false,usable_reported_=false,usable_previous_=false,usable_previous_valid_=false;
  bool startup_=true; // Cleared by the first actual admission; reset/recovery never rearms it.
  bool trial_relay_=false,trial_bound_=false,trial_failed_=false;
  mutable SelectionView selection_{};
  bool relay(uint64_t id)const{for(const auto& c:candidates_)if(c.identity==id)return c.relay;return parent_==id&&parent_relay_;}
  int score(const Candidate& c,uint64_t now)const{return G1ParentHistory::score(history_.find(c.identity,c.relay,now),c.relay,c.rssi,now,history_);}
  void begin_trial(uint64_t id,bool is_relay,uint64_t now){
    if(!id||!now||(trial_==id&&trial_relay_==is_relay))return;
#ifdef PTOB_N30
    lock_budget_.reset(now);
#endif
    trial_=id;trial_relay_=is_relay;trial_since_=now;locked_at_=lock_candidate_since_=lock_sample_at_=0;trial_bound_=trial_failed_=false;retry_after_=0;
  }
 public:
  explicit G1ParentSelection(uint64_t failed_retry_us=0):failed_retry_us_(failed_retry_us){}
  // Retire every locator/current owner while retaining only advisory learning.
  void reset(){for(auto& c:candidates_)c={};quality_={};collecting_=discovery_since_=parent_=renew_at_=0;
    scan_at_=scan_owned_=target_=bound_at_=usable_at_=usable_owned_=usable_good_=startup_scan_owned_=0;missed_=0;
    scanner_=force_relay_=cold_poor_=target_relay_=parent_relay_=false;target_relay_=true;
    pending_relay_=pending_until_=0;
    recovering_gm_=gm_recovery_until_=0;
    trial_=trial_since_=locked_at_=lock_candidate_since_=lock_sample_at_=0;trial_relay_=trial_bound_=trial_failed_=false;retry_after_=0;
#ifdef PTOB_N30
    lock_budget_.reset();
#endif
    bound_rssi_=-127;usable_value_=usable_reported_=usable_previous_=usable_previous_valid_=false;selection_={};}
  // A leader incarnation change invalidates selection learning and failed
  // trials. Operator pins and profile policy survive; cold-start collection
  // is not rearmed after an actual admission.
  void leader_lost(){const auto target=target_;const auto relay=target_relay_;const auto forced=force_relay_;
    reset();clear_history();target_=target;target_relay_=relay;force_relay_=forced;}
  void clear_history(){history_.clear();}
  const G1ParentHistory& history()const{return history_;}
  SelectionView selection_view(uint64_t now)const{auto v=selection_;v.entries=uint8_t(history_.entries(now));return v;}
  void diagnostic(bool unavailable){force_relay_=unavailable;}
  bool diagnostic()const{return force_relay_;}
  void target(uint64_t id,bool relay=true){target_=id;target_relay_=relay;}
  bool pinned()const{return target_!=0;}
  bool accepts(uint64_t id,bool relay)const{return !target_||(relay==target_relay_&&id==target_);}
  bool seeking_relay()const{return target_?target_relay_:force_relay_||cold_poor_||quality_.view.poor||needs_alternative();}
  bool needs_alternative()const{return !target_&&!force_relay_&&trial_&&trial_failed_;}
  bool deferred(uint64_t id,bool relay,uint64_t now)const{return history_.lock_deferred(id,relay,now);}
  bool retry_allowed(uint64_t id,bool is_relay,uint64_t now)const{
    return !failed_retry_us_||target_||force_relay_||!trial_failed_||
      id!=trial_||is_relay!=trial_relay_||now>=retry_after_;
  }
  void attempted(uint64_t id,bool is_relay,uint64_t now){
    if(!retry_allowed(id,is_relay,now))return;
    if(failed_retry_us_&&trial_failed_&&id==trial_&&is_relay==trial_relay_)
      retry_after_=now>UINT64_MAX-failed_retry_us_?UINT64_MAX:now+failed_retry_us_;
    // Probing a relay must not make the old failed GM protected again. Only
    // a successful new binding replaces an existing parent's lock trial.
    if(!parent_)begin_trial(id,is_relay,now);
  }
  // W12 requires two observed seconds of raw relative lock plus its existing
  // one-second hold gate. N30's separate loss budget below requires sustained
  // recovery. FOLLOW/packets and renewals never establish relative lock.
  void locked(bool value,uint64_t now){
    if(!trial_||!trial_since_)return;
    if(now<trial_since_||(locked_at_&&now<locked_at_)){
#ifdef PTOB_N30
      lock_budget_.reset(now);
#endif
      trial_since_=now;locked_at_=lock_candidate_since_=lock_sample_at_=0;trial_failed_=false;retry_after_=0;return;
    }
#ifdef PTOB_N30
    // N30's weak links can briefly pass raw lock for two seconds while
    // spending minutes in HOLD. Count all observed loss until a full minute
    // of uninterrupted lock recovers the parent; pulses cannot renew its trial.
    if(failed_retry_us_){
      lock_budget_.observe(value&&parent_==trial_,now,LockDwellUs,LockSampleGapUs);
      if(lock_budget_.stable(now,LockDwellUs)){
        locked_at_=now;trial_failed_=false;retry_after_=0;history_.locked(trial_,trial_relay_,now);return;
      }
      if(trial_failed_||lock_budget_.lost()<LockDwellUs)return;
      trial_failed_=true;retry_after_=now>UINT64_MAX-failed_retry_us_?UINT64_MAX:now+failed_retry_us_;
      history_.lock_failed(trial_,trial_relay_,now);return;
    }
#endif
    if(value&&parent_==trial_){
      if(!lock_sample_at_||now<lock_sample_at_||now-lock_sample_at_>LockSampleGapUs)lock_candidate_since_=now;
      lock_sample_at_=now;
      if(now-lock_candidate_since_>=LockConfirmUs){
        locked_at_=now;trial_failed_=false;retry_after_=0;history_.locked(trial_,trial_relay_,now);return;
      }
    }else lock_candidate_since_=lock_sample_at_=0;
    const auto since=locked_at_?locked_at_:trial_since_;
    if(!trial_failed_&&now-since>=LockDwellUs){
      trial_failed_=true;
      // Leave a full admission-budget window for alternatives. Otherwise
      // direct retries can keep the shared relay rate gate closed forever.
      retry_after_=now>UINT64_MAX-failed_retry_us_?UINT64_MAX:now+failed_retry_us_;
      history_.lock_failed(trial_,trial_relay_,now);
    }
  }
  uint64_t parent()const{return parent_;}
  const r2_link_quality_view& quality()const{return quality_.view;}
  void seen(uint64_t id,bool relay,int8_t rssi,uint64_t now,bool publishing=false){
    if(!id||!now||rssi>0||rssi<-127)return;
    unsigned slot=8;
    for(unsigned i=0;i<8;i++)if(candidates_[i].identity==id){slot=i;break;}
    if(slot==8){slot=0;for(unsigned i=0;i<8;i++){
      if(!candidates_[i].identity){slot=i;break;}
      if(candidates_[i].seen<candidates_[slot].seen)slot=i;
    }candidates_[slot]={};candidates_[slot].identity=id;}
    auto& c=candidates_[slot];c.rssi=c.seen?int16_t((3*c.rssi+rssi)/4):rssi;
    c.seen=now;c.relay=relay;c.publishing=relay&&publishing;if(!collecting_)collecting_=now;
    if(parent_==id&&parent_relay_==relay)bound_rssi_=c.rssi;
  }
  void cooldown(uint64_t id,uint64_t now,uint64_t duration){
    for(auto& c:candidates_)if(c.identity==id){
      c.cooldown=now>UINT64_MAX-duration?UINT64_MAX:now+duration;c.consumed=c.seen;break;
    }
  }
  void bind(uint64_t id,uint64_t boot,uint64_t epoch,uint64_t now){
    if(!id||!boot||!epoch)return;
    const bool changed=parent_!=id||quality_.parent!=id||quality_.boot!=boot||quality_.epoch!=epoch;
    parent_relay_=relay(id);
    begin_trial(id,parent_relay_,now);
    if(!trial_bound_){trial_since_=now;trial_bound_=true;}
    for(const auto& c:candidates_)if(c.identity==id&&c.relay==parent_relay_){bound_rssi_=c.rssi;break;}
    parent_=id;renew_at_=now>UINT64_MAX-300000000?UINT64_MAX:now+300000000;
    pending_relay_=pending_until_=0;
    recovering_gm_=gm_recovery_until_=0;
    startup_=false;
    r2_link_quality_bind(&quality_,id,boot,epoch);
    if(changed){bound_at_=now;cold_poor_=false;missed_=0;discovery_since_=0;lock_candidate_since_=lock_sample_at_=0;
      usable_owned_=usable_good_=0;usable_reported_=usable_previous_valid_=false;}
  }
  void authenticated(uint64_t id,bool is_relay,uint64_t boot,uint64_t root,uint64_t root_boot,uint64_t source_epoch,uint64_t now){
    if(!id||!boot||!root||!root_boot||!source_epoch)return;
    // A compact non-security context tag; never used for equality/authority.
    uint64_t tag=UINT64_C(14695981039346656037);
    const uint64_t fields[4]={boot,root,root_boot,source_epoch};
    for(auto v:fields)for(unsigned i=0;i<8;i++){tag^=uint8_t(v>>(8*i));tag*=UINT64_C(1099511628211);}
    history_.authenticated(id,is_relay,tag,now);if(parent_==id)parent_relay_=is_relay;
  }
  void pending(uint64_t id,uint64_t now){
    if(!id||pending_relay_==id)return; // Repeated PENDING never restarts the bound.
    pending_relay_=id;pending_until_=now>UINT64_MAX-PendingRelayUs?UINT64_MAX:now+PendingRelayUs;
  }
  void failed(uint64_t id,bool is_relay,uint64_t now){
    if(pending_relay_==id)pending_relay_=pending_until_=0;
    const auto n=history_.failed(id,is_relay,now);
    if(n)cooldown(id,now,UINT64_C(5000000)<<(n-1));}
  void failed(uint64_t id,uint64_t now){failed(id,relay(id),now);}
  // Report current actual FOLLOW immediately before tick with the same now.
  // No report means unknown, not failure; quiet adds no denominator.
  void usable(bool value,uint64_t now){usable_value_=value;usable_at_=now;usable_reported_=true;}
  void reobserve_gm(uint64_t id,uint64_t now){
    if(!id||!now)return;
    recovering_gm_=id;gm_recovery_until_=now>UINT64_MAX-GmRecoveryUs?UINT64_MAX:now+GmRecoveryUs;
  }
  void unavailable(uint64_t now=0){
    // Retain only an advisory locator preference through a GM clock/scope
    // resynchronization. It never retains time, permission or an RF owner.
    if(now&&parent_&&!parent_relay_)reobserve_gm(parent_,now);
    parent_=renew_at_=bound_at_=0;lock_candidate_since_=lock_sample_at_=0;cold_poor_=true;quality_={};usable_owned_=usable_good_=0;usable_reported_=usable_previous_valid_=false;
  }
  void tick(uint64_t now,bool receiving,bool scanning){
    history_.advance(now);
    if(quality_.clock_seen&&now<quality_.at){quality_.clock_seen=false;quality_.listened=0;quality_.in_sample=0;usable_owned_=usable_good_=0;usable_previous_valid_=false;}
    const auto elapsed_quality=quality_.clock_seen&&now>=quality_.at?now-quality_.at:0;
    const bool eligible=quality_.clock_seen&&quality_.listening&&receiving&&now>=quality_.at&&elapsed_quality<=250000;
    const bool reported=usable_reported_&&usable_at_==now;
    if(eligible&&reported&&usable_previous_valid_){usable_owned_+=elapsed_quality;if(usable_previous_&&usable_value_)usable_good_+=elapsed_quality;}
    const auto listened=quality_.listened+(eligible?elapsed_quality:0);
    const auto received=quality_.in_sample;const bool completed=eligible&&listened>=R2_LINK_SAMPLE_US;
    r2_link_quality_tick(&quality_,receiving,now);
    if(completed&&parent_&&listened){
      const auto expected=listened/R2_LINK_INTERVAL_US;
      auto delivery=expected?uint64_t(received)*1000/expected:0;if(delivery>1000)delivery=1000;
      const bool known=usable_owned_==listened;
      history_.sample(parent_,parent_relay_,uint16_t(delivery),known,
        uint16_t(known?usable_good_*1000/listened:0),now);
      usable_owned_=usable_good_=0;
    }
    usable_previous_valid_=reported;usable_previous_=usable_value_;usable_reported_=false;
    const auto elapsed=scanner_&&scanning&&now>=scan_at_?now-scan_at_:0;
    // Count only observed scanner ownership after discovery began. Quiet,
    // admission, queued starts and unobserved long worker gaps supply no credit.
    if(startup_&&collecting_&&now>=collecting_&&elapsed<=250000){
      const auto owned=elapsed<now-collecting_?elapsed:now-collecting_;
      startup_scan_owned_+=owned<StartupScanUs-startup_scan_owned_?owned:StartupScanUs-startup_scan_owned_;
    }
    scan_at_=now;scanner_=scanning;
    if(elapsed<=250000)scan_owned_+=elapsed;
    // Completed real scanner opportunities, never queued scans or JOIN/map
    // warmup. Direct authenticated delivery suppresses cold fallback.
    if(scan_owned_>=100000){
      scan_owned_=0;
      if(quality_.view.received&&now>=quality_.view.last_rx&&now-quality_.view.last_rx<3000000){missed_=0;discovery_since_=0;}
      else {if(!discovery_since_)discovery_since_=now;if(missed_<3)++missed_;}
    }
    if(missed_>=3&&now>=discovery_since_&&now-discovery_since_>=3000000)cold_poor_=true;
    if(quality_.view.healthy&&now>=quality_.view.last_rx&&now-quality_.view.last_rx<3000000)cold_poor_=false;
  }
  void delivered(uint32_t train,uint16_t event,uint64_t now){r2_link_quality_rx(&quality_,train,event,now);}
  uint64_t choose(uint64_t now,bool renewal=false)const{
    (void)renewal;selection_={};selection_.entries=uint8_t(history_.entries(now));
    // First-source discovery gets several advertisement opportunities before
    // a relay can lock out the GM. Named/relay-only choices keep their policy.
    const bool startup=startup_&&!parent_&&!force_relay_&&!target_;
    if(!collecting_||now<collecting_)return 0;
    const bool collecting=now-collecting_<(startup?StartupCollectUs:CollectUs);
#ifndef PTOB_N30
    if(collecting)return 0;
#endif
    const Candidate* best=nullptr;const Candidate* incumbent=nullptr;const Candidate* unbound_retry=nullptr;const Candidate* active_relay=nullptr;
    const Candidate* best_relay=nullptr;const Candidate* best_gm=nullptr;const Candidate* pending=nullptr;const Candidate* recovery_gm=nullptr;bool waiting_gm=false;
    int best_points=INT16_MIN,current_points=INT16_MIN,gm_points=INT16_MIN;
    const bool trial_failed=needs_alternative();
    const bool retained=parent_&&accepts(parent_,parent_relay_)&&!(force_relay_&&!parent_relay_)&&!quality_.view.poor&&!cold_poor_&&!trial_failed;
    const bool gm_dwell=trial_&&!trial_relay_&&!target_&&!force_relay_&&now>=trial_since_&&now-trial_since_<LockDwellUs;
    const Candidate* trial_gm=nullptr;bool trial_gm_seen=false;
    if(parent_){current_points=G1ParentHistory::score(history_.find(parent_,parent_relay_,now),parent_relay_,bound_rssi_,now,history_);
      selection_.current_score=int16_t(current_points);}
    selection_.reason=Decision::NoCandidate;
    for(const auto& c:candidates_){
      if(gm_dwell&&c.identity==trial_&&!c.relay&&now>=c.seen&&now-c.seen<FreshUs){
        trial_gm_seen=true;if(now>=c.cooldown&&c.seen>c.consumed)trial_gm=&c;
      }
      if(!c.identity||now<c.seen||now<c.cooldown||c.seen<=c.consumed||
         !accepts(c.identity,c.relay)||(force_relay_&&!c.relay))continue;
      // A legacy GM admission hint can disappear while the GM accepts another
      // ACL. A recent hint postpones relay fallback for bounded re-observation;
      // it never permits a connection with an expired locator or grants time.
      if(startup&&!c.relay&&now-c.seen<StartupGmObserveUs)waiting_gm=true;
      if(now-c.seen>=FreshUs)continue;
      const int points=score(c,now);
      if(c.identity==parent_){incumbent=&c;current_points=points;}
      // A failed precision trial remains distinct from successful packet
      // delivery. Keep a current locator for renewal if no alternative exists.
      if(!target_&&!force_relay_&&(deferred(c.identity,c.relay,now)||(trial_failed&&c.identity==trial_))){
        // N30 may lose the active binding while the former parent's failed
        // trial remains. Remember only its freshly observed locator for the
        // bounded retry path; no old clock, permission or binding survives.
        if(failed_retry_us_&&!parent_&&c.identity==trial_&&c.relay==trial_relay_&&retry_allowed(c.identity,c.relay,now))unbound_retry=&c;
        continue;
      }
      if(!c.relay&&c.identity==recovering_gm_)recovery_gm=&c;
      if(c.relay&&c.identity==pending_relay_)pending=&c;
      if(c.relay){
        if(!best_relay||c.rssi>best_relay->rssi||(c.rssi==best_relay->rssi&&
           (points>score(*best_relay,now)||(points==score(*best_relay,now)&&c.identity<best_relay->identity))))best_relay=&c;
        if(c.publishing&&(!active_relay||c.rssi>active_relay->rssi||
           (c.rssi==active_relay->rssi&&c.identity<active_relay->identity)))active_relay=&c;
      }else if(!best_gm||points>gm_points||(points==gm_points&&c.identity<best_gm->identity)){best_gm=&c;gm_points=points;}
    }
#ifdef PTOB_N30
    // A fresh eligible GM locator ends N30's collection wait immediately.
    // The native owner stops the actual scan before fresh authenticated JOIN;
    // cooldown, consumed locators, operator choices and live-parent guards stay.
    if(collecting&&!best_gm)return 0;
#endif
    // Match T096's smoothed RSSI ordering among RCs. Reuse a nearby active
    // stream first, keeping existing GM comparison and current-link guards.
    if(best_relay&&!best_relay->publishing&&active_relay&&active_relay->rssi>=best_relay->rssi-ActiveRelayRssiMargin&&
       score(*active_relay,now)>=score(*best_relay,now)-16)best_relay=active_relay;
    if(gm_dwell&&trial_gm_seen){
      selection_.reason=recovering_gm_==trial_&&now<gm_recovery_until_?Decision::RecoverGM:
        retained&&parent_==trial_?Decision::HoldGM:Decision::Dwell;
      return trial_gm?trial_gm->identity:0; // Cooling still cannot shorten a reachable GM's minute.
    }
    if(!target_&&!force_relay_&&recovering_gm_&&!deferred(recovering_gm_,false,now)&&now<gm_recovery_until_){
      selection_.reason=Decision::RecoverGM;
      if(recovery_gm){selection_.best_score=int16_t(score(*recovery_gm,now));return recovery_gm->identity;}
      return 0; // Bounded re-observation, never a connection using a stale hint.
    }
    if(retained&&!parent_relay_&&!target_&&!force_relay_){
      selection_.reason=Decision::HoldGM;
      if(incumbent)selection_.best_score=int16_t(score(*incumbent,now));
      return incumbent?incumbent->identity:0;
    }
    if(pending_relay_&&now<pending_until_){
      selection_.reason=Decision::Admission;
      if(pending){selection_.best_score=int16_t(score(*pending,now));return pending->identity;}
      return 0; // Wait for this locator to be freshly observed; never admit stale data.
    }
    best=best_gm;
    const bool return_gm=best_gm&&parent_relay_&&!target_&&!force_relay_;
    const bool gm_first=best_gm&&!parent_&&!target_&&!force_relay_&&!trial_failed;
    if(best_relay&&(!best||(!startup&&!return_gm&&!gm_first&&(score(*best_relay,now)>gm_points||
       (score(*best_relay,now)==gm_points&&best_relay->identity<best->identity)))))best=best_relay;
    if(trial_failed&&best_relay)best=best_relay;
    if(!best&&incumbent&&retry_allowed(incumbent->identity,incumbent->relay,now))best=incumbent;
    if(!best&&unbound_retry)best=unbound_retry;
    if(!best)return 0;
    best_points=score(*best,now);
    selection_.best_score=int16_t(best_points);
    if(startup&&best->relay&&(now-collecting_<StartupRelayWaitUs||startup_scan_owned_<StartupScanUs||waiting_gm)){
      selection_.reason=Decision::Collecting;return 0;
    }
    if(incumbent)selection_.current_score=int16_t(current_points);
    if(retained&&best!=incumbent){
      // A remembered score can defer a switch, never supply a stale locator.
      if(now<bound_at_||now-bound_at_<DwellUs){selection_.reason=Decision::Dwell;return incumbent?incumbent->identity:0;}
      if(!return_gm&&best_points<current_points+G1ParentHistory::SwitchMargin){selection_.reason=Decision::Margin;return incumbent?incumbent->identity:0;}
    }
    selection_.reason=trial_failed&&best!=incumbent?Decision::LockTrial:return_gm?Decision::ReturnGM:best==incumbent?Decision::Incumbent:Decision::Best;return best->identity;
  }
};
}
