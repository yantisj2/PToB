#pragma once
#include <cstdint>
#include "g1_parent_history.hpp"
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
  enum class Decision:uint8_t{Collecting,NoCandidate,Best,Dwell,Margin,Incumbent,ReservedLock,Admission,RecoverGM,ReturnGM,HoldGM};
  struct SelectionView {uint8_t entries=0;int16_t current_score=INT16_MIN,best_score=INT16_MIN;Decision reason=Decision::Collecting;};
  struct Candidate {
    uint64_t identity=0,seen=0,consumed=0,cooldown=0;
    int16_t rssi=-127;bool relay=false,publishing=false;
  };
 private:
  Candidate candidates_[8]{};
  r2_link_quality quality_{};
  G1ParentHistory history_{};
  uint64_t collecting_=0,discovery_since_=0,parent_=0,renew_at_=0;
  uint64_t scan_at_=0,scan_owned_=0,target_=0;
  uint64_t startup_scan_owned_=0;
  uint64_t pending_relay_=0,pending_until_=0;
  uint64_t recovering_gm_=0,gm_recovery_until_=0;
  uint64_t bound_at_=0,usable_at_=0,usable_owned_=0,usable_good_=0;
  int16_t bound_rssi_=-127;
  uint8_t missed_=0;bool scanner_=false,force_relay_=false,cold_poor_=false,target_relay_=true;
  bool parent_relay_=false,usable_value_=false,usable_reported_=false,usable_previous_=false,usable_previous_valid_=false;
  bool startup_=true; // Cleared by the first actual admission; reset/recovery never rearms it.
  mutable SelectionView selection_{};
  bool relay(uint64_t id)const{for(const auto& c:candidates_)if(c.identity==id)return c.relay;return parent_==id&&parent_relay_;}
  int score(const Candidate& c,uint64_t now)const{return G1ParentHistory::score(history_.find(c.identity,c.relay,now),c.relay,c.rssi,now,history_);}
 public:
  // Retire every locator/current owner while retaining only advisory learning.
  void reset(){for(auto& c:candidates_)c={};quality_={};collecting_=discovery_since_=parent_=renew_at_=0;
    scan_at_=scan_owned_=target_=bound_at_=usable_at_=usable_owned_=usable_good_=startup_scan_owned_=0;missed_=0;
    scanner_=force_relay_=cold_poor_=target_relay_=parent_relay_=false;target_relay_=true;
    pending_relay_=pending_until_=0;
    recovering_gm_=gm_recovery_until_=0;
    bound_rssi_=-127;usable_value_=usable_reported_=usable_previous_=usable_previous_valid_=false;selection_={};}
  void clear_history(){history_.clear();}
  const G1ParentHistory& history()const{return history_;}
  SelectionView selection_view(uint64_t now)const{auto v=selection_;v.entries=uint8_t(history_.entries(now));return v;}
  void diagnostic(bool unavailable){force_relay_=unavailable;}
  bool diagnostic()const{return force_relay_;}
  void target(uint64_t id,bool relay=true){target_=id;target_relay_=relay;}
  bool pinned()const{return target_!=0;}
  bool accepts(uint64_t id,bool relay)const{return !target_||(relay==target_relay_&&id==target_);}
  bool seeking_relay()const{return target_?target_relay_:force_relay_||cold_poor_||quality_.view.poor;}
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
    for(const auto& c:candidates_)if(c.identity==id&&c.relay==parent_relay_){bound_rssi_=c.rssi;break;}
    parent_=id;renew_at_=now>UINT64_MAX-300000000?UINT64_MAX:now+300000000;
    pending_relay_=pending_until_=0;
    recovering_gm_=gm_recovery_until_=0;
    startup_=false;
    r2_link_quality_bind(&quality_,id,boot,epoch);
    if(changed){bound_at_=now;cold_poor_=false;missed_=0;discovery_since_=0;
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
    parent_=renew_at_=bound_at_=0;cold_poor_=true;quality_={};usable_owned_=usable_good_=0;usable_reported_=usable_previous_valid_=false;
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
    if(!collecting_||now<collecting_||now-collecting_<(startup?StartupCollectUs:CollectUs))return 0;
    const Candidate* best=nullptr;const Candidate* incumbent=nullptr;const Candidate* active_relay=nullptr;
    const Candidate* best_relay=nullptr;const Candidate* best_gm=nullptr;const Candidate* pending=nullptr;const Candidate* recovery_gm=nullptr;bool waiting_gm=false;
    int best_points=INT16_MIN,current_points=INT16_MIN,gm_points=INT16_MIN;
    const bool retained=parent_&&accepts(parent_,parent_relay_)&&!(force_relay_&&!parent_relay_)&&!quality_.view.poor&&!cold_poor_;
    if(parent_){current_points=G1ParentHistory::score(history_.find(parent_,parent_relay_,now),parent_relay_,bound_rssi_,now,history_);
      selection_.current_score=int16_t(current_points);}
    selection_.reason=Decision::NoCandidate;
    for(const auto& c:candidates_){
      if(!c.identity||now<c.seen||now<c.cooldown||c.seen<=c.consumed||
         !accepts(c.identity,c.relay)||(force_relay_&&!c.relay))continue;
      // A legacy GM admission hint can disappear while the GM accepts another
      // ACL. A recent hint postpones relay fallback for bounded re-observation;
      // it never permits a connection with an expired locator or grants time.
      if(startup&&!c.relay&&now-c.seen<StartupGmObserveUs)waiting_gm=true;
      if(now-c.seen>=FreshUs)continue;
      const int points=score(c,now);
      if(!c.relay&&c.identity==recovering_gm_)recovery_gm=&c;
      if(c.relay&&c.identity==pending_relay_)pending=&c;
      if(c.relay){
        if(!best_relay||c.rssi>best_relay->rssi||(c.rssi==best_relay->rssi&&
           (points>score(*best_relay,now)||(points==score(*best_relay,now)&&c.identity<best_relay->identity))))best_relay=&c;
        if(c.publishing&&(!active_relay||c.rssi>active_relay->rssi||
           (c.rssi==active_relay->rssi&&c.identity<active_relay->identity)))active_relay=&c;
      }else if(!best_gm||points>gm_points||(points==gm_points&&c.identity<best_gm->identity)){best_gm=&c;gm_points=points;}
      if(c.identity==parent_){incumbent=&c;current_points=points;}
    }
    // Match T096's smoothed RSSI ordering among RCs. Reuse a nearby active
    // stream first, keeping existing GM comparison and current-link guards.
    if(best_relay&&!best_relay->publishing&&active_relay&&active_relay->rssi>=best_relay->rssi-ActiveRelayRssiMargin&&
       score(*active_relay,now)>=score(*best_relay,now)-16)best_relay=active_relay;
    if(!target_&&!force_relay_&&recovering_gm_&&now<gm_recovery_until_){
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
    const bool gm_first=best_gm&&!parent_&&!target_&&!force_relay_;
    if(best_relay&&(!best||(!startup&&!return_gm&&!gm_first&&(score(*best_relay,now)>gm_points||
       (score(*best_relay,now)==gm_points&&best_relay->identity<best->identity)))))best=best_relay;
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
    selection_.reason=return_gm?Decision::ReturnGM:best==incumbent?Decision::Incumbent:Decision::Best;return best->identity;
  }
};
}
