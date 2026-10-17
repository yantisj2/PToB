#pragma once
#include "g1_parent_journal.hpp"
#include "g1_admission_policy.hpp"
#include "g1_receive_loss.hpp"
#include "g1_retained_estimate.hpp"
#include "g1_relative_quality.hpp"
#include <cstdint>
#include "console_protocol.hpp"
#include "r2_client_service.hpp"
#include "periodic_rx_model.hpp"
#include "g1_diagnostics.hpp"
#include "g1_clock_prediction.hpp"
#include "g1_reset_recovery.hpp"
#include "rc_parent_diagnostic.hpp"
#include "../legacy/reference/clock_prediction.hpp"
namespace v12 {
enum class R2Mode:uint8_t {Off,Passive,Active,Client};
enum class R2Api:uint8_t {None,DataLength,Mtu,Service,Characteristics,ControlWrite,SyncCreate,ScanStart,ScanStop,AdvStop,AdvRemove,Disconnect,ConnectCancel,SyncCancel,SyncTerminate,AdvConfigure,AdvData,AdvStart,Connect};
struct R2Status {
  R2Mode mode=R2Mode::Off;OwnerLease lease{};R2ClientService client{};
  bool requested=false,scan=false,advertising=false,sync_pending=false,synced=false;
  bool connecting=false,owner_busy=false,off_ack=true,fault=false,active_used=false;
  bool ready=false,serving=false,following=false,clock_valid=false,source=false;
  bool boot_client=false,boot_client_attempted=false,boot_client_cancelled=false;
  uint8_t self=255,coordinator=255,root=255,standby=255,members=0,role=0;
  uint32_t connections=0,rx=0,tx=0,rejected=0,drops=0,authenticated=0,hellos=0;
  uint32_t discoveries=0,pa_reports=0,p1_authenticated=0,p1_refused=0,api_errors=0;
  uint64_t follower_utc_us=0,close_deadline_us=0,worker_max_us=0;
  uint64_t follower_snapshot_us=0,follower_received_us=0;
  uint64_t root_boot=0,root_identity=0,service_until_us=0;
  uint32_t source_epoch=0,publication=0;
  uint16_t peer_mtu_min=0;
  uint16_t precision_interval_units=0,precision_hz=0;
  uint64_t schedule_descriptor=0;
  uint32_t peer_losses=0,setup_retries=0;
  int32_t last_api_rc=0;
  R2Api last_api=R2Api::None;
  uint8_t last_api_peer=255,link_mask=0,gatt_mask=0;
  uint64_t last_api_at_us=0;
  R2Api first_error_api=R2Api::None;int32_t first_error_rc=0;
  uint64_t first_error_at_us=0,scan_retry_deadline_us=0,adv_retry_deadline_us=0;
  periodic_rx::Stats rx_capture{};
  // Membership is separate from original source permission and precision.
  bool group_member=false;uint64_t group_id=0,client_identity=0,joined_peer=0,joined_boot=0;
  uint32_t key_epoch=0,join_attempts=0,join_accepts=0,join_rejected=0,join_busy=0;
  uint8_t client_capabilities=0,client_control=0,g1_gatt_stage=0,g1_last_gatt_stage=0;
  uint32_t g1_pending_disconnects=0,g1_pending_link_teardowns=0,g1_cancel_calls=0;
  int32_t g1_cancel_rc=0,g1_last_disconnect_reason=0;bool g1_sdk_connect_pending=false;uint64_t g1_last_disconnect_at_us=0;
  uint32_t g1_adv_callbacks=0,g1_adv_incomplete=0,g1_adv_long=0,g1_adv_nonrandom=0,g1_discovery_checked=0,g1_group_records=0;
  bool g1_sdk_scan_active=false;uint64_t g1_last_adv_at_us=0,g1_last_group_id=0,g1_last_group_at_us=0;uint32_t g1_last_key_epoch=0;
  uint64_t g1_anchor_deadline_us=0,g1_anchor_guard_us=0,g1_window_start_us=0,g1_window_closed_us=0,g1_window_max_owned_us=0;
  uint32_t g1_windows=0,g1_window_misses=0,g1_quiet_faults=0,g1_refresh_attempts=0,g1_relay_authenticated=0,g1_relay_refused=0;
  uint8_t g1_listening_percent=0;bool g1_refresh_needed=false,g1_refresh_urgent=false;
  bool rc7_exclude_direct_gm=false,rc7_bound=false;uint8_t rc7_depth=0;
  bool rc7_relay_only=false,rc7_relay_only_requested=false,rc7_upstream_mode_pending=false;
  char rc7_path_hex[179]{}; // Canonical89-byte admitted-path diagnostic, no authority.
  int16_t g1_parent_score=0,g1_parent_best=0;
  uint8_t g1_parent_entries=0,g1_parent_reason=0;
  uint16_t rc7_delivery=0;uint32_t rc7_capabilities=0,rc7_requests=0,rc7_responses=0,rc7_pending=0;
  uint64_t rc7_lease_until_us=0,rc7_renew_at_us=0;
  RcParentDiagnostic rc_parent{};
  uint32_t rc7_server_flags=0,rc7_server_publication=0,rc7_server_admissions=0;
  uint32_t rc7_admission_overlaps=0,rc7_quarantine_active=0,rc7_quarantine_timeouts=0;
  uint8_t g1_first_fault_site=0;
  uint32_t host_stack_min_bytes=UINT32_MAX,host_stack_configured_bytes=0;
  uint32_t discipline_accepted=0,discipline_refused=0,rms_ns=UINT32_MAX,precision_ns=UINT32_MAX,hold_ms=0,hold_samples=0;
  int32_t trim_mppb=0;bool precision_fit=false;
  uint64_t model_available20=0;ClockPrediction precision_model{};
  // G1 read-only freshness fence; no framed-body expansion or authority renewal.
  uint64_t g1_coarse_snapshot_utc_us=0,g1_model_until_us=0;
  // Origin of the currently valid held clock; never live GM authority.
  uint64_t g1_clock_origin=0;
  G1ClockPrediction g1_clock_model{};uint32_t g1_corrected_accepted=0,g1_corrected_refused=0,g1_frequency_pairs=0,g1_frequency_scatter_ns=UINT32_MAX;
  uint64_t g1_corrected_until_us=0,g1_corrected_original20=0;
  bool g1_frequency_trained=false;
  bool g1_retained_estimate=false;uint64_t g1_estimate_observed_us=0;
  G1AdmissionPolicy g1_admission_policy{};G1ReceiveLoss g1_receive_loss{};
  G1RetainedEstimate g1_retained_clock{}; // Own scope; never the current authority.
  uint32_t g1_clock_predictions=0;uint64_t g1_clock_causal_squares=0,g1_clock_nominal_squares=0,g1_clock_causal_max_ns=0;
  uint32_t g1_clock_discontinuities=0;uint64_t g1_clock_last_discontinuity_ns=0,g1_clock_fit_tick20=0;
  uint16_t g1_fit_points=0,g1_fit_span_ms=0;uint8_t g1_fit_reason=0;
  uint32_t g1_quality_missing=0;
  G1RelativeQuality g1_relative_quality{};
  G1ResetRecovery g1_recovery{};
  G1Diagnostics g1_diagnostics{};
  uint32_t discipline_first_refusal=0,discipline_last_refusal=0;
  uint32_t discipline_reason_counts[16]{};
};
// A local retained HOLD is not a newly delivered/current GPS witness.
inline bool r2_clock_witness(const R2Status& s){return s.requested&&!s.fault&&s.clock_valid&&
  !s.g1_retained_estimate&&s.g1_clock_origin&&s.g1_clock_model.tick;}
struct PpsComparison {bool valid=false;int64_t modulo_ns=0;};
inline PpsComparison r2_shadow_pps(const R2Status& mesh,uint64_t edge20,uint64_t current20,bool mapped){
  PpsComparison score;
  // The already published BLE model must precede this physical edge. The
  // witness cannot train, refit or select a new model to score its own PPS.
  if(!mapped||!edge20||current20<edge20||current20-edge20>30000000||!mesh.requested||mesh.fault||
     !mesh.precision_fit||!mesh.following||!mesh.model_available20||mesh.model_available20>edge20||
     !mesh.precision_model.tick||mesh.precision_model.tick>edge20)return score;
  const int64_t predicted=mesh.precision_model.at(edge20);if(predicted<=0)return score;
  int64_t phase=predicted%INT64_C(1000000000);if(phase>=500000000)phase-=1000000000;
  score.valid=true;score.modulo_ns=phase;return score;
}
bool r2_init(uint64_t boot,const uint8_t factory[6]);
bool r2_host_init();
bool r2_host_sync();
void r2_host_poll();
#if V12_R2_CONFIG == 13
void r2_host_cleanup();
#endif
void r2_host_reset();
#if V12_R2_CONFIG == 13
void r2_host_reset(int reason);
#endif
bool r2_request(R2Mode mode,uint32_t duration_ms,uint64_t id);
bool r2_client_start(uint64_t id);
#if defined(V12_R2_CONFIG) && V12_R2_CONFIG == 13
// Select only while actual all-owner OFF gates admit a new client generation.
// An active service must finish its original drain before changing profile.
bool r2_client_start_profile(uint64_t id,uint8_t listening_percent);
// Clears diagnostic pins and requests automatic upstream selection. The old
// clock/path is fenced and all owners drain within the original2s deadline
// before the worker applies the mode. No controller calls occur in this API.
bool r2_set_relay_only(bool enabled);
bool r2_parent_event(uint32_t sequence,G1ParentEvent& event);
bool r2_parent_next(uint32_t after,G1ParentEvent& event);
struct R2Activity {
  uint32_t rx=0,tx=0;
  bool requested=false,fault=false,mode_pending=false,relay_only=false,tx_armed=false;
};
// Lightweight published GAP callback activity for UI polling. TX is a PA
// controller callback counter, not actual RF timing; animate only if tx_armed.
// relay_only is requested intent, including while the applied mode drains.
R2Activity r2_activity();
bool r2_rc_parent(uint64_t identity);
bool r2_gm_parent(uint64_t identity);
#endif
void r2_stop();
void r2_observe(uint64_t now);
R2Status r2_status();
// Main-task GNSS observer only. The timebase conversion is coarse, never
// physical BLE capture or a claim that a PPS edge has a UTC second label.
void r2_nmea(const uint8_t* bytes,unsigned size,uint64_t now_us);
void r2_pps(uint64_t mapped20,uint64_t current20,uint64_t now_us,bool mapped);
}
