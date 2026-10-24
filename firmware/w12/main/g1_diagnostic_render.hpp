#pragma once
#include "native_r2.hpp"
#include "native_transport.hpp"
#include "bounded_log.hpp"
#include "g1_hci_trace.hpp"
#include <cstdio>
#include <cinttypes>
namespace v12 {
constexpr unsigned G1DiagnosticLineBytes=768;
inline int g1_render_parent(char* out,unsigned capacity,const G1ParentEvent& e,uint32_t lost=0){
  const auto& a=e.from;const auto& b=e.to;
  return std::snprintf(out,capacity,"W12_G1_PARENT seq=%" PRIu32 " at_us=%" PRIu64 " reason=%s decision=%u poor=%u score=%d/%d"
    " old_parent=%016" PRIx64 " new_parent=%016" PRIx64 " old_role=%s new_role=%s"
    " old_boot=%016" PRIx64 " new_boot=%016" PRIx64 " old_root=%016" PRIx64 " new_root=%016" PRIx64
    " old_root_boot=%016" PRIx64 " new_root_boot=%016" PRIx64 " source=%" PRIu32 "/%" PRIu32
    " train=%" PRIu32 "/%" PRIu32 " depth=%u/%u lost_before=%" PRIu32 " history=RAM8 precision=UNQUALIFIED\n",
    e.sequence,e.at,g1_parent_reason(e.reason),unsigned(e.decision),unsigned(e.poor),int(e.current_score),int(e.best_score),
    a.peer,b.peer,!a.peer?"NONE":a.relay?"RC":"GM",!b.peer?"NONE":b.relay?"RC":"GM",
    a.boot,b.boot,a.root,b.root,a.root_boot,b.root_boot,a.source,b.source,a.train,b.train,unsigned(a.depth),unsigned(b.depth),lost);
}
inline uint64_t g1_diag_le(const uint8_t* p,unsigned bytes){uint64_t n=0;for(unsigned i=0;i<bytes;i++)n|=uint64_t(p[i])<<(8*i);return n;}
inline int g1_render_relay_owner(char* out,unsigned capacity,const Report& r){
  if(r.kind!=22||r.length!=64||r.bytes[0]!=1)return 0;
  constexpr const char* events[]={"INVALID","CONNECT","TERMINATE_BEGIN","TERMINATE_RETURN","DISCONNECT"};
  const auto event=r.bytes[1];const char* name=event<sizeof(events)/sizeof(events[0])?events[event]:"INVALID";
  return std::snprintf(out,capacity,"W12_G1_RELAY_OWNER at_us=%" PRIu64 " generation=%" PRIu32
    " event=%s decision=%u handle=%u descriptor_rc=%" PRId32 " role=%u result=%" PRId32
    " state=0x%02x service=0x%02x gates=0x%04x upstream_handle=%" PRIu64 " session_handle=%" PRIu64 " quarantine=%u"
    " deadline_us=%" PRIu64 " upstream_drain_us=%" PRIu64 " session_drain_us=%" PRIu64
    " upstream=%016" PRIx64 " address=%u/%02x%02x%02x%02x%02x%02x reference=API_ONLY\n",
    r.callback_us,r.cycle,name,unsigned(r.bytes[2]),unsigned(r.handle),int32_t(g1_diag_le(r.bytes+6,4)),unsigned(r.bytes[3]),
    int32_t(g1_diag_le(r.bytes+10,4)),unsigned(r.bytes[4]),unsigned(r.bytes[27]),unsigned(g1_diag_le(r.bytes+14,2)),
    g1_diag_le(r.bytes+16,2),g1_diag_le(r.bytes+18,2),unsigned(r.bytes[5]),g1_diag_le(r.bytes+32,8),
    g1_diag_le(r.bytes+40,8),g1_diag_le(r.bytes+48,8),g1_diag_le(r.bytes+56,8),unsigned(r.bytes[20]),
    unsigned(r.bytes[26]),unsigned(r.bytes[25]),unsigned(r.bytes[24]),unsigned(r.bytes[23]),unsigned(r.bytes[22]),unsigned(r.bytes[21]));
}
inline int g1_render_tx_estimate(char* out,unsigned capacity,const Report& r){
  if(r.kind!=23||r.length!=184||r.bytes[0]!=1||(r.bytes[1]!=1&&r.bytes[1]!=2)||r.bytes[3]!=5)return 0;
  return std::snprintf(out,capacity,"W12_G1_TX_EST at_us=%" PRIu64
    " publisher=%016" PRIx64 "/%016" PRIx64 " root=%016" PRIx64 "/%016" PRIx64 "/%" PRIu64
    " train=%" PRIu64 " epoch=%" PRIu64 " pub=%" PRIu64 " event=%" PRIu64 " ch=%u"
    " tick20=%" PRIu64 " callback20=%" PRIu64 " anchor20=%" PRIu64 "/%" PRIu64
    " bt=%" PRIu64 "/%" PRIu64 " tx_ns=%" PRIu64 " seen20=%" PRIu64
    " map20=%" PRIu64 " map_ns=%" PRId64 " phase_ns=%" PRId64 " rate_mppb=%" PRId32 " rate_known=%u"
    " original20=%" PRIu64 " queued20=%" PRIu64 " source_ns=%" PRIu64 "/%" PRIu64
    " age_us=%" PRIu64 " reference=%s actual=0 bound=UNKNOWN\n",
    r.callback_us,g1_diag_le(r.bytes+44,8),g1_diag_le(r.bytes+52,8),g1_diag_le(r.bytes+60,8),g1_diag_le(r.bytes+68,8),g1_diag_le(r.bytes+20,4),
    g1_diag_le(r.bytes+12,4),g1_diag_le(r.bytes+16,4),g1_diag_le(r.bytes+8,4),g1_diag_le(r.bytes+4,2),unsigned(r.bytes[2]),
    g1_diag_le(r.bytes+76,8),g1_diag_le(r.bytes+84,8),g1_diag_le(r.bytes+92,8),g1_diag_le(r.bytes+100,8),
    g1_diag_le(r.bytes+32,4),g1_diag_le(r.bytes+36,4),g1_diag_le(r.bytes+108,8),g1_diag_le(r.bytes+116,8),
    g1_diag_le(r.bytes+124,8),int64_t(g1_diag_le(r.bytes+132,8)),int64_t(g1_diag_le(r.bytes+140,8)),int32_t(g1_diag_le(r.bytes+180,4)),unsigned(r.bytes[6]),
    g1_diag_le(r.bytes+148,8),g1_diag_le(r.bytes+156,8),g1_diag_le(r.bytes+164,8),g1_diag_le(r.bytes+172,8),g1_diag_le(r.bytes+24,4),r.bytes[1]==2?"PROGRAMMED_HALF_SLOT_MEAN4":"PROGRAMMED_HALF_SLOT");
}

// Same tuple as W12_G1_TX_EST. V2 exposes the nominal1M ADDRESS endpoint and
// the legacy-total decomposition. V1 remains a historical programmed estimate.
inline int g1_render_tx_path(char* out,unsigned capacity,const Report& r){
  const bool v4=r.length==160&&r.bytes[0]==4;
  const bool v3=(r.length==128&&r.bytes[0]==3)||v4;
  const bool v2=(r.length==124&&r.bytes[0]==2)||v3;
  if(r.kind!=24||(!v2&&(r.length!=96||r.bytes[0]!=1)))return 0;
  const int prefix=std::snprintf(out,capacity,"W12_G1_TX_PATH at_us=%" PRIu64
    " publisher=%016" PRIx64 "/%016" PRIx64 " root=%016" PRIx64 "/%016" PRIx64 "/%" PRIu64
    " train=%" PRIu64 " epoch=%" PRIu64 " pub=%" PRIu64 " event=%" PRIu64 " ch=%u tick20=%" PRIu64
    " raw_tx_ns=%" PRIu64 " tx_ns=%" PRIu64 " path_offset_ns=%" PRId32 " path_profile=%016" PRIx64
    " capture=%016" PRIx64,
    r.callback_us,g1_diag_le(r.bytes+24,8),g1_diag_le(r.bytes+32,8),g1_diag_le(r.bytes+40,8),g1_diag_le(r.bytes+48,8),g1_diag_le(r.bytes+16,4),
    g1_diag_le(r.bytes+8,4),g1_diag_le(r.bytes+12,4),g1_diag_le(r.bytes+4,4),g1_diag_le(r.bytes+2,2),unsigned(r.bytes[1]),g1_diag_le(r.bytes+56,8),
    g1_diag_le(r.bytes+64,8),g1_diag_le(r.bytes+72,8),int32_t(g1_diag_le(r.bytes+20,4)),g1_diag_le(r.bytes+80,8),g1_diag_le(r.bytes+88,8));
  if(prefix<0||unsigned(prefix)>=capacity)return prefix;
  unsigned used=unsigned(prefix);
  if(v4){
    const auto n=std::snprintf(out+used,capacity-used," projections20=%" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64,
      g1_diag_le(r.bytes+128,8),g1_diag_le(r.bytes+136,8),g1_diag_le(r.bytes+144,8),g1_diag_le(r.bytes+152,8));
    if(n<0||unsigned(n)>=capacity-used)return n<0?n:int(used)+n;
    used+=unsigned(n);
  }
  const int suffix=v2?std::snprintf(out+used,capacity-used,
    " address20=%" PRIu64 " address_ns=%" PRIu64 " base_ns=%" PRId32 " residual_ns=%" PRId32 " applied_ns=%" PRId32
    " receive_correction_ns=%" PRId32 " reference=NOMINAL_ADDRESS_1M delay=UNKNOWN jitter=SEPARATE actual=0 bound=UNKNOWN\n",
    g1_diag_le(r.bytes+96,8),g1_diag_le(r.bytes+104,8),int32_t(g1_diag_le(r.bytes+112,4)),
    int32_t(g1_diag_le(r.bytes+116,4)),int32_t(g1_diag_le(r.bytes+120,4)),v3?int32_t(g1_diag_le(r.bytes+124,4)):0):
    std::snprintf(out+used,capacity-used," scope=AGGREGATE_PROVISIONAL jitter=SEPARATE actual=0 bound=UNKNOWN absolute_UTC=UNKNOWN\n");
  return suffix<0?suffix:int(used)+suffix;
}

// Main-task rendering only. Record size is fixed and independent of RF rate.
inline int g1_render_link(char* out,unsigned capacity,const Report& r){
  if(r.kind!=8||r.length!=32||r.bytes[5]!=2)return 0;
  constexpr const char* events[]={"INVALID","CONNECT","DISCONNECT","PUBLIC_MARKER","LOCATOR",
    "JOIN_TX","HIDDEN_CLEANUP","TERMINATE_ACK","QUIET_FAULT","MTU","COLD_PHASE",
    "CONNECT_REQUEST","CONNECT_ACK","ACL_DESCRIPTOR","CANCEL_ACK","SYNC_REQUEST","SYNC_CREATE_ACK",
    "SYNC_ESTABLISHED","SYNC_CANCEL_ACK","SYNC_TERMINATE_ACK","SYNC_LOST","PERMISSION_PHASE",
    "SYNC_CLEANUP_QUEUED","SYNC_CANCEL_BEGIN","SYNC_TERMINATE_BEGIN","CORRECTED_PHASE",
    "HOST_RESET","HOST_RESYNC","CLIENT_REARM","REARM_REFUSED","SYNC_CACHE_CHECK"};
  const unsigned event=r.bytes[0];const char* name=event<sizeof(events)/sizeof(events[0])?events[event]:"INVALID";
  if(event==1&&!g1_diag_le(r.bytes+1,4)&&(r.bytes[7]&32))name="CONNECT_DRAIN_ACK";
  const char* result=(event==6||event==8)?"window_detail_us":event==9?"mtu_status_packed":(event==10||event==21||event==25)?"phase_us":"rc";
  return std::snprintf(out,capacity,"W12_G1_LINK at_us=%" PRIu64 " gen=%" PRIu32 " peer=%016" PRIx64
    " event=%s %s=%" PRId32 " handle=%u stage=%u owners=0x%02x window_us=%" PRIu64 " attempt_us=%" PRIu64
    " interval_1250us=%" PRIu64 " latency=%" PRIu64 " supervision_10ms=%" PRIu64 " PA_phy=%u PA_clock_accuracy=%u reference=API_ONLY\n",
    r.callback_us,r.cycle,g1_diag_le(r.bytes+16,8),name,result,int32_t(g1_diag_le(r.bytes+1,4)),unsigned(r.handle),unsigned(r.bytes[6]),unsigned(r.bytes[7]),
    g1_diag_le(r.bytes+8,4),g1_diag_le(r.bytes+12,4),g1_diag_le(r.bytes+24,2),g1_diag_le(r.bytes+26,2),g1_diag_le(r.bytes+28,2),unsigned(r.bytes[30]),unsigned(r.bytes[31]));
}
inline int g1_render_scope(char* out,unsigned capacity,const Report& r){
  if(r.kind!=10||r.length!=58||r.bytes[0]!=1)return 0;
  return std::snprintf(out,capacity,"W12_G1_SCOPE at_us=%" PRIu64 " reason=%s old=%016" PRIx64 "/%016" PRIx64 "/%" PRIu64 "/%" PRIu64
    " new=%016" PRIx64 "/%016" PRIx64 "/%" PRIu64 "/%" PRIu64 " until_us=%" PRIu64 " precision=UNQUALIFIED\n",
    r.callback_us,g1_reason_name(G1Reason(r.bytes[1])),g1_diag_le(r.bytes+2,8),g1_diag_le(r.bytes+10,8),g1_diag_le(r.bytes+18,4),g1_diag_le(r.bytes+22,4),
    g1_diag_le(r.bytes+26,8),g1_diag_le(r.bytes+34,8),g1_diag_le(r.bytes+42,4),g1_diag_le(r.bytes+46,4),g1_diag_le(r.bytes+50,8));
}
inline int g1_render_summary(char* out,unsigned capacity,const R2Status& s,uint64_t now,unsigned row){
  const auto& d=s.g1_diagnostics;const auto* c=d.reasons;
  if(row==0)return std::snprintf(out,capacity,"W12_G1_DIAG at_us=%" PRIu64 " running=%u fault=%u following=%u candidate=%016" PRIx64
    " rssi=%d eligible=%" PRIu32 " attempts=%" PRIu32 " accepted=%" PRIu32 " connect_ready=%" PRIu32 "/%" PRIu32 " drain_CONNECT=%" PRIu32 " descriptor=%" PRIu32
    " preCONNECT_loss=%" PRIu32 " hidden_cleanup=%" PRIu32 " last_disconnect=0x%03" PRIx32
    " TX_getter_dbm=%d/%d/%d/%d"
    " closed=%" PRIu32 " min_margin_us=%" PRIu32 " disconnect_max_us=%" PRIu32 " window_max_us=%" PRIu64 " queue_drop=%" PRIu32 " traces=%" PRIu32 "\n",
    now,unsigned(s.client.running),unsigned(s.fault),unsigned(s.following),d.last_candidate,int(d.last_rssi),d.eligible,s.join_attempts,s.join_accepts,d.connect_ok,d.connect_failed,d.connect_drain,d.descriptor_seen,
    s.g1_pending_disconnects,s.g1_pending_link_teardowns,uint32_t(s.g1_last_disconnect_reason),
    int(d.default_tx_dbm),int(d.scan_tx_dbm),int(d.init_tx_dbm),int(d.conn_tx_dbm),
    d.closed,d.close_min_margin_us,d.disconnect_max_us,s.g1_window_max_owned_us,s.drops,d.traces);
  if(row==1)return std::snprintf(out,capacity,"W12_G1_FILTER first=%s@%" PRIu64 " last=%s@%" PRIu64
    " malformed=%" PRIu32 " group=%" PRIu32 " epoch=%" PRIu32 " flags=%" PRIu32 " address=%" PRIu32 " identity=%" PRIu32
    " window=%" PRIu32 " owner=%" PRIu32 " retry=%" PRIu32 " cooldown=%" PRIu32 " hint=%" PRIu32 " current_peer=%" PRIu32
    " join_stale=%" PRIu32 " join_auth=%" PRIu32 " join_busy=%" PRIu32 " join_exchange=%" PRIu32 " join_time=%" PRIu32 " root_hint=%" PRIu32
    " PA_stale=%" PRIu32 " PA_auth=%" PRIu32 " PA_scope=%" PRIu32 " permission=%" PRIu32 "\n",
    g1_reason_name(d.first),d.first_us,g1_reason_name(d.last),d.last_us,
    c[1],c[2],c[3],c[4],c[5],c[6],c[7],c[8],c[9],c[10],c[11],c[12],c[13],c[14],c[15],c[16],c[17],c[18],c[19],c[20],c[21],c[22]);
  return std::snprintf(out,capacity,"W12_G1_TIME root=%016" PRIx64 " boot=%016" PRIx64 " server=%016" PRIx64
    " source=%" PRIu32 " train=%" PRIu32 " permission_left_us=%" PRIu64 " GR3=%" PRIu32 "/%" PRIu32 " servo=%" PRIu32 "/%" PRIu32
    " refusal_first=0x%04" PRIx32 " refusal_last=0x%04" PRIx32
    " corrected=%" PRIu32 "/%" PRIu32 " frequency=%u/%" PRIu32 " rate_ppb=%d scatter_ns=%" PRIu32 " causal=%" PRIu32 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 " missing_capture=%" PRIu32 " edge=%" PRIu32 " precision=UNQUALIFIED\n",
    s.root_identity,s.root_boot,s.joined_peer,s.source_epoch,s.publication,s.service_until_us>now?s.service_until_us-now:0,s.g1_relay_authenticated,s.g1_relay_refused,s.discipline_accepted,s.discipline_refused,
    s.discipline_first_refusal,s.discipline_last_refusal,s.g1_corrected_accepted,s.g1_corrected_refused,unsigned(s.g1_frequency_trained),s.g1_frequency_pairs,int(s.g1_clock_model.rate),s.g1_frequency_scatter_ns,s.g1_clock_predictions,s.g1_clock_causal_squares,s.g1_clock_nominal_squares,s.g1_clock_causal_max_ns,s.discipline_reason_counts[2],s.discipline_reason_counts[5]);
}
// Bounded capture-only diagnostics; ordinary status frame stays unchanged.
inline int g1_render_admission(char* out,unsigned capacity,const R2Status& s,uint64_t now){
  const auto& a=s.g1_admission_policy;
  return std::snprintf(out,capacity,"W12_G1_ADMISSION at_us=%" PRIu64 " warm=%u starts_us=%" PRIu64 "/%" PRIu64
    " older_start_us=%" PRIu64 " retry_at_us=%" PRIu64 " failures=%" PRIu32 " reply=%u NO_SOURCE=%" PRIu32 " foreign_root=%" PRIu32 " unusable=%" PRIu32
    " local_estimate=%u observed_us=%" PRIu64 " loss_expiries=%" PRIu32 " loss_reacquisitions=%" PRIu32
    " error_bound=UNKNOWN source_extension=0 USB_timing=0\n",
    now,unsigned(a.warm),a.starts[0],a.starts[1],a.older[0],a.retry_at,a.failures,unsigned(a.last),a.no_source,a.foreign_root,a.unusable,
    unsigned(s.g1_retained_estimate),s.g1_estimate_observed_us,s.g1_receive_loss.expiries,s.g1_receive_loss.reacquisitions);
}
inline int g1_render_loss(char* out,unsigned capacity,const G1ReceiveLoss& c,unsigned row){
  if(!c.frozen||row>=c.count)return 0;
  const auto& e=c.entries[(c.sequence-c.count+row)%8];
  return std::snprintf(out,capacity,"W12_G1_RX_LOSS gen=%" PRIu32 " seq=%" PRIu32 " count=%" PRIu32 " frozen_us=%" PRIu64
    " row=%u at_us=%" PRIu64 " outcome=0x%02" PRIx32 " capture_epoch=%" PRIu32
    " reference=WORKER_OBSERVATION history=FIRST_LOSS_PER_BOOT later_detail=UNAVAILABLE RF_opportunity=UNKNOWN DONE=UNKNOWN CRC=UNKNOWN\n",
    c.generation,c.sequence,c.count,c.frozen_at,row,e.at,e.outcome,e.epoch);
}
inline int g1_render_recovery(char* out,unsigned capacity,const R2Status& s){
  const auto& r=s.g1_recovery;
  return std::snprintf(out,capacity,"W12_G1_RECOVERY stage=%u reset=%" PRIu32 " sync=%" PRIu32 " rearm=%" PRIu32 " refused=%" PRIu32
    " reason=%" PRId32 " reset_us=%" PRIu64 " sync_us=%" PRIu64 " quiet_until_us=%" PRIu64
    " old_generation=%" PRIu32 " old_stop_us=%" PRIu64 " old_deadline_us=%" PRIu64 " old_completed_us=%" PRIu64 " original_failure=RETAINED\n",
    unsigned(r.stage),r.resets,r.syncs,r.rearms,r.refused,r.reason,r.reset_at,r.sync_at,r.quiet_until,r.old_generation,r.old_stop_requested,r.old_stop_deadline,r.old_completed);
}
inline int g1_render_clock(char* out,unsigned capacity,const R2Status& s,uint64_t now){
  const auto& p=s.g1_clock_model;
  const auto& q=s.g1_relative_quality;
  const auto left=s.g1_anchor_deadline_us>s.follower_snapshot_us?s.g1_anchor_deadline_us-s.follower_snapshot_us:0;
  return std::snprintf(out,capacity,"W12_G1_CLOCK at_us=%" PRIu64 " valid=%u origin=%016" PRIx64
    " anchor_at_us=%" PRIu64 " guard_us=%" PRIu64 " replay_until_us=%" PRIu64
    " signed_map_until_us=%" PRIu64 " original20=%" PRIu64 " available20=%" PRIu64
    " sample20=%" PRIu64 " evaluated20=%" PRIu64
    " relative_ns=%" PRId64 " phase_ns=%" PRId64 " utc_offset_ns=%" PRId64
    " trained=%u pairs=%" PRIu32 " rate_ppb=%d scatter_ns=%" PRIu32
    " causal_recent=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " rate_guard_ppb=%" PRIu32
    " relative_est_ns=%" PRIu32 " relative_hold_est_ms=%" PRIu32 " relative_lock_est=%u"
    " resyncs=%" PRIu32 " last_step_ns=%" PRIu64 " fit20=%" PRIu64
    " qualification=UNQUALIFIED hold_budget=EST_REPLAY\n",
    now,unsigned(s.clock_valid),s.g1_clock_origin,s.follower_received_us,s.g1_anchor_guard_us,s.g1_anchor_deadline_us,
    s.g1_corrected_until_us,s.g1_corrected_original20,p.tick,q.original20,q.evaluated20,p.epoch,p.phase,p.utc_offset,
    unsigned(s.g1_frequency_trained),s.g1_frequency_pairs,int(p.rate),s.g1_frequency_scatter_ns,
    q.samples,q.rms_ns,q.max_ns,uint32_t(std::min<double>(UINT32_MAX,std::ceil(q.rate_guard_ppb))),
    q.estimate(q.evaluated20,p),q.hold_ms(q.evaluated20,p,left),unsigned(s.following&&q.locked(q.evaluated20,p)&&q.hold_ms(q.evaluated20,p,left)>=1000),
    s.g1_clock_discontinuities,s.g1_clock_last_discontinuity_ns,s.g1_clock_fit_tick20);
}
inline int g1_render_fit(char* out,unsigned capacity,const R2Status& s,uint64_t now){
  return std::snprintf(out,capacity,"W12_G1_FIT at_us=%" PRIu64 " fit20=%" PRIu64 " points=%u span_ms=%u pairs=%" PRIu32 " reason=%u fresh=%u unscored=%" PRIu32 " history=COMPACT64_ACQ62MS_TRACK250MS quality=ALL_CAUSAL_PHYSICAL16S precision=UNQUALIFIED\n",
    now,s.g1_clock_fit_tick20,unsigned(s.g1_fit_points),unsigned(s.g1_fit_span_ms),s.g1_frequency_pairs,unsigned(s.g1_fit_reason),unsigned(s.g1_frequency_trained),s.g1_quality_missing);
}
// Same immutable snapshot and DisplayState that the OLED consumes. Main task
// only, during a bounded capture; the emitter formats into the existing logger.
// No added queue, RF message, callback formatting or display confidence gate.
template<typename Emit>
inline int g1_emit_ui_budget(Emit emit,const R2Status& s,uint64_t now,const char* ui_state,
                           uint32_t ui_rms,uint32_t ui_hold,bool ui_lock,unsigned settle,uint64_t lock_seconds=0){
  const auto& q=s.g1_relative_quality;const auto& p=s.g1_clock_model;
  const auto left=s.g1_anchor_deadline_us>s.follower_snapshot_us?s.g1_anchor_deadline_us-s.follower_snapshot_us:0;
  const auto hold=q.hold_ms(q.evaluated20,p,left);const double phase=q.phase_left(q.evaluated20,p);
  const auto phase_ns=std::isfinite(phase)?uint32_t(std::min<double>(UINT32_MAX,std::ceil(phase))):UINT32_MAX;
  const auto guard=std::isfinite(q.rate_guard_ppb)&&q.rate_guard_ppb>=0?uint32_t(std::min<double>(UINT32_MAX,std::ceil(q.rate_guard_ppb))):UINT32_MAX;
  return emit("W12_G1_UI at_us=%" PRIu64 " evaluated20=%" PRIu64 " sample20=%" PRIu64 " model20=%" PRIu64
    " epoch_ns=%" PRId64 " phase_ns=%" PRId64 " rate_mppb=%" PRId32
    " origin=%016" PRIx64 " boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32
    " scope=%" PRIu32 " valid=%u follow=%u causal=%" PRIu32 "/%" PRIu32 "/%" PRIu32
    " phase_left_ns=%" PRIu32 " rate_guard_ppb=%" PRIu32 " accel_guard_ppb_s=%" PRIu32 " budget_ns=%" PRIu32 " legacy_sum_ns=%" PRIu32
    " raw_hold_ms=%" PRIu32 " raw_lock=%u ui_state=%s ui_rms_ns=%" PRIu32 " ui_hold_ms=%" PRIu32
    " ui_lock=%u settle=%u ui_lock_s=%" PRIu64 " lock_v=2 precision=UNQUALIFIED\n",
    now,q.evaluated20,q.original20,p.tick,p.epoch,p.phase,int32_t(std::isfinite(p.rate)&&std::abs(p.rate)<=50000?std::llround(p.rate*1000):0),
    s.g1_clock_origin,s.root_boot,s.source_epoch,s.publication,q.scope_generation,unsigned(s.clock_valid),unsigned(s.following),
    q.samples,q.rms_ns,q.max_ns,phase_ns,guard,std::isfinite(q.accel_guard_ppb_s)?uint32_t(q.accel_guard_ppb_s):UINT32_MAX,q.estimate(q.evaluated20,p),q.legacy_sum_estimate(q.evaluated20,p),
    hold,unsigned(s.clock_valid&&q.locked(q.evaluated20,p)&&hold>=1000),ui_state,ui_rms,ui_hold,unsigned(ui_lock),settle,lock_seconds);
}
inline int g1_render_peer(char* out,unsigned capacity,const G1Diagnostics::Peer& p){
  if(!p.identity)return 0;
  return std::snprintf(out,capacity,"W12_G1_PEER identity=%016" PRIx64 " last_us=%" PRIu64 " rssi=%d eligible=%" PRIu32
    " attempts=%" PRIu32 " accepts=%" PRIu32 " connect_failed=%" PRIu32 " trust=DISCOVERY_ONLY\n",
    p.identity,p.last_us,int(p.rssi),p.eligible,p.attempts,p.accepts,p.failures);
}
inline int g1_render_live(char* out,unsigned capacity,const R2Status& s,const LogStatus& log,uint64_t now,const G1HciStatus& hci={}){
  const auto& d=s.g1_diagnostics;
  const char* state=s.fault?"FAULT":!s.client.running?"OFF":s.following?"FOLLOW":s.clock_valid?"HOLD":s.group_member?"ACQUIRE":s.connecting||s.connections?"JOIN":"SEARCH";
  return std::snprintf(out,capacity,"W12_G1_LIVE at_us=%" PRIu64 " state=%s root=%016" PRIx64 " server=%016" PRIx64
    " perm_us=%" PRIu64 " refresh=%u candidate=%016" PRIx64 " rssi=%d attempts=%" PRIu32 " JOIN_ok=%" PRIu32 " ACL_desc=%" PRIu32 " ready=%" PRIu32 " drain_ACK=%" PRIu32
    " preCONNECT_loss=%" PRIu32 " disconnect=0x%03" PRIx32 " margin_us=%" PRIu32 " quiet_fault=%" PRIu32
    " GR3=%" PRIu32 "/%" PRIu32 " servo=%" PRIu32 "/%" PRIu32 " refusal=0x%04" PRIx32
    " corrected=%" PRIu32 " rate_ppb=%d freq=%u TX_getter_dbm=%d/%d/%d/%d rearm=%" PRIu32 " queue_peak=%" PRIu32 " dropped=%" PRIu32 " expired=%" PRIu32 " HCI_pending=0x%04x/%" PRIu64 " HCI_drop=%" PRIu32 " precision=UNQUALIFIED\n",
    now,state,s.root_identity,s.joined_peer,s.service_until_us>now?s.service_until_us-now:0,unsigned(s.g1_refresh_needed),d.last_candidate,int(d.last_rssi),s.join_attempts,s.join_accepts,d.descriptor_seen,d.connect_ok,d.connect_drain,
    s.g1_pending_disconnects,uint32_t(s.g1_last_disconnect_reason),d.close_min_margin_us,s.g1_quiet_faults,
    s.g1_relay_authenticated,s.g1_relay_refused,s.discipline_accepted,s.discipline_refused,s.discipline_last_refusal,
    s.g1_corrected_accepted,int(s.g1_clock_model.rate),unsigned(s.g1_frequency_trained),int(d.default_tx_dbm),int(d.scan_tx_dbm),int(d.init_tx_dbm),int(d.conn_tx_dbm),s.g1_recovery.rearms,log.peak,log.dropped,log.expired,unsigned(hci.pending_opcode),hci.pending_at_us&&now>=hci.pending_at_us?now-hci.pending_at_us:0,hci.dropped);
}
}
