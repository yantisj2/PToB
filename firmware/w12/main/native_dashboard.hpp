#pragma once
#include "display.hpp"
#include "ptob_registry.h"
#include "native_r2.hpp"
#include "g1_quality_display.hpp"
#include <cstdio>
namespace v12 {
// Presentation only: live FOLLOW and the valid held clock's origin are
// separate. A remembered/discovered peer cannot label either clock.
class R2Dashboard {
 private:
  bool seen=false,last_requested=false,last_fault=false,last_valid=false,last_following=false;
  bool last_relay_only=false,last_mode_pending=false;
  uint64_t last_root=0,last_boot=0,last_clock_origin=0,last_parent=0,last_parent_boot=0;uint32_t last_source=0,last_train=0;
  uint8_t last_depth=0;bool last_rc_bound=false;char followed_label[24]{},root_label[12]{};
  uint64_t locked_since20=0;
  G1QualityDisplay quality_display;
#ifdef R2_TZ_WIRE
  r2_tz last_tz{};
#endif
 public:
  uint8_t settlement_pending()const{return quality_display.pending();}
  bool needs_authority_update(const R2Status& mesh)const{
    return
#ifdef R2_TZ_WIRE
      last_tz.id!=mesh.tz.id||last_tz.offset_min!=mesh.tz.offset_min||
#endif
      !seen||last_requested!=mesh.requested||last_fault!=mesh.fault||
      last_relay_only!=mesh.rc7_relay_only_requested||last_mode_pending!=mesh.rc7_upstream_mode_pending||
      last_valid!=mesh.clock_valid||last_following!=mesh.following||
      last_root!=mesh.root_identity||last_boot!=mesh.root_boot||last_clock_origin!=mesh.g1_clock_origin||
      last_parent!=mesh.joined_peer||last_parent_boot!=mesh.joined_boot||last_depth!=mesh.rc7_depth||last_rc_bound!=mesh.rc7_bound||
      last_source!=mesh.source_epoch||last_train!=mesh.publication;
  }
  static const char* name(uint64_t identity){
    const auto* alias=ptob_registry_name(identity);return alias?alias:"UNKNOWN";
  }
  static void label(char* out,unsigned bytes,uint64_t identity){
    const auto* known=name(identity);
    if(identity&&!std::strcmp(known,"UNKNOWN"))
      std::snprintf(out,bytes,"B%06llX",static_cast<unsigned long long>(identity&0xffffff));
    else std::snprintf(out,bytes,"%s",identity?known:"NONE");
  }
  void apply(DisplayState& state,const R2Status& mesh){
    const bool changed=last_clock_origin!=mesh.g1_clock_origin;
    seen=true;last_requested=mesh.requested;last_fault=mesh.fault;last_valid=mesh.clock_valid;last_following=mesh.following;
    last_relay_only=mesh.rc7_relay_only_requested;last_mode_pending=mesh.rc7_upstream_mode_pending;
    last_root=mesh.root_identity;last_boot=mesh.root_boot;last_clock_origin=mesh.g1_clock_origin;last_source=mesh.source_epoch;last_train=mesh.publication;
    last_parent=mesh.joined_peer;last_parent_boot=mesh.joined_boot;last_depth=mesh.rc7_depth;last_rc_bound=mesh.rc7_bound;
    state.r2_dashboard=true;state.basic_profile=false;
#ifdef PTOB_W12_SHADOW_HOLDOVER
#ifdef PTOB_W12_HOLDOVER_HORIZON
    state.horizon_ms=mesh.g1_horizon.duration_ms;state.horizon_gaps=mesh.g1_horizon.gaps;state.horizon_probe_ms=mesh.g1_horizon.probe_ms;
    state.horizon_trials=mesh.g1_horizon.trials;state.horizon_bad=mesh.g1_horizon.bad;state.horizon_unknown=mesh.g1_horizon.unknown;
    state.horizon_ready=mesh.g1_horizon.ready;state.horizon_lower_bound=mesh.g1_horizon.lower_bound;
#endif
    state.shadow_holdover=true;state.shadow_p99_ns=mesh.g1_shadow.p99_ns;state.shadow_samples=mesh.g1_shadow.samples;
    state.shadow_age_ms=mesh.g1_shadow.age20/20000;state.shadow_safe_ms=mesh.g1_shadow.safe20/20000;
    state.shadow_cross_ms=mesh.g1_shadow.cross20/20000;state.shadow_holding=mesh.g1_shadow.holding;state.shadow_gaps=mesh.g1_shadow.gaps;
#ifdef PTOB_W12_HOLDOVER_PRODUCTION
    state.holdover_production=mesh.g1_holdover_model;state.holdover_selected=mesh.g1_holdover_selected;
    if(state.holdover_selected&&mesh.g1_clock_model.thermal.start20&&mesh.g1_relative_quality.evaluated20>=mesh.g1_clock_model.thermal.start20)
      state.shadow_age_ms=uint32_t(std::min<uint64_t>(UINT32_MAX,(mesh.g1_relative_quality.evaluated20-mesh.g1_clock_model.thermal.start20)/20000));
#endif
#endif
#ifdef R2_TZ_WIRE
    last_tz=mesh.tz;state.tz=mesh.tz;state.tz_until_us=mesh.tz_until_us;state.tz_sampled_us=mesh.follower_snapshot_us;
#endif
    state.r2_relay_only=mesh.rc7_relay_only_requested;state.r2_mode_pending=mesh.rc7_upstream_mode_pending;
    state.ble_rx_enabled=mesh.requested;state.scan=mesh.scan;
    const bool usable=mesh.requested&&!mesh.fault&&!mesh.rc7_upstream_mode_pending&&mesh.clock_valid;
    const bool following=usable&&mesh.following&&mesh.root_identity&&mesh.root_boot&&mesh.source_epoch&&mesh.publication;
    state.valid=usable;
    // The root and immediate parent are different clocks. Keep the admitted
    // parent/RX mode through a valid held-clock interval; HOLD still visibly
    // withdraws fresh reception. A locator or stale/invalid clock grants no label.
    const bool relayed=usable&&mesh.rc7_bound&&mesh.joined_peer&&mesh.joined_boot&&
      (mesh.rc7_depth==1||mesh.rc7_depth==2);
    const auto parent=relayed?mesh.joined_peer:following?mesh.root_identity:usable?mesh.g1_clock_origin:0;
    char parent_label[12];label(parent_label,sizeof parent_label,parent);
    if(relayed)std::snprintf(followed_label,sizeof followed_label,"%s RC%u",parent_label,unsigned(mesh.rc7_depth));
    else std::snprintf(followed_label,sizeof followed_label,"%s",parent_label);
    state.followed_clock=followed_label;
    state.r2_connection=!usable?"NONE":relayed?(mesh.rc7_depth==1?"RC1":"RC2"):"BLE";
    label(root_label,sizeof root_label,following?mesh.root_identity:usable?mesh.g1_clock_origin:0);
    state.r2_root_clock=root_label;
    state.utc=usable?uint32_t(mesh.follower_utc_us/1000000):0;
    state.timing_source=usable?DisplayState::Ble:DisplayState::NoSource;
    state.gps_source=false;state.utc_holdover=usable&&!following;
    state.role=mesh.fault?"FAULT":following?"FOLLOW":usable?"HOLD":
      mesh.mode==R2Mode::Passive?"PASSIVE":!mesh.requested?"OFF":
      (mesh.connecting||mesh.connections)?"JOIN":mesh.group_member?"ACQUIRE":"SEARCH";
    // The discipliner publishes quality only after physical association and
    // causal correction acceptance; transport/control scatter cannot fill it.
    const bool measured=following&&mesh.model_available20;
    state.fit=measured&&mesh.precision_fit;state.precision_ns=measured?mesh.precision_ns:UINT32_MAX;
    state.rms_ns=measured?mesh.rms_ns:UINT32_MAX;
    state.hold_percentile=99;state.hold_trials=measured?mesh.hold_samples:0;
    state.hold_1us_ms=measured&&mesh.hold_samples>=100?mesh.hold_ms:0;
    state.r2_relative_lock=false;state.hold_estimated=false;state.relative_prediction_ns=UINT32_MAX;
    if(usable&&mesh.g1_clock_model.tick){
      // Host snapshot clock is already available; the main reader refreshes
      // UTC but never refits. Quality projection ages in the same20MHz domain.
      const auto& q=mesh.g1_relative_quality;
      const auto now=q.evaluated20;
      if(q.samples&&now>=mesh.g1_clock_model.tick){state.rms_ns=q.rms_ns;state.samples=q.samples;}
      state.relative_prediction_ns=q.estimate(now,mesh.g1_clock_model);
      state.hold_estimated=q.ready()&&state.relative_prediction_ns!=UINT32_MAX;
      if(state.hold_estimated){
        state.hold_percentile=0; // Explicit model estimate, never P99 trials.
        const auto left=mesh.g1_anchor_deadline_us>mesh.follower_snapshot_us?mesh.g1_anchor_deadline_us-mesh.follower_snapshot_us:0;
        const auto raw_hold=q.hold_ms(now,mesh.g1_clock_model,left);
        const auto shown=quality_display.update(now,q.scope_generation,q.rms_ns,raw_hold,q.locked(now,mesh.g1_clock_model));
        state.rms_ns=shown.rms_ns;state.hold_1us_ms=shown.hold_ms;state.hold_trials=0;
        state.r2_relative_lock=shown.locked;
        if(state.r2_relative_lock&&following)state.role="LOCK_EST";
      }else quality_display.clear();
    }else quality_display.clear();
    const auto now=mesh.g1_relative_quality.evaluated20;
    if(!state.r2_relative_lock||!now)locked_since20=0;
    else if(!locked_since20||changed||now<locked_since20)locked_since20=now;
    state.r2_lock_seconds=locked_since20?(now-locked_since20)/20000000:0;
  }
};
}
