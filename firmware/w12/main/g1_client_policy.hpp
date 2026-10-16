#pragma once
#include <cstdint>
extern "C" {
#include "group_join.h"
}
namespace v12 {
// Freshness and source permission have separate lifetimes. This clock is a
// conservative replay context, never an independently qualified UTC source.
struct G1Anchor {
  bool source=false;
  uint64_t root=0,boot=0,utc=0,at=0,until=0;
  uint32_t source_epoch=0,publication=0,uncertainty=0;
  uint64_t context_until=0;uint16_t drift_ppm=2000;
  uint64_t deadline()const{return context_until?context_until:r2_group_anchor_deadline(at,uncertainty);}
  uint64_t guard(uint64_t now)const {
    if(now<at)return UINT64_MAX;
    if(drift_ppm==2000)return uint64_t(uncertainty)+(now-at)/500+100;
    const auto elapsed=now-at;
    if(elapsed/1000000>(UINT64_MAX-uncertainty-100)/drift_ppm)return UINT64_MAX;
    return uint64_t(uncertainty)+elapsed/1000000*drift_ppm+(elapsed%1000000*drift_ppm+999999)/1000000+100;
  }
  bool valid(uint64_t now)const {
    return source&&now>=at&&now<deadline()&&guard(now)<249900;
  }
  bool live(uint64_t now)const {return valid(now)&&now<until;}
  bool rebootstrap(uint64_t now)const {
    // T096 precision_step retains acquisition independently of live output.
    // A1.5s continuous permission gap requests a new causal binding; expiry
    // itself withdraws FOLLOW immediately without destroying PA acquisition.
    return !valid(now)||!until||(now>=until&&now-until>=1500000);
  }
  // JOIN refresh is driven by replay uncertainty or sustained acquisition
  // loss, not each short permission. It never renews the corrected model.
  // Continuous reception can leave50ms of replay budget for proactive JOIN,
  // rather than refreshing after only100ms accumulated uncertainty. The
  // half-duty acquisition windows keep their earlier100/125ms thresholds.
  // Neither policy extends the original250ms replay limit or source expiry.
  bool refresh(uint64_t now,bool continuous=false)const {return rebootstrap(now)||guard(now)>=(continuous?200000:100000);}
  bool urgent(uint64_t now,bool continuous=false)const {return rebootstrap(now)||guard(now)>=(continuous?225000:125000);}
  bool same(const G1Anchor& other)const {
    return root==other.root&&boot==other.boot&&source_epoch==other.source_epoch&&publication==other.publication;
  }
};
struct G1Window {
  static constexpr uint64_t Cycle=1000000,Nominal=450000,Quiet=500000,Initiate=100000,SyncCancelGrace=8000;
  static uint64_t start(uint64_t now,uint32_t phase=0){return now>=phase?now-(now-phase)%Cycle:0;}
  static bool active(uint64_t now,uint32_t phase=0){return now>=phase&&(now-phase)%Cycle<Nominal;}
  static uint64_t quiet_deadline(uint64_t now,uint32_t phase=0){return start(now,phase)+Quiet;}
  static bool initiate(uint64_t now,const G1Anchor& anchor,bool corrected,uint32_t phase=0){
    return now>=phase&&(now-phase)%Cycle<Initiate&&anchor.refresh(now)&&(anchor.urgent(now)||corrected);
  }
  static bool align(uint64_t now,const G1Anchor& anchor,bool live_map){return anchor.live(now)&&live_map;}
};
struct G1PhaseLearning {
  G1Anchor scope{};uint64_t last=0;unsigned short_bootstraps=0;
  void accepted(const G1Anchor& next,uint64_t now){
    if(!scope.same(next)||now<last||now-last>3000000)short_bootstraps=0;
    scope=next;last=now;
    if(next.live(now)&&next.until-now<1000000){if(short_bootstraps<2)++short_bootstraps;}
    else short_bootstraps=0;
  }
  bool due(const G1Anchor& current,uint64_t now)const {
    return short_bootstraps>=2&&scope.same(current)&&current.live(now)&&now>=last&&now-last<=3000000;
  }
  void shifted(){short_bootstraps=0;}
};
// A displayed coarse clock has a separate conservative uncertainty budget.
// Retaining it after permission expiry grants no current root, membership,
// source permission or corrected-model lifetime. No host arrival fits it.
struct G1CoarseHold {
  G1Anchor saved{};
  void retain(const G1Anchor& source){saved=source;}
  void clear(){saved={};}
  bool same(const G1Anchor& next)const{return saved.source&&saved.same(next);}
  bool deliver(uint64_t now,uint64_t& utc_us)const {
    if(!saved.valid(now)||now-saved.at>UINT64_MAX-saved.utc)return false;
    utc_us=saved.utc+now-saved.at;return true;
  }
};
}
