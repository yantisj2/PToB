// N30 adaptation of the selected public W12 entry point.
// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "native_transport.hpp"
#include "native_device_binding.hpp"
#include "capture_contract.hpp"
#include "bounded_log.hpp"
#include "public_observer.hpp"
#include "native_lora.hpp"
#include "native_direct.hpp"
#include "native_diagnostic_tx.hpp"
#ifdef V12_R2_ENABLED
#include "native_r2.hpp"
#include "native_dashboard.hpp"
#include "g1_diagnostic_render.hpp"
#include "g1_record_budget.hpp"
#if V12_R2_CONFIG == 13 && PTOB_GNSS_INDICATORS && !defined(V12_GPS_SHADOW_OBSERVER)
#define PTOB_G1_PPS_DIAGNOSTIC 1
#include "g1_pps_diagnostic.hpp"
#endif
#if V12_R2_CONFIG == 13
#include "g1_ui_indicators.hpp"
#include "rc7_provisional_config.hpp"
#endif
#ifdef V12_GPS_SHADOW_OBSERVER
#include "g1_gps_shadow.hpp"
#endif
#endif
#include "update_gate.hpp"
#include "fault_watchdog.hpp"
#include "driver/gptimer.h"
#include "s3_clock_reference.h"
#include "driver/gpio.h"
#include "n30_console.hpp"
#include "n30_display.hpp"
#include "n30_lora.hpp"
#include "sync_watchdog.hpp"
#include "timing_diagnostics_api.hpp"
#include "timing_command.hpp"
#include "fine_capture.hpp"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gnss_pps.hpp"
#include "w12_device_profile.hpp"
#include "display.hpp"
#include "button_input.hpp"
#include <cstdio>
#include <cstring>
#include <cinttypes>
// ESP-IDF Xtensa reports high-water marks in bytes. Keep the historical
// main_stack_free_words console key compatible; its value is bytes here.
static_assert(sizeof(StackType_t)==1,"W12 main stack watermark unit changed");
namespace {
gptimer_handle_t clock_timer=nullptr;
uint64_t session=0,last_ui=0,console_bytes=0,console_work=0;
uint32_t console_max_us=0,commands_rejected=0,physical_pps=0;
char factory_text[18]{};
uint8_t page=display_pages::Front;
bool identity=false,gps_ready=false,ui_ready=false;
#ifdef V12_R2_ENABLED
v12::R2Dashboard dashboard;
uint64_t last_g1_diagnostic=0;
unsigned g1_diagnostic_peer=0;
uint32_t g1_parent_history_sequence=0;uint64_t g1_parent_history_capture=0;
v12::G1RecordBudget g1_record_budget;
#if V12_R2_CONFIG == 13
v12::G1UiIndicators indicator_model{bool(PTOB_GNSS_INDICATORS)};
uint64_t last_indicator_us=0;
bool ui_subus=false,ui_gps_present=false;
#endif
#ifdef V12_GPS_SHADOW_OBSERVER
v12::G1GpsShadow gps_comparison;bool shadow_capture=false;unsigned shadow_dump=0;
#endif
#endif
char ui_follow[24]="NONE";
const char* ui_state="OFF";
uint32_t ui_utc=0;
bool ui_valid=false;
uint8_t ui_hold_percentile=95;
uint32_t ui_hold_ms=0,ui_hold_samples=0;
uint32_t ui_rms_ns=UINT32_MAX;uint8_t ui_gps_lamps=0;
v12::UpdateGate update_gate;
v12::ConsoleTicket ticket;
v12::ConsoleFrame response;
uint64_t last_nonce=0,update_completed_us=0;bool lora_diagnostic_response=false;
bool timing_diagnostic_response=false;uint32_t timing_after=0;
bool fine_diagnostic_response=false;uint32_t fine_after=0;uint64_t fine_factory=0;
struct CommandResult {uint64_t nonce=0,admitted_us=0;bool accepted=false;const char* action="STATUS";const char* reason="READ_ONLY";} last_command;
uint64_t ticks(){uint64_t t=0;return clock_timer&&gptimer_get_raw_count(clock_timer,&t)==ESP_OK?t:0;}
uint64_t micros(){const int64_t t=esp_timer_get_time();return t>0?uint64_t(t):0;}
#ifdef PTOB_G1_PPS_DIAGNOSTIC
v12::G1PpsDiagnostic production_pps_diagnostic;
// Complete an independent model snapshot before consuming queued physical PPS.
// Uncertain activation, later models and original expiry remain unscorable.
[[gnu::noinline]] void observe_production_pps_diagnostic(){
  const auto before20=ticks();const auto witness=v12::r2_status();const auto seen20=ticks();
  production_pps_diagnostic.observe(witness,before20,seen20,micros());
}
[[gnu::noinline]] void score_production_pps_diagnostic(const gnss_pps::Observation& edge){
  const auto ref=gnss_pps::reference();
  const auto score=production_pps_diagnostic.edge(edge.tick,edge.number,edge.map_bracket_ns,edge.mapped,
    {ref.rmc20,ref.second,ref.epoch,ref.fix&&ref.coherent>=3});
  const auto& m=score.map;const auto& r=score.row;
  const auto output_phase=v12::G1PpsDiagnostic::phase(score.utc_ns);
  const auto receiver_phase=score.map_valid?v12::G1PpsDiagnostic::phase(score.utc_ns-m.clock.output_correction_ns):0;
  const bool rate_known=m.clock.tick&&std::isfinite(m.clock.rate)&&std::abs(m.clock.rate)<=50000;
  const auto rate_mppb=rate_known?int64_t(std::llround(m.clock.rate*1000)):0;
  std::printf("W12_G1_PPS_SHADOW number=%" PRIu32 " edge20=%" PRIu64 " raw80=%" PRIu64 " enclosure_ns=%" PRIu32
    " map_valid=%u reason=%u reference=%u following=%u permission=%u session=%" PRIu32
    " root=%016" PRIx64 " root_boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32 " scope=%" PRIu32
    " model20=%" PRIu64 " activation_lo20=%" PRIu64 " known20=%" PRIu64 " until20=%" PRIu64 " permission20=%" PRIu64
    " epoch_ns=%" PRId64 " phase_left_ns=%" PRId64 " rate_mppb=%" PRId64 " rate_known=%u UTC_offset_ns=%" PRId64
    " relative_phase_ns=%" PRId32 " receiver_phase_ns=%" PRId32 " output_phase_ns=%" PRId32 " output_correction_ns=%" PRId32
    " GPS_epoch=%" PRIu32 " rmc20=%" PRIu64 " rmc_second=%" PRIu32
    " clock=PRODUCTION_G1_ACTUATOR association=EMPIRICAL_UNCALIBRATED RF_bound=UNKNOWN UTC_bound=UNKNOWN BLE_training=NONE\n",
    edge.number,edge.tick,edge.raw_extended,edge.map_bracket_ns,unsigned(score.map_valid),unsigned(r.reason),unsigned(r.reference),
    unsigned(r.following),unsigned(r.permission),r.session,m.root,m.boot,m.source,m.train,m.scope,m.clock.tick,m.lo,m.active,m.until,m.permission,
    m.clock.epoch,m.clock.phase,rate_mppb,unsigned(rate_known),m.clock.utc_offset,
    r.phase_ns,receiver_phase,output_phase,m.clock.output_correction_ns,ref.epoch,ref.rmc20,ref.second);
}
#endif
#ifdef V12_GPS_SHADOW_OBSERVER
// The large status snapshot must not remain in app_main's permanent frame
// while console/UI/PPS consumers run. Preserve the same availability bracket.
[[gnu::noinline]] void observe_gps_actuator(){
  const auto before20=ticks();const auto witness=v12::r2_status();const auto seen20=ticks();const auto observed_us=micros();
  const auto deadline20=[&](uint64_t until){return until>observed_us&&until-observed_us<UINT64_MAX/20&&before20<=UINT64_MAX-(until-observed_us)*20?before20+(until-observed_us)*20:before20;};
  v12::G1GpsShadow::Map map;map.clock=witness.g1_clock_model;map.root=witness.g1_clock_origin;
  map.boot=witness.root_boot;map.source=witness.source_epoch;map.train=witness.publication;map.scope=witness.g1_relative_quality.scope_generation;
  map.valid=v12::r2_clock_witness(witness);
  map.following=witness.following;map.until=deadline20(witness.g1_anchor_deadline_us);map.permission=deadline20(witness.g1_corrected_until_us);
  if(map.valid&&!map.following){const auto prior=gps_comparison.latest();if(prior&&prior->root==map.root&&prior->scope==map.scope){map.boot=prior->boot;map.source=prior->source;map.train=prior->train;}else map.valid=false;}
  gps_comparison.observe(map,before20,seen20,witness.g1_anchor_deadline_us,witness.g1_corrected_until_us);
  // Restore the existing one-shot L76K aid using this same authenticated
  // snapshot. The GNSS consumer checks freshness, uncertainty, original
  // permission and its TX deadline; all resulting PPS retain aided provenance.
  if(!update_gate.latched()&&witness.requested&&!witness.fault&&witness.following&&witness.group_member)
    gnss_pps::aid_time({true,witness.follower_utc_us,witness.follower_snapshot_us,witness.follower_received_us,
      witness.service_until_us,witness.root_identity,witness.root_boot,uint32_t(witness.g1_anchor_guard_us),witness.source_epoch,witness.publication},micros());
}
#endif
void pps(const gnss_pps::Observation& edge){
#ifdef V12_GPS_SHADOW_OBSERVER
  const auto ref=gnss_pps::reference();
  const auto score=gps_comparison.edge(edge.tick,edge.number,edge.map_bracket_ns,edge.mapped,{ref.rmc20,ref.second,ref.epoch,ref.fix&&ref.coherent>=3});
  const auto& m=score.map;const auto& r=score.row;
  std::printf("W12_GPS_PPS_SHADOW number=%" PRIu32 " edge20=%" PRIu64 " raw80=%" PRIu64 " enclosure_ns=%" PRIu32
    " reason=%u map_valid=%u reference=%u following=%u permission=%u session=%" PRIu32
    " root=%016" PRIx64 " root_boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32 " scope=%" PRIu32
    " model20=%" PRIu64 " activation_lo20=%" PRIu64 " known20=%" PRIu64 " until20=%" PRIu64
    " epoch_ns=%" PRId64 " phase_left_ns=%" PRId64 " rate_mppb=%" PRId64 " relative_ns=%" PRId64 " utc_ns=%" PRId64 " phase_ns=%" PRId32
    " GPS_epoch=%" PRIu32 " rmc20=%" PRIu64 " rmc_second=%" PRIu32
    " association=UNKNOWN calibration=UNQUALIFIED BLE_training=NONE aid_this_boot=%u prior_aid=UNKNOWN\n",
    edge.number,edge.tick,edge.raw_extended,edge.map_bracket_ns,unsigned(r.reason),unsigned(score.map_valid),unsigned(r.reference),unsigned(r.following),unsigned(r.permission),r.session,
    m.root,m.boot,m.source,m.train,m.scope,m.clock.tick,m.lo,m.active,m.until,m.clock.epoch,m.clock.phase,int64_t(std::llround(m.clock.rate*1000)),score.relative_ns,score.utc_ns,r.phase_ns,
    ref.epoch,ref.rmc20,ref.second,unsigned(gnss_pps::ble_aided()));
#elif defined(V12_R2_ENABLED)
  const auto followed=v12::r2_status();const auto comparison=v12::r2_shadow_pps(followed,edge.tick,ticks(),edge.mapped);
  std::printf("W12_R2_PPS_SHADOW number=%" PRIu32 " mapped20=%" PRIu64 " model_available20=%" PRIu64
    " root_identity=%016" PRIx64 " root_boot=%016" PRIx64 " source_epoch=%" PRIu32 " publication=%" PRIu32
    " BLE_prediction_valid=%u modulo_error_ns=%" PRId64 " association=UNKNOWN bias=UNKNOWN absolute_UTC=UNQUALIFIED BLE_training=NONE GNSS_aided_this_boot=%u\n",
    edge.number,edge.tick,followed.model_available20,followed.root_identity,followed.root_boot,followed.source_epoch,followed.publication,unsigned(comparison.valid),comparison.modulo_ns,unsigned(gnss_pps::ble_aided()));
  v12::r2_pps(edge.tick,ticks(),micros(),edge.mapped);
#ifdef PTOB_G1_PPS_DIAGNOSTIC
  score_production_pps_diagnostic(edge);
#endif
#endif
  if(physical_pps!=UINT32_MAX)++physical_pps;
  std::printf("W12_V12_PPS number=%" PRIu32 " physical_raw80=%" PRIu32 " raw_extended80=%" PRIu64
    " mapped20=%" PRIu64 " map_available20=%" PRIu64 " enclosure_ns=%" PRIu32
    " mapped=%u physical_bias=UNKNOWN independent_output=UNAVAILABLE BLE_training=NONE\n",
    edge.number,edge.capture,edge.raw_extended,edge.tick,edge.anchor_available,edge.map_bracket_ns,unsigned(edge.mapped));
}
[[gnu::noinline]] void lora_status_frame(uint64_t nonce,uint64_t begin){
 const auto next=ticket.issue(begin);v12::frame_begin(response,nonce,next,begin,factory_text,session,V12_SOURCE_REVISION,V12_BUILD_ID);
 const auto mesh=v12::r2_status();
  response.format("W12_R2_STATUS profile=%s enrollment_sha=%s self=3 client_capabilities=0 client_control=0 source=0 serving=0 group_id=%016" PRIx64 " key_epoch=%" PRIu32 " client_identity=%016" PRIx64 " following=%u requested=%u fault=%u pcfg=%08" PRIx32 "\n",V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA,mesh.group_id,mesh.key_epoch,mesh.client_identity,unsigned(mesh.following),unsigned(mesh.requested),unsigned(mesh.fault),uint32_t(v12::rc7_provisional_config().id>>32));
 response.format("W12_V12_STATUS factory=%s source=%s build=%s session=%016" PRIx64 " GPS_inputs=0\n",factory_text,V12_SOURCE_REVISION,V12_BUILD_ID,session);
 if(fine_diagnostic_response)n30_fine_frame(response,fine_after,begin);else if(timing_diagnostic_response)n30_timing_frame(response,timing_after,begin);else n30_lora_diagnostics(response);
 response.format("W12_V12_COMMAND nonce=%016" PRIx64 " action=%s accepted=%u reason=%s admitted_us=%" PRIu64 " NVS=PRESERVED\n",last_command.nonce,last_command.action,unsigned(last_command.accepted),last_command.reason,last_command.admitted_us);
 n30_display_status(response);v12::frame_submit(response);
}
[[gnu::noinline]] void status(uint64_t nonce,uint64_t begin){
  const uint64_t next=ticket.issue(begin);
  v12::frame_begin(response,nonce,next,begin,factory_text,session,V12_SOURCE_REVISION,V12_BUILD_ID);const auto s=v12::transport_status();
  [[maybe_unused]] const auto gps=gnss_pps::shadow_view(ticks());
  const auto log=v12::log_status();
  const auto lora=v12::lora_status();
  const auto direct=v12::direct_status();
  const auto tx=v12::diagnostic_tx_status();
  const auto wd=v12::fault_watchdog_status();
#ifdef V12_R2_ENABLED
  const auto mesh=v12::r2_status();
#if V12_R2_CONFIG == 13
  response.format(

    "W12_R2_STATUS profile=%s enrollment_sha=%s self=%u mode=%u requested=%u scan=%u advertising=%u sync_pending=%u synced=%u connecting=%u connections=%"
    PRIu32
    " owner_busy=%u off_ack=%u fault=%u coordinator=%u root=%u standby=%u members=%u role=%u serving=%u following=%u source=%u boot_client=%u boot_client_attempted=%u boot_client_cancelled=%u group_member=%u group_id=%016"
    PRIx64 " key_epoch=%" PRIu32 " client_identity=%016" PRIx64 " joined_peer=%016" PRIx64 " joined_boot=%016" PRIx64 " join_attempts=%" PRIu32
    " join_accepts=%" PRIu32 " join_rejected=%" PRIu32 " join_busy=%" PRIu32
    " client_capabilities=%u client_control=%u g1_connection_owner_revision=3 g1_pending_disconnects=%" PRIu32 " g1_pending_link_teardowns=%" PRIu32
    " g1_cancel_calls=%" PRIu32 " g1_cancel_rc=%" PRId32 " g1_sdk_connect_pending=%u g1_last_disconnect_reason=%" PRId32 " g1_last_disconnect_at_us=%"
    PRIu64 " g1_discovery_diagnostic_revision=1 g1_adv_callbacks=%" PRIu32 " g1_adv_incomplete=%" PRIu32 " g1_adv_long=%" PRIu32 " g1_adv_nonrandom=%"
    PRIu32 " g1_discovery_checked=%" PRIu32 " g1_group_records=%" PRIu32 " g1_sdk_scan_active=%u g1_last_adv_at_us=%" PRIu64 " g1_last_group_id=%016"
    PRIx64 " g1_last_key_epoch=%" PRIu32 " g1_last_group_at_us=%" PRIu64 " root_boot=%016" PRIx64 " root_identity=%016" PRIx64 " source_epoch=%"
    PRIu32 " publication=%" PRIu32 " service_until_us=%" PRIu64
    " peer_mtu_min=%u precision_interval_units=%u precision_hz=%u schedule_descriptor=%016" PRIx64 " link_diagnostics_version=2 peer_losses=%" PRIu32
    " setup_retries=%" PRIu32 " last_api=%u last_api_rc=%" PRId32 " last_api_peer=%u last_api_at_us=%" PRIu64
    " link_mask=%u gatt_mask=%u first_error_api=%u first_error_rc=%" PRId32 " first_error_at_us=%" PRIu64 " scan_retry_deadline_us=%" PRIu64
    " adv_retry_deadline_us=%" PRIu64 " drops=%" PRIu32 " discoveries=%" PRIu32 " pa_reports=%" PRIu32 " p1_authenticated=%" PRIu32 " p1_refused=%"
    PRIu32 " api_errors=%" PRIu32 " deadline_exceeded=%u stop_generation=%" PRIu32 " stop_completed=%" PRIu32 " client_generation=%" PRIu32
    " client_request_id=%016" PRIx64 " client_pending=%u client_running=%u client_stopping=%u client_admitted_us=%" PRIu64
    " client_start_deadline_us=%" PRIu64 " client_started_us=%" PRIu64 " client_stop_requested_us=%" PRIu64 " client_stop_deadline_us=%" PRIu64
    " client_completed_us=%" PRIu64 " client_stop_generation=%" PRIu32 " client_stop_completed=%" PRIu32
    " client_deadline_exceeded=%u rx_shadow_version=1 rx_installed=%u rx_armed=%u rx_fault=%" PRIu32 " rx_calls=%" PRIu32 " rx_candidates=%" PRIu32
    " rx_matched=%" PRIu32 " rx_missed=%" PRIu32 " rx_ambiguous=%" PRIu32 " rx_overflow=%" PRIu32 " rx_epoch=%" PRIu32 " rx_peak=%" PRIu32
    " rx_hook_max_cycles=%" PRIu32 " host_stack_min_bytes=%" PRIu32 " host_stack_configured_bytes=%" PRIu32 " discipline_accepted=%" PRIu32
    " discipline_refused=%" PRIu32 " rms_ns=%" PRIu32 " hold_p99_ms=%" PRIu32 " hold_p99_samples=%" PRIu32
    " anchor_deadline_us=%" PRIu64 " anchor_guard_us=%" PRIu64 " refresh_needed=%u refresh_urgent=%u refresh_attempts=%" PRIu32
    " window_start_us=%" PRIu64 " window_closed_us=%" PRIu64 " window_max_owned_us=%" PRIu64 " windows=%" PRIu32 " window_misses=%" PRIu32
    " quiet_faults=%" PRIu32 " GR_authenticated=%" PRIu32 " GR_refused=%" PRIu32
    " rc7=2/%u/%u/%u rq7=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 " rc7_until=%" PRIu64 "/%" PRIu64 " rc7_delivery=%u rp=%016" PRIx64 "/%" PRIu32 "/%u/%" PRIu64 "/%" PRIu64 "/%u"
    " listening_percent=%u g1_rearm=%" PRIu32 " g1_join=%" PRIu32 "/%" PRIu32 " cal=%d/%u pcfg=%08" PRIx32
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
    " rs7=%08" PRIx32 "/%08" PRIx32 "/%08" PRIx32 " rcq=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%u"
#endif
    " path7=%s sel7=%u/%d/%d/%u"
    " event_capture=SHADOW_ONLY precision=UNAVAILABLE RF_quiet=UNKNOWN WiFi=OFF PTS=OFF\n",
    V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA,unsigned(mesh.self),unsigned(mesh.mode),unsigned(mesh.requested),unsigned(mesh.scan),unsigned(mesh.advertising),unsigned(mesh.sync_pending),unsigned(mesh.synced),unsigned(mesh.connecting),mesh.connections,unsigned(mesh.owner_busy),
    unsigned(mesh.off_ack),unsigned(mesh.fault),unsigned(mesh.coordinator),unsigned(mesh.root),unsigned(mesh.standby),unsigned(mesh.members),unsigned(mesh.role),unsigned(mesh.serving),unsigned(mesh.following),unsigned(mesh.source),unsigned(mesh.boot_client),unsigned(mesh.boot_client_attempted),
    unsigned(mesh.boot_client_cancelled),unsigned(mesh.group_member),mesh.group_id,mesh.key_epoch,mesh.client_identity,mesh.joined_peer,mesh.joined_boot,mesh.join_attempts,mesh.join_accepts,mesh.join_rejected,mesh.join_busy,unsigned(mesh.client_capabilities),
    unsigned(mesh.client_control),mesh.g1_pending_disconnects,mesh.g1_pending_link_teardowns,mesh.g1_cancel_calls,mesh.g1_cancel_rc,unsigned(mesh.g1_sdk_connect_pending),mesh.g1_last_disconnect_reason,mesh.g1_last_disconnect_at_us,mesh.g1_adv_callbacks,mesh.g1_adv_incomplete,mesh.g1_adv_long,mesh.g1_adv_nonrandom,
    mesh.g1_discovery_checked,mesh.g1_group_records,unsigned(mesh.g1_sdk_scan_active),mesh.g1_last_adv_at_us,mesh.g1_last_group_id,mesh.g1_last_key_epoch,mesh.g1_last_group_at_us,mesh.root_boot,mesh.root_identity,mesh.source_epoch,mesh.publication,mesh.service_until_us,
    unsigned(mesh.peer_mtu_min),unsigned(mesh.precision_interval_units),unsigned(mesh.precision_hz),mesh.schedule_descriptor,mesh.peer_losses,mesh.setup_retries,unsigned(mesh.last_api),mesh.last_api_rc,unsigned(mesh.last_api_peer),mesh.last_api_at_us,unsigned(mesh.link_mask),unsigned(mesh.gatt_mask),
    unsigned(mesh.first_error_api),mesh.first_error_rc,mesh.first_error_at_us,mesh.scan_retry_deadline_us,mesh.adv_retry_deadline_us,mesh.drops,mesh.discoveries,mesh.pa_reports,mesh.p1_authenticated,mesh.p1_refused,mesh.api_errors,unsigned(mesh.lease.deadline_exceeded),
    mesh.lease.stop_generation,mesh.lease.stop_completed,mesh.client.generation,mesh.client.request_id,unsigned(mesh.client.pending),unsigned(mesh.client.running),unsigned(mesh.client.stopping),mesh.client.admitted_us,mesh.client.start_deadline_us,mesh.client.started_us,mesh.client.stop_requested_us,mesh.client.stop_deadline_us,
    mesh.client.completed_us,mesh.client.stop_generation,mesh.client.stop_completed,unsigned(mesh.client.deadline_exceeded),unsigned(mesh.rx_capture.installed),unsigned(mesh.rx_capture.armed),mesh.rx_capture.fault,mesh.rx_capture.calls,mesh.rx_capture.candidates,mesh.rx_capture.matched,mesh.rx_capture.missed,mesh.rx_capture.ambiguous,
    mesh.rx_capture.overflow,mesh.rx_capture.epoch,mesh.rx_capture.peak,mesh.rx_capture.hook_max_cycles,mesh.host_stack_min_bytes,mesh.host_stack_configured_bytes,mesh.discipline_accepted,mesh.discipline_refused,mesh.rms_ns,mesh.hold_ms,mesh.hold_samples,mesh.g1_anchor_deadline_us,
    mesh.g1_anchor_guard_us,unsigned(mesh.g1_refresh_needed),unsigned(mesh.g1_refresh_urgent),mesh.g1_refresh_attempts,mesh.g1_window_start_us,mesh.g1_window_closed_us,mesh.g1_window_max_owned_us,mesh.g1_windows,mesh.g1_window_misses,mesh.g1_quiet_faults,mesh.g1_relay_authenticated,mesh.g1_relay_refused,unsigned(mesh.rc7_exclude_direct_gm),unsigned(mesh.rc7_bound),unsigned(mesh.rc7_depth),mesh.rc7_capabilities,mesh.rc7_requests,mesh.rc7_responses,mesh.rc7_pending,mesh.rc7_lease_until_us,mesh.rc7_renew_at_us,unsigned(mesh.rc7_delivery),mesh.rc_parent.target,mesh.rc_parent.generation,unsigned(mesh.rc_parent.state),mesh.rc_parent.deadline_us,mesh.rc_parent.completed_us,unsigned(mesh.rc_parent.relay),unsigned(mesh.g1_listening_percent),mesh.g1_recovery.rearms,mesh.rx,mesh.tx,int(mesh.g1_clock_model.output_correction_ns),unsigned(mesh.g1_clock_model.output_correction_ns!=0),uint32_t(v12::rc7_provisional_config().id>>32)
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
    ,mesh.rc7_server_flags,mesh.rc7_server_publication,mesh.rc7_server_admissions,mesh.rc7_admission_overlaps,mesh.rc7_quarantine_active,mesh.rc7_quarantine_timeouts,unsigned(mesh.g1_first_fault_site)
#endif
    ,mesh.rc7_path_hex,unsigned(mesh.g1_parent_entries),int(mesh.g1_parent_score),int(mesh.g1_parent_best),unsigned(mesh.g1_parent_reason)
    );
#else
  response.format("W12_R2_STATUS profile=%s enrollment_sha=%s self=%u mode=%u requested=%u scan=%u advertising=%u sync_pending=%u synced=%u connecting=%u connections=%u owner_busy=%u off_ack=%u fault=%u active_used=%u"
    " ready=%u coordinator=%u root=%u standby=%u members=%u role=%u serving=%u following=%u source=%u clock_valid=%u follower_utc_us=%" PRIu64
    " boot_client=%u boot_client_attempted=%u boot_client_cancelled=%u"
    " group_member=%u group_id=%016" PRIx64 " key_epoch=%" PRIu32 " client_identity=%016" PRIx64 " joined_peer=%016" PRIx64 " joined_boot=%016" PRIx64 " join_attempts=%" PRIu32 " join_accepts=%" PRIu32 " join_rejected=%" PRIu32 " join_busy=%" PRIu32 " client_capabilities=%u client_control=%u"
    " g1_connection_owner_revision=3 g1_pending_disconnects=%" PRIu32 " g1_pending_link_teardowns=%" PRIu32 " g1_cancel_calls=%" PRIu32 " g1_cancel_rc=%" PRId32 " g1_sdk_connect_pending=%u g1_last_disconnect_reason=%" PRId32 " g1_last_disconnect_at_us=%" PRIu64
    " g1_discovery_diagnostic_revision=1 g1_adv_callbacks=%" PRIu32 " g1_adv_incomplete=%" PRIu32 " g1_adv_long=%" PRIu32 " g1_adv_nonrandom=%" PRIu32 " g1_discovery_checked=%" PRIu32 " g1_group_records=%" PRIu32 " g1_sdk_scan_active=%u g1_last_adv_at_us=%" PRIu64 " g1_last_group_id=%016" PRIx64 " g1_last_key_epoch=%" PRIu32 " g1_last_group_at_us=%" PRIu64
    " root_boot=%016" PRIx64 " root_identity=%016" PRIx64 " source_epoch=%" PRIu32 " publication=%" PRIu32 " service_until_us=%" PRIu64 " peer_mtu_min=%u"
    " precision_interval_units=%u precision_hz=%u schedule_descriptor=%016" PRIx64
    " link_diagnostics_version=2 peer_losses=%" PRIu32 " setup_retries=%" PRIu32 " last_api=%u last_api_rc=%" PRId32 " last_api_peer=%u last_api_at_us=%" PRIu64 " link_mask=%u gatt_mask=%u"
    " first_error_api=%u first_error_rc=%" PRId32 " first_error_at_us=%" PRIu64 " scan_retry_deadline_us=%" PRIu64 " adv_retry_deadline_us=%" PRIu64
    " rx=%" PRIu32 " tx=%" PRIu32 " authenticated=%" PRIu32 " hellos=%" PRIu32 " rejected=%" PRIu32 " drops=%" PRIu32 " discoveries=%" PRIu32 " pa_reports=%" PRIu32 " p1_authenticated=%" PRIu32 " p1_refused=%" PRIu32 " api_errors=%" PRIu32
    " generation=%" PRIu32 " request_id=%016" PRIx64 " admitted_us=%" PRIu64 " deadline_us=%" PRIu64 " started_us=%" PRIu64 " completed_us=%" PRIu64 " close_deadline_us=%" PRIu64 " worker_max_us=%" PRIu64 " deadline_exceeded=%u stop_generation=%" PRIu32 " stop_completed=%" PRIu32
    " client_generation=%" PRIu32 " client_request_id=%016" PRIx64 " client_pending=%u client_running=%u client_stopping=%u client_admitted_us=%" PRIu64 " client_start_deadline_us=%" PRIu64 " client_started_us=%" PRIu64 " client_stop_requested_us=%" PRIu64 " client_stop_deadline_us=%" PRIu64 " client_completed_us=%" PRIu64 " client_stop_generation=%" PRIu32 " client_stop_completed=%" PRIu32 " client_deadline_exceeded=%u"
    " rx_shadow_version=1 rx_installed=%u rx_armed=%u rx_fault=%" PRIu32 " rx_calls=%" PRIu32 " rx_candidates=%" PRIu32 " rx_matched=%" PRIu32 " rx_missed=%" PRIu32 " rx_ambiguous=%" PRIu32 " rx_overflow=%" PRIu32 " rx_epoch=%" PRIu32 " rx_peak=%" PRIu32 " rx_hook_max_cycles=%" PRIu32
    " rx_aux_calls=%" PRIu32 " rx_aux_in_rx=%" PRIu32 " rx_aux_armed=%" PRIu32 " rx_reason=%" PRIu32 " rx_first=%" PRIu32 " rx_index=%" PRIu32 " rx_control=%" PRIu32 " rx_status=%" PRIu32 " rx_header=%" PRIu32 " rx_offset=%" PRIu32
    " host_stack_min_bytes=%" PRIu32 " host_stack_configured_bytes=%" PRIu32
    " discipline_accepted=%" PRIu32 " discipline_refused=%" PRIu32 " rms_ns=%" PRIu32 " trim_mppb=%" PRId32 " hold_p99_ms=%" PRIu32 " hold_p99_samples=%" PRIu32
    " g1_passive_revision=1 anchor_deadline_us=%" PRIu64 " anchor_guard_us=%" PRIu64 " refresh_needed=%u refresh_urgent=%u refresh_attempts=%" PRIu32
    " window_start_us=%" PRIu64 " window_closed_us=%" PRIu64 " window_max_owned_us=%" PRIu64 " windows=%" PRIu32 " window_misses=%" PRIu32 " quiet_faults=%" PRIu32 " GR_authenticated=%" PRIu32 " GR_refused=%" PRIu32
    " freshness=P1_UNKNOWN event_capture=SHADOW_ONLY precision=UNAVAILABLE PPS_UTC=UNKNOWN RF_quiet=UNKNOWN WiFi=OFF PTS=OFF\n",
    V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA,unsigned(mesh.self),unsigned(mesh.mode),unsigned(mesh.requested),unsigned(mesh.scan),unsigned(mesh.advertising),unsigned(mesh.sync_pending),unsigned(mesh.synced),unsigned(mesh.connecting),mesh.connections,unsigned(mesh.owner_busy),unsigned(mesh.off_ack),unsigned(mesh.fault),unsigned(mesh.active_used),
    unsigned(mesh.ready),unsigned(mesh.coordinator),unsigned(mesh.root),unsigned(mesh.standby),unsigned(mesh.members),unsigned(mesh.role),unsigned(mesh.serving),unsigned(mesh.following),unsigned(mesh.source),unsigned(mesh.clock_valid),mesh.follower_utc_us,
    unsigned(mesh.boot_client),unsigned(mesh.boot_client_attempted),unsigned(mesh.boot_client_cancelled),unsigned(mesh.group_member),mesh.group_id,mesh.key_epoch,mesh.client_identity,mesh.joined_peer,mesh.joined_boot,mesh.join_attempts,mesh.join_accepts,mesh.join_rejected,mesh.join_busy,unsigned(mesh.client_capabilities),unsigned(mesh.client_control),mesh.g1_pending_disconnects,mesh.g1_pending_link_teardowns,mesh.g1_cancel_calls,mesh.g1_cancel_rc,unsigned(mesh.g1_sdk_connect_pending),mesh.g1_last_disconnect_reason,mesh.g1_last_disconnect_at_us,mesh.g1_adv_callbacks,mesh.g1_adv_incomplete,mesh.g1_adv_long,mesh.g1_adv_nonrandom,mesh.g1_discovery_checked,mesh.g1_group_records,unsigned(mesh.g1_sdk_scan_active),mesh.g1_last_adv_at_us,mesh.g1_last_group_id,mesh.g1_last_key_epoch,mesh.g1_last_group_at_us,mesh.root_boot,mesh.root_identity,mesh.source_epoch,mesh.publication,mesh.service_until_us,unsigned(mesh.peer_mtu_min),
    unsigned(mesh.precision_interval_units),unsigned(mesh.precision_hz),mesh.schedule_descriptor,
    mesh.peer_losses,mesh.setup_retries,unsigned(mesh.last_api),mesh.last_api_rc,unsigned(mesh.last_api_peer),mesh.last_api_at_us,unsigned(mesh.link_mask),unsigned(mesh.gatt_mask),unsigned(mesh.first_error_api),mesh.first_error_rc,mesh.first_error_at_us,mesh.scan_retry_deadline_us,mesh.adv_retry_deadline_us,
    mesh.rx,mesh.tx,mesh.authenticated,mesh.hellos,mesh.rejected,mesh.drops,mesh.discoveries,mesh.pa_reports,mesh.p1_authenticated,mesh.p1_refused,mesh.api_errors,
    mesh.lease.generation,mesh.lease.request_id,mesh.lease.admitted_us,mesh.lease.deadline_us,mesh.lease.started_us,mesh.lease.completed_us,mesh.close_deadline_us,mesh.worker_max_us,unsigned(mesh.lease.deadline_exceeded),mesh.lease.stop_generation,mesh.lease.stop_completed,
    mesh.client.generation,mesh.client.request_id,unsigned(mesh.client.pending),unsigned(mesh.client.running),unsigned(mesh.client.stopping),mesh.client.admitted_us,mesh.client.start_deadline_us,mesh.client.started_us,mesh.client.stop_requested_us,mesh.client.stop_deadline_us,mesh.client.completed_us,mesh.client.stop_generation,mesh.client.stop_completed,unsigned(mesh.client.deadline_exceeded),
    unsigned(mesh.rx_capture.installed),unsigned(mesh.rx_capture.armed),mesh.rx_capture.fault,mesh.rx_capture.calls,mesh.rx_capture.candidates,mesh.rx_capture.matched,mesh.rx_capture.missed,mesh.rx_capture.ambiguous,mesh.rx_capture.overflow,mesh.rx_capture.epoch,mesh.rx_capture.peak,mesh.rx_capture.hook_max_cycles,
    mesh.rx_capture.aux_calls,mesh.rx_capture.aux_in_rx,mesh.rx_capture.aux_armed,mesh.rx_capture.reason,mesh.rx_capture.first,mesh.rx_capture.index,
    mesh.rx_capture.control,mesh.rx_capture.status,mesh.rx_capture.header,mesh.rx_capture.offset,
    mesh.host_stack_min_bytes,mesh.host_stack_configured_bytes,
    mesh.discipline_accepted,mesh.discipline_refused,mesh.rms_ns,mesh.trim_mppb,mesh.hold_ms,mesh.hold_samples,mesh.g1_anchor_deadline_us,mesh.g1_anchor_guard_us,unsigned(mesh.g1_refresh_needed),unsigned(mesh.g1_refresh_urgent),mesh.g1_refresh_attempts,mesh.g1_window_start_us,mesh.g1_window_closed_us,mesh.g1_window_max_owned_us,mesh.g1_windows,mesh.g1_window_misses,mesh.g1_quiet_faults,mesh.g1_relay_authenticated,mesh.g1_relay_refused);
#endif
#endif
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  response.format("W12_V12_STATUS factory=%s session=%016" PRIx64 " source=%s build=%s monotonic_us=%" PRIu64
    " lifecycle=%u requested=%u scan=%u pending=%u sync=%u host_busy=%u command_pending=%u off_ack=%u host_ready=%u GPS_inputs=%u queue_drops=%" PRIu32 " oversize=%" PRIu32 " api_errors=%" PRIu32 " lease_faults=%" PRIu32
    " rx_generation=%" PRIu32 " rx_id=%016" PRIx64 " rx_admitted_us=%" PRIu64 " rx_deadline_us=%" PRIu64 " rx_started_us=%" PRIu64 " rx_completed_us=%" PRIu64
    " rx_stop_generation=%" PRIu32 " rx_stop_completed=%" PRIu32 " rx_deadline_exceeded=%u"
    " heap_free=%u main_stack_free_words=%u ui_page=%u ui_ready=%u ui_valid=%u ui_utc=%" PRIu32 " ui_state=%s ui_follow=%s"
    " ui_hold_percentile=%u ui_hold_ms=%" PRIu32 " ui_hold_samples=%" PRIu32 " ui_rms_ns=%" PRIu32
    " authority=NONE precision=UNAVAILABLE WiFi=OFF PTS=OFF\n",
    factory_text,session,V12_SOURCE_REVISION,V12_BUILD_ID,begin,unsigned(s.client.state),unsigned(s.radio_requested),unsigned(s.client.scan),unsigned(s.client.pending),unsigned(s.client.synced),unsigned(s.host_work_busy),unsigned(s.command_pending),unsigned(s.off_acknowledged),unsigned(s.host_ready),unsigned(gps_ready),s.queue_drops,s.oversize,s.api_errors,s.client.faults,
    s.lease.generation,s.lease.request_id,s.lease.admitted_us,s.lease.deadline_us,s.lease.started_us,s.lease.completed_us,s.lease.stop_generation,s.lease.stop_completed,unsigned(s.lease.deadline_exceeded),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(uxTaskGetStackHighWaterMark(nullptr)),unsigned(page),unsigned(ui_ready),unsigned(ui_valid),ui_utc,ui_state,ui_follow,ui_hold_percentile,ui_hold_ms,ui_hold_samples,ui_rms_ns);
#else
  response.format("W12_V12_STATUS factory=%s session=%016" PRIx64 " source=%s build=%s monotonic_us=%" PRIu64 " mode=%u lifecycle=%u"
    " requested=%u scan=%u pending=%u sync=%u host_busy=%u command_pending=%u off_ack=%u cycle=%" PRIu32 " callbacks=%" PRIu32
    " incomplete=%" PRIu32 " oversize=%" PRIu32 " late=%" PRIu32 " queue_drops=%" PRIu32
    " queue_peak=%" PRIu32 " api_errors=%" PRIu32 " api_max_us=%" PRIu64
    " lease_faults=%" PRIu32 " host_ready=%u GPS_inputs=%u GPS_coarse=%u GPS_PPS=%" PRIu32
    " heap_free=%u heap_min=%u main_stack_free_words=%u console_bytes=%" PRIu64
    " console_work_us=%" PRIu64 " console_max_us=%" PRIu32 " commands_rejected=%" PRIu32
    " rx_generation=%" PRIu32 " rx_id=%016" PRIx64 " rx_admitted_us=%" PRIu64 " rx_deadline_us=%" PRIu64
    " rx_started_us=%" PRIu64 " rx_completed_us=%" PRIu64 " rx_stop_generation=%" PRIu32 " rx_stop_completed=%" PRIu32 " rx_deadline_exceeded=%u"
    " timestamp=TASK_DIAGNOSTIC physical_capture=UNSUPPORTED event_counter=UNSUPPORTED"
    " payload_binding=UNSUPPORTED phase=UNSUPPORTED RF_listening=UNKNOWN RF_quiet=UNKNOWN"
    " authority=NONE precision=UNAVAILABLE LoRa_transport=SEPARATE WiFi=OFF PTS=OFF"
    " ui_page=%u ui_ready=%u ui_valid=%u ui_utc=%" PRIu32 " ui_state=%s ui_follow=%s"
    " ui_hold_percentile=%u ui_hold_ms=%" PRIu32 " ui_hold_samples=%" PRIu32 " ui_rms_ns=%" PRIu32 "\n",
    factory_text,session,V12_SOURCE_REVISION,V12_BUILD_ID,begin,unsigned(s.client.mode),unsigned(s.client.state),
    unsigned(s.radio_requested),unsigned(s.client.scan),unsigned(s.client.pending),unsigned(s.client.synced),unsigned(s.host_work_busy),unsigned(s.command_pending),unsigned(s.off_acknowledged),
    s.client.cycle,s.reports,s.incomplete,s.oversize,s.late,s.queue_drops,s.queue_peak,s.api_errors,s.max_api_us,
    s.client.faults,unsigned(s.host_ready),unsigned(gps_ready),unsigned(gps.valid),physical_pps,
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
    unsigned(uxTaskGetStackHighWaterMark(nullptr)),console_bytes,console_work,console_max_us,commands_rejected,s.lease.generation,s.lease.request_id,s.lease.admitted_us,s.lease.deadline_us,
    s.lease.started_us,s.lease.completed_us,s.lease.stop_generation,s.lease.stop_completed,unsigned(s.lease.deadline_exceeded),
    unsigned(page),unsigned(ui_ready),unsigned(ui_valid),ui_utc,ui_state,ui_follow,ui_hold_percentile,ui_hold_ms,ui_hold_samples,ui_rms_ns);
#endif
  const uint64_t work=micros()-begin;console_work+=work;
  if(work>console_max_us)console_max_us=uint32_t(work);
  // These peripherals are absent/disabled in this target. Compact literal
  // rows reserve the display row within the unchanged shared whole-frame cap.
  response.format("W12_V12_GPS inputs=0 coarse=0 PPS=0 GPS=ABSENT\n");
  response.format("W12_V12_LORA_STATUS enabled=%u initialized=%u failed=%u requested=%u worker_busy=%u off_ack=%u errors=%" PRIu32 " TX=OFF\n",
    unsigned(lora.enabled),unsigned(lora.initialized),unsigned(lora.failed),unsigned(lora.requested),unsigned(lora.worker_busy),unsigned(lora.off_acknowledged),lora.radio_errors);
  const auto public_status=v12::public_observer_status();const auto& p=public_status.records;
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  response.format("W12_V12_PUBLIC malformed=%" PRIu32 " invalid_signature=%" PRIu32 " authenticated=%" PRIu32 " queue_drops=%" PRIu32
    " usable_precision=0 trust_provisioned=0 delivered_output=UNAVAILABLE\n",
    p.malformed,p.invalid_signature,p.authenticated,public_status.drops);
#else
  response.format("W12_V12_PUBLIC malformed=%" PRIu32 " no_trusted_key=%" PRIu32 " invalid_signature=%" PRIu32
    " authenticated=%" PRIu32 " cold_freshness_unknown=%" PRIu32 " missing_parent=%" PRIu32
    " timing_no_capture=%" PRIu32 " work_max_us=%" PRIu64 " queue_drops=%" PRIu32 " queue_peak=%" PRIu32
    " usable_precision=0 trust_provisioned=0 delivered_output=UNAVAILABLE\n",
    p.malformed,p.no_trusted_key,p.invalid_signature,p.authenticated,p.freshness_unknown,p.missing_parent,p.timing_no_capture,
    public_status.work_max_us,public_status.drops,public_status.peak);
#endif
  response.format("W12_V12_CONSOLE dropped=%" PRIu32 " truncated=%" PRIu32 " isr_refused=%" PRIu32
    " peak=%" PRIu32 " transmitted_bytes=%" PRIu64 " write_errors=%" PRIu32
    " enqueue_work_us=%" PRIu64 " enqueue_max_us=%" PRIu32 " TX_timeout_ticks=0"
    " suppressed_no_capture=%" PRIu64 " expired=%" PRIu32 " frame_drops=%" PRIu32 " capture_id=%016" PRIx64
    " capture_active=%u capture_deadline_us=%" PRIu64 " log_sequence=%" PRIu64 " counter_exhausted=%u"
    " observer_effect=UNMEASURED coverage=%s\n",log.dropped,log.truncated,log.isr_refused,log.peak,
    log.transmitted,log.write_errors,log.enqueue_work_us,log.enqueue_max_us,log.suppressed_no_capture,log.expired,log.frame_drops,
    log.capture_id,unsigned(log.capture_active),log.capture_deadline_us,log.log_sequence,unsigned(log.counter_exhausted),
    (log.dropped||log.truncated||log.isr_refused)?"HISTORICAL_INCOMPLETE":"TASK_QUEUE_ONLY");
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  response.format("W12_V12_DIRECT stage=%u incarnation=%" PRIu32 " handle=%u encrypted=%u confirmed=%u"
    " api_errors=%" PRIu32 " crypto_errors=%" PRIu32 " queue_drops=%" PRIu32 " GM_eligible=0 authority=NONE\n",
    unsigned(direct.stage),direct.incarnation,unsigned(direct.handle),unsigned(direct.encrypted),unsigned(direct.confirmed),direct.api_errors,direct.crypto_errors,direct.queue_drops);
#else
  response.format("W12_V12_DIRECT stage=%u role=%u incarnation=%" PRIu32 " handle=%u mtu=%u credentials=%u"
    " encrypted=%u confirmed=%u refusals=%" PRIu32 " api_errors=%" PRIu32 " queue_drops=%" PRIu32
    " queue_peak=%" PRIu32 " crypto_errors=%" PRIu32 " crypto_max_us=%" PRIu64 " control_refused=%" PRIu32
    " grants_verified=%" PRIu32 " grant_freshness_unknown=%" PRIu32
    " GM_eligible=0 timing_key_registration=UNAVAILABLE source=UNKNOWN authority=NONE\n",
    unsigned(direct.stage),unsigned(direct.role),direct.incarnation,unsigned(direct.handle),unsigned(direct.mtu),unsigned(direct.credentials),
    unsigned(direct.encrypted),unsigned(direct.confirmed),direct.refusals,direct.api_errors,direct.queue_drops,direct.queue_peak,
    direct.crypto_errors,direct.crypto_max_us,direct.control_refused,direct.grant_verified,direct.grant_freshness_unknown);
#endif
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  response.format("W12_V12_DIAGNOSTIC_TX requested=%u periodic=%u discovery=%u fault=%u api_errors=%" PRIu32
    " pending=%u owner_busy=%u off_ack=%u generation=%" PRIu32 " request_id=%016" PRIx64
    " admitted_us=%" PRIu64 " deadline_us=%" PRIu64 " started_us=%" PRIu64 " completed_us=%" PRIu64
    " stop_generation=%" PRIu32 " stop_completed=%" PRIu32 " deadline_exceeded=%u"
    " selected_tx_power_dbm=%d selected_power_available=%u RF=UNQUALIFIED authority=NONE\n",
    unsigned(tx.requested),unsigned(tx.periodic),unsigned(tx.discovery),unsigned(tx.fault),tx.api_errors,unsigned(tx.lease.pending),unsigned(tx.owner_busy),unsigned(tx.off_acknowledged),tx.lease.generation,tx.lease.request_id,
    tx.lease.admitted_us,tx.lease.deadline_us,tx.lease.started_us,tx.lease.completed_us,tx.lease.stop_generation,tx.lease.stop_completed,unsigned(tx.lease.deadline_exceeded),int(tx.selected_tx_power),unsigned(tx.selected_power_available));
#else
  response.format("W12_V12_DIAGNOSTIC_TX requested=%u periodic=%u discovery=%u fault=%u bytes=%u"
    " api_errors=%" PRIu32 " completed=%" PRIu32 " PA_us=31250 discovery_us=31250+advDelay"
    " run=%" PRIu32 " pending=%u owner_busy=%u off_ack=%u generation=%" PRIu32 " request_id=%016" PRIx64
    " admitted_us=%" PRIu64 " deadline_us=%" PRIu64 " started_us=%" PRIu64 " completed_us=%" PRIu64
    " stop_generation=%" PRIu32 " stop_completed=%" PRIu32 " deadline_exceeded=%u"
    " requested_tx_power_dbm=0 selected_tx_power_dbm=%d selected_power_available=%u public_address_available=%u public_address_le=%02x%02x%02x%02x%02x%02x"
    " primary_phy=1 secondary_phy=1 sid=0 PA_interval=25 discovery_interval=50"
    " completion_reason_available=%u completion_reason=%d reported_completed_events=%u completed_events_available=%u"
    " controller_reports=NOT_RF_EMISSION"
    " diagnostic_kinds=F0/F1 single_PDU=UNPROVEN phase=UNSUPPORTED physical_capture=UNAVAILABLE authority=NONE\n",
    unsigned(tx.requested),unsigned(tx.periodic),unsigned(tx.discovery),unsigned(tx.fault),unsigned(tx.bytes),tx.api_errors,tx.completed,tx.run,unsigned(tx.lease.pending),unsigned(tx.owner_busy),unsigned(tx.off_acknowledged),
    tx.lease.generation,tx.lease.request_id,tx.lease.admitted_us,tx.lease.deadline_us,tx.lease.started_us,tx.lease.completed_us,
    tx.lease.stop_generation,tx.lease.stop_completed,unsigned(tx.lease.deadline_exceeded),
    int(tx.selected_tx_power),unsigned(tx.selected_power_available),unsigned(tx.public_address_available),tx.public_address_le[0],tx.public_address_le[1],tx.public_address_le[2],tx.public_address_le[3],tx.public_address_le[4],tx.public_address_le[5],
    unsigned(tx.completion_reason_available),tx.completion_reason,unsigned(tx.reported_completed_events),unsigned(tx.completed_events_available));
#endif
  response.format("W12_V12_COMMAND nonce=%016" PRIx64 " action=%s accepted=%u reason=%s admitted_us=%" PRIu64
    " update_stage=%u update_deadline_us=%" PRIu64 " update_completed_us=%" PRIu64
    " application_drained=%u wd=%u/%08" PRIx32 "/%" PRIu64 "/%" PRIu64 "/%08" PRIx32 "/%" PRIu32
    "/%016" PRIx64 "/%" PRIu64 "/%u/%" PRId32 " wdpin=%016" PRIx64 "/%u/%u wdtest=%u"
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    " um=%u/%u/%u uii=%u/%u/%u"
#endif
    " physical_quiet=UNKNOWN NVS=PRESERVED\n",
    last_command.nonce,last_command.action,unsigned(last_command.accepted),last_command.reason,last_command.admitted_us,
    unsigned(update_gate.stage),update_gate.deadline,update_completed_us,unsigned(update_gate.stage==v12::UpdateStage::Ready),
    unsigned(wd.enabled),wd.mask,wd.since_us,wd.elapsed_us,wd.previous_mask,wd.restarts,wd.previous_boot,
    wd.previous_elapsed_us,unsigned(wd.previous_api),wd.previous_rc,wd.parent,unsigned(wd.parent_relay),unsigned(wd.parent_pending),unsigned(wd.test)
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    ,unsigned(mesh.rc7_relay_only),unsigned(mesh.rc7_relay_only_requested),unsigned(mesh.rc7_upstream_mode_pending),unsigned(ui_subus),unsigned(ui_gps_present),unsigned(ui_gps_lamps)
#endif
    );
  n30_display_status(response);
  v12::frame_submit(response);
}
void respond_status(uint64_t nonce,uint64_t begin){if(lora_diagnostic_response||timing_diagnostic_response||fine_diagnostic_response)lora_status_frame(nonce,begin);else status(nonce,begin);}
[[gnu::noinline]] void ui(){
  if(!ui_ready)return;
  const auto s=v12::transport_status();const auto gps=gnss_pps::shadow_view(ticks());
  DisplayState state;state.page=page;state.unit_label=v12::native_device_binding()->label;
  state.role="V12 UNQUAL";state.basic_profile=true;state.ble_rx_enabled=s.radio_requested;
  state.scan=s.client.scan;state.basic_callbacks=s.reports;state.basic_queue_drops=s.queue_drops;
#ifdef V12_R2_ENABLED
  const auto mesh=v12::r2_status();
  state.ble_rx_enabled=mesh.requested;state.scan=mesh.scan;state.basic_callbacks=mesh.rx+mesh.pa_reports;state.basic_queue_drops=mesh.drops;
#endif
  state.basic_malformed=s.incomplete+s.oversize;state.no_stamp=s.reports;
  state.gps_inputs=gps_ready;state.gps_valid=gps.valid;state.gps_qualified=false;
  state.gps_pps_age_ms=gps.pps_age_ticks==UINT64_MAX?UINT32_MAX:uint32_t(gps.pps_age_ticks/20000);
  state.gps_rmc_age_ms=gps.rmc_age_ticks==UINT64_MAX?UINT32_MAX:uint32_t(gps.rmc_age_ticks/20000);
  state.gps_raw_rms_ns=gps.raw_rms_ns;state.gps_mapped_rms_ns=gps.mapped_rms_ns;
  state.gps_map_enclosure_ns=gps.map_bracket_ns;
  // C3 corner lamps: top physical PPS, middle checksum-valid GPS packets,
  // bottom fresh RMC fix. BLE clock quality and GNSS shadow stay separate.
  const auto indicator_tick=ticks();
  const auto activity=gnss_pps::activity(indicator_tick);
  state.pps_blink=gps_ready&&activity.pps20&&indicator_tick>=activity.pps20&&indicator_tick-activity.pps20<14000000;
  state.d1_rx=gps_ready&&activity.packet20&&indicator_tick>=activity.packet20&&indicator_tick-activity.packet20<14000000;
  state.d2_rx=gps_ready&&activity.fix;
  ui_gps_lamps=uint8_t((state.pps_blink?1:0)|(state.d1_rx?2:0)|(state.d2_rx?4:0));
#ifdef V12_GPS_SHADOW_OBSERVER
  const auto comparison=gps_comparison.view();state.gps_compare=true;state.gps_compare_samples=comparison.samples;
  state.gps_compare_mean_ns=comparison.mean_ns;state.gps_compare_rms_ns=comparison.rms_ns;state.gps_compare_max_ns=comparison.max_ns;
  state.gps_compare_fresh=comparison.reason==v12::G1GpsShadow::Ok&&comparison.last20&&indicator_tick>=comparison.last20&&indicator_tick-comparison.last20<40000000;
#endif
  // The local coarse GPS display never becomes V12 precision/source authority.
  if(gps.valid){state.valid=true;state.utc=uint32_t(gps.ns/1000000000LL);
    state.timing_source=DisplayState::Gps;state.gps_source=true;}
#ifdef V12_R2_ENABLED
  dashboard.apply(state,mesh);
#if V12_R2_CONFIG == 13
  const auto now_us=micros();
  const auto until_us=std::min(mesh.g1_anchor_deadline_us,mesh.g1_corrected_until_us);
  const auto until20=until_us>now_us&&until_us-now_us<(UINT64_MAX-indicator_tick)/20?
    indicator_tick+(until_us-now_us)*20:indicator_tick;
  indicator_model.quality(mesh.g1_relative_quality,mesh.g1_clock_model,state.r2_relative_lock,until20);
  const auto radio=v12::r2_activity();
  const auto lamps=indicator_model.update(indicator_tick,radio.rx,radio.tx,
    radio.requested&&!radio.fault&&!radio.mode_pending,radio.tx_armed,
    activity.packet20,activity.pps20,activity.fix);
  state.pps_blink=lamps.pps;state.d1_rx=lamps.rx;state.d2_rx=lamps.tx;
  state.r2_relative_lock=state.r2_relative_lock&&lamps.subus;
  if(!state.r2_relative_lock&&!std::strcmp(state.role,"LOCK_EST"))state.role="FOLLOW";
  ui_subus=lamps.subus;ui_gps_present=false;
  ui_gps_lamps=0;
#endif
#endif
  std::snprintf(ui_follow,sizeof ui_follow,"%s",state.followed_clock);
  for(auto& ch:ui_follow)if(ch==' ')ch='_'; // Console V3 fields remain single tokens; the OLED keeps spaces.
  ui_state=state.role;ui_utc=state.utc;ui_valid=state.valid;
  ui_hold_percentile=state.hold_percentile;ui_hold_ms=state.hold_1us_ms;ui_hold_samples=state.hold_trials;
  ui_rms_ns=state.rms_ns;
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  if(v12::log_status().capture_active){
    const auto begin=micros();
    const int n=v12::g1_emit_ui_budget([](const char* format,auto... args){return std::printf(format,args...);},
      mesh,begin,state.role,state.rms_ns,state.hold_1us_ms,state.r2_relative_lock,dashboard.settlement_pending(),state.r2_lock_seconds);
    if(n>0)console_bytes+=unsigned(n);
    const auto work=micros()-begin;console_work+=work;if(work>console_max_us)console_max_us=uint32_t(work);
  }
#endif
  n30_display_publish(mesh,state,micros());
}
void stop_all(){
  n30_timing_stop();n30_fine_stop();
  v12::fault_watchdog_sync_progress(false,0);
#ifdef V12_R2_ENABLED
  v12::r2_stop();
#endif
  v12::transport_stop();v12::direct_stop();v12::diagnostic_tx_stop();v12::lora_request(false,0);}
#ifdef V12_R2_ENABLED
// Keep the whole status snapshot out of command's frame. A read-only status
// response calls status() from this parser and must not stack both snapshots.
[[gnu::noinline]] bool r2_command_off_ack(){return v12::r2_status().off_ack;}
#endif
void command(const char* text){
  // Unbound OFF is a safety-only stop request. It carries no completed/fresh
  // receipt and cannot admit or prolong any RF/capture operation.
  if(!std::strcmp(text,"v12 off")){stop_all();v12::capture_stop();return;}
  char nonce_text[17]{},ticket_text[17]{},mac[18]{},boot_text[17]{};char extra=0;uint64_t nonce=0,permit=0,boot=0;
  const uint64_t admitted=micros();
  if(std::sscanf(text,"v12 status %16s %c",nonce_text,&extra)==1&&v12::hex_id(nonce_text,nonce)&&nonce!=last_nonce){
    last_nonce=nonce;last_command={nonce,admitted,true,"STATUS","READ_ONLY"};respond_status(nonce,admitted);return;
  }
  int at=0;
  if(std::sscanf(text,"v12 do %16s %16s %17s %16s %n",ticket_text,nonce_text,mac,boot_text,&at)!=4||at<=0||
      !v12::hex_id(nonce_text,nonce))return;
  last_command={nonce,admitted,false,"REFUSE","TICKET_OR_BOOT_BINDING"};
  const bool authorized=identity&&!std::strcmp(mac,factory_text)&&v12::hex_id(boot_text,boot)&&boot==session&&
    nonce!=last_nonce&&v12::hex_id(ticket_text,permit)&&ticket.consume(permit,admitted);
  if(!authorized){if(commands_rejected!=UINT32_MAX)++commands_rejected;respond_status(nonce,admitted);return;}
  last_nonce=nonce;const char* action=text+at;uint32_t duration=0;char mode[12]{};
  if(!std::strcmp(action,"off")){stop_all();v12::capture_stop();last_command={nonce,admitted,true,"OFF","STOP_REQUEST_ONLY"};}
  else if(!std::strcmp(action,"prepare_update")){
    const bool ok=update_gate.begin(admitted);if(ok){stop_all();v12::capture_stop();}
    last_command={nonce,admitted,ok,"PREPARE_UPDATE",ok?"ORIGINAL_2S_DRAIN":"LATCHED_OR_EXHAUSTED"};
  }else if(update_gate.latched()){last_command.reason="UPDATE_GATE_LATCHED";}
  // Presentation owns no BLE resource. Retain ticket/boot/maintenance guards,
  // then admit it before the shared radio-ownership fence.
  else if(!std::strcmp(action,"ui page 0")||!std::strcmp(action,"ui page 1")||!std::strcmp(action,"ui page 2")){
    const bool ok=n30_display_page(action[8]-'0');last_command={nonce,admitted,ok,"UI_PAGE",ok?"DISPLAY_ONLY":"INVALID_PAGE"};
  }
  else if(!std::strcmp(action,"lora diag")){lora_diagnostic_response=true;last_command={nonce,admitted,true,"LORA_DIAG","READ_ONLY_CRC_SNAPSHOT"};}
  else if(!std::strncmp(action,"fine ",5)){
    uint32_t spacing=0,edges=0;bool synthetic=false;
    if(!std::strncmp(action,"fine probe ",11)&&std::strlen(action)<80){
      const char* p=action+11;
      if(n30::timing::number(p,spacing)&&*p==' '){++p;synthetic=n30::timing::number(p,edges)&&!*p&&spacing>=50&&spacing<=1000&&edges>=4&&edges<=24&&edges%4==0;}
    }
    char translated[80]{};const auto length=std::snprintf(translated,sizeof translated,"timing %s",action+5);
    const auto request=length>=0&&unsigned(length)<sizeof translated?n30::timing::parse(translated):n30::timing::Request{};fine_diagnostic_response=true;
    last_command={nonce,admitted,false,"FINE","INVALID_BOUNDS_OR_SYNTAX"};
    if(synthetic){
      const bool ok=n30_fine_probe(nonce,fine_factory,admitted,spacing,edges);fine_after=0;
      last_command={nonce,admitted,ok,"FINE_PROBE",ok?"FINITE_SYNTHETIC_GPIO_ONLY":"BINDING_RESOURCE_OR_LATCH_REFUSED"};
    }else if(request.command==n30::timing::Command::Stop){
      n30_fine_stop();fine_after=0;last_command={nonce,admitted,true,"FINE_STOP","RAW_OBSERVER_ONLY"};
    }else if(request.command==n30::timing::Command::Read){
      fine_after=request.after;last_command={nonce,admitted,true,"FINE_READ","NONCONSUMING_CRC_CURSOR"};
    }else if(request.command==n30::timing::Command::Start){
      const bool ok=n30_fine_start(nonce,fine_factory,admitted,request.duration,request.every);fine_after=0;
      last_command={nonce,admitted,ok,"FINE_START",ok?"FINITE_RAW_OBSERVER":"BINDING_RESOURCE_OR_LATCH_REFUSED"};
    }
  }
  else if(!std::strncmp(action,"timing ",7)){
    const auto request=n30::timing::parse(action);timing_diagnostic_response=true;
    last_command={nonce,admitted,false,"TIMING","INVALID_BOUNDS_OR_SYNTAX"};
    if(request.command==n30::timing::Command::Stop){
      n30_timing_stop();timing_after=0;last_command={nonce,admitted,true,"TIMING_STOP","OBSERVER_ONLY"};
    }else if(request.command==n30::timing::Command::Read){
      timing_after=request.after;last_command={nonce,admitted,true,"TIMING_READ","NONCONSUMING_CRC_CURSOR"};
    }else if(request.command==n30::timing::Command::Start){
      const bool ok=n30_timing_start(nonce,admitted,request.duration,request.every);timing_after=0;
      last_command={nonce,admitted,ok,"TIMING_START",ok?"FINITE_VOLATILE_OBSERVER":"ACTIVE_OR_INVALID_BOUNDS"};
    }
  }
  else if(!std::strcmp(action,"lora auto")||!std::strcmp(action,"lora on")||!std::strcmp(action,"lora off")){
    const auto mode=!std::strcmp(action,"lora on")?n30::LoRaMode::KeepOn:!std::strcmp(action,"lora off")?n30::LoRaMode::KeepOff:n30::LoRaMode::Auto;
    const bool ok=n30_lora_mode(mode);last_command={nonce,admitted,ok,"LORA_MODE",ok?"VOLATILE_RX_ONLY":"DISABLED_FAULT_OR_RESOURCE"};
  }
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
  else if(!std::strcmp(action,"watchdog-test")){
    const bool ok=v12::fault_watchdog_test();if(ok)stop_all();
    last_command={nonce,admitted,ok,"WATCHDOG_TEST",ok?"SYNTHETIC_FAULT_ORIGINAL_DRAIN_120S_RESTART":"WATCHDOG_NOT_HEALTHY_OR_ALREADY_TESTED"};
  }
#endif
#ifdef V12_R2_ENABLED
  else if(!std::strcmp(action,"r2 client")){
    const auto ls=v12::lora_status();const bool ok=!ls.enabled&&!ls.requested&&!ls.worker_busy&&ls.off_acknowledged&&!ls.failed&&v12::r2_client_start(nonce);
    last_command={nonce,admitted,ok,"R2_CLIENT",ok?"PERSISTENT_COARSE_CLIENT_BOUNDED_START_STOP":"R2_RESOURCE_PROFILE_OR_BOOT_SCOPE"};
  }
#if V12_R2_CONFIG == 13
  else if(!std::strcmp(action,"upstream auto")||!std::strcmp(action,"upstream relay")){
    const bool ok=v12::r2_set_relay_only(!std::strcmp(action,"upstream relay"));
    last_command={nonce,admitted,ok,"UPSTREAM_MODE",ok?"AUTOMATIC_PARENT_ORIGINAL_2S_REBIND":"MODE_RESOURCE_OR_PENDING"};
  }
  else if(!std::strncmp(action,"rc-parent ",10)||!std::strncmp(action,"gm-parent ",10)){
    uint64_t target=0;const char* value=action+10;
    const bool parsed=!std::strcmp(value,"auto")||(std::strlen(value)==16&&v12::hex_id(value,target));
    const bool relay=action[0]=='r';
    const bool ok=parsed&&(relay?v12::r2_rc_parent(target):v12::r2_gm_parent(target));
    last_command={nonce,admitted,ok,relay?"RC_PARENT":"GM_PARENT",ok?"VOLATILE_PARENT_ORIGINAL_2S_REBIND":"PARENT_KIND_PROFILE_RESOURCE_OR_PENDING"};
  }
  else if(!std::strcmp(action,"r2 client 50")||!std::strcmp(action,"r2 client 100")){
    const auto ls=v12::lora_status();const auto percent=!std::strcmp(action,"r2 client 50")?50:100;
    const bool ok=!ls.enabled&&!ls.requested&&!ls.worker_busy&&ls.off_acknowledged&&!ls.failed&&v12::r2_client_start_profile(nonce,percent);
    last_command={nonce,admitted,ok,"R2_CLIENT_PROFILE",ok?"EXACT_OFF_NEW_GENERATION_PROFILE":"R2_RESOURCE_PROFILE_OR_BOOT_SCOPE"};
  }
#endif
  else if(std::sscanf(action,"r2 %11s %" SCNu32 " %c",mode,&duration,&extra)==2){
    const auto chosen=!std::strcmp(mode,"passive")?v12::R2Mode::Passive:!std::strcmp(mode,"active")?v12::R2Mode::Active:v12::R2Mode::Off;
    const auto ls=v12::lora_status();const bool ok=!ls.enabled&&!ls.requested&&!ls.worker_busy&&ls.off_acknowledged&&!ls.failed&&v12::r2_request(chosen,duration,nonce);last_command={nonce,admitted,ok,"R2",ok?"FINITE_R2_ORIGINAL_LEASE":"R2_RESOURCE_PROFILE_OR_BOUND"};
  }else if(!r2_command_off_ack()&&std::strncmp(action,"capture ",8)){last_command.reason="R2_OWNS_BLE";}
#endif
  else if(!std::strcmp(action,"capture stop")){
    v12::capture_stop();last_command={nonce,admitted,true,"CAPTURE_STOP","OBSERVER_ONLY_SERVICE_UNCHANGED"};
  }
  else if(std::sscanf(action,"capture rolling %" SCNu32 " %c",&duration,&extra)==1){
    const bool ok=v12::capture_rolling_begin(nonce,duration);last_command={nonce,admitted,ok,"CAPTURE_ROLLING",ok?"BOUNDED_OBSERVER_ORIGINAL_DEADLINE":"BUSY_OR_BOUND"};
  }
  else if(std::sscanf(action,"capture %" SCNu32 " %c",&duration,&extra)==1){
    const bool ok=v12::capture_begin(nonce,duration);last_command={nonce,admitted,ok,"CAPTURE",ok?"FINITE_OBSERVER_LEASE":"BUSY_OR_BOUND"};
  }else{last_command.reason="UNKNOWN_OR_UNAVAILABLE_PROTOCOL";}
  if(!last_command.accepted&&commands_rejected!=UINT32_MAX)++commands_rejected;
  respond_status(nonce,admitted);lora_diagnostic_response=timing_diagnostic_response=fine_diagnostic_response=false;
}
void input(){
  static char line[192]{};static unsigned used=0;static bool discard=false;
  uint8_t bytes[64];const int n=n30_console::read(bytes,sizeof(bytes));
  for(int i=0;i<n;++i){const char c=char(bytes[i]);
    if(c=='\r')continue;
    if(c=='\n'){if(!discard&&used){line[used]=0;command(line);}used=0;discard=false;continue;}
    if(discard)continue;
    if(c<32||c>126||used==sizeof(line)-1){discard=true;used=0;continue;}
    line[used++]=c;
  }
}
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
// Large snapshots live only across their own bounded operation. They must
// not reserve the main loop's frame across parser, console, UI and fit work.
[[gnu::noinline]] void g1_task_diagnostics(uint64_t now){
    if(now>=last_g1_diagnostic+10000000){const auto begin=micros();last_g1_diagnostic=now;const auto mesh=v12::r2_status();char line[v12::G1DiagnosticLineBytes];
      const auto log=v12::log_status();
      if(log.capture_active){
        if(g1_parent_history_capture!=log.capture_id){g1_parent_history_capture=log.capture_id;g1_parent_history_sequence=0;}
        for(unsigned i=0;i<2;i++){
          v12::G1ParentEvent event{};if(!v12::r2_parent_next(g1_parent_history_sequence,event))break;
          const auto lost=event.sequence-g1_parent_history_sequence-1;
          const int n=v12::g1_render_parent(line,sizeof line,event,lost);
          if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}
          g1_parent_history_sequence=event.sequence;
        }
        const auto hci=v12::g1_hci_status();std::printf("W12_G1_HCI_COUNTS at_us=%" PRIu64 " command=%" PRIu32 " ACK=%" PRIu32 " sync=%" PRIu32 " dropped=%" PRIu32 " max_us=%" PRIu32 " pending=0x%04x since_us=%" PRIu64 "\n",now,hci.commands,hci.acks,hci.sync_events,hci.dropped,hci.max_us,unsigned(hci.pending_opcode),hci.pending_at_us);
        const int printed=std::printf("W12_G1_RECORDS at_us=%" PRIu64 " AD_total=%" PRIu32 " AD_sampled=%" PRIu32 " AD_aggregated=%" PRIu32 " AD_policy=ONE_PER_PEER_PER_SECOND PA_capture_policy=UNTHROTTLED\n",now,g1_record_budget.discovery_total,g1_record_budget.discovery_sampled,g1_record_budget.discovery_aggregated);if(printed>0)console_bytes+=unsigned(printed);
        for(unsigned i=0;i<3;i++){const int n=v12::g1_render_summary(line,sizeof line,mesh,now,i);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
        {const int n=v12::g1_render_admission(line,sizeof line,mesh,now);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
        static uint32_t loss_generation=0;static uint64_t loss_capture=0;
        if(mesh.g1_receive_loss.frozen&&(loss_generation!=mesh.g1_receive_loss.generation||loss_capture!=log.capture_id)){
          for(unsigned i=0;i<mesh.g1_receive_loss.count;i++){const int n=v12::g1_render_loss(line,sizeof line,mesh.g1_receive_loss,i);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
          loss_generation=mesh.g1_receive_loss.generation;loss_capture=log.capture_id;
        }
        {const int n=v12::g1_render_recovery(line,sizeof line,mesh);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
        {const int n=v12::g1_render_clock(line,sizeof line,mesh,now);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
        {const int n=v12::g1_render_fit(line,sizeof line,mesh,now);if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);}}
        for(unsigned i=0;i<8;i++){const unsigned index=(g1_diagnostic_peer+i)%8;const int n=v12::g1_render_peer(line,sizeof line,mesh.g1_diagnostics.peers[index]);
          if(n>0&&unsigned(n)<sizeof line){const int printed=std::printf("%s",line);if(printed>0)console_bytes+=unsigned(printed);g1_diagnostic_peer=(index+1)%8;break;}}
      }else{const int n=v12::g1_render_live(line,sizeof line,mesh,log,now,v12::g1_hci_status());if(n>0&&unsigned(n)<sizeof line){const int printed=v12::log_live(line);if(printed>0)console_bytes+=unsigned(printed);}
        // Schedule from after the logger's independent10s gate, avoiding the
        // few-microsecond ordering that otherwise skips a whole snapshot.
        last_g1_diagnostic=micros();}
      const auto work=micros()-begin;console_work+=work;if(work>console_max_us)console_max_us=uint32_t(work);}
}
#endif
[[gnu::noinline]] void poll_update_drain(uint64_t now){
    if(update_gate.stage==v12::UpdateStage::Draining){
      const auto rx=v12::transport_status();const auto tx=v12::diagnostic_tx_status();
      const auto acl=v12::direct_status();const auto lora=v12::lora_status();
      v12::Services services;services.scan=rx.client.scan||rx.radio_requested;services.pending=rx.client.pending;
      services.host=!n30_fine_released()||rx.host_work_busy||rx.command_pending||!rx.off_acknowledged||tx.owner_busy||tx.lease.pending||!tx.off_acknowledged;services.sync=rx.client.synced;services.periodic=tx.periodic||tx.requested;services.discovery=tx.discovery;
      services.acl=acl.stage!=v12::DirectStage::Off;services.lora=lora.enabled||lora.requested||lora.worker_busy||!lora.off_acknowledged;
      services.fault=n30_fine_cleanup_fault()||rx.client.state==v12::Lifecycle::Fault||tx.fault||acl.stage==v12::DirectStage::Fault||lora.failed;
#ifdef V12_R2_ENABLED
      const auto mesh=v12::r2_status();services.scan|=mesh.scan||mesh.requested;services.pending|=mesh.sync_pending||mesh.connecting;
      services.sync|=mesh.synced;services.discovery|=mesh.advertising;services.acl|=mesh.connections!=0;
      services.host|=mesh.owner_busy||!mesh.off_ack;services.fault|=mesh.fault;
#endif
      update_gate.poll(now,services);
      if(update_gate.stage==v12::UpdateStage::Ready||update_gate.stage==v12::UpdateStage::Fault)update_completed_us=micros();
    }

}
[[gnu::noinline]] void poll_display_action(){
  // Consume during maintenance too: no deferred radio action after update.
  if(n30_display_take_upstream_toggle(micros())&&!update_gate.latched()){
    const auto radio=v12::r2_activity();(void)v12::r2_set_relay_only(!radio.relay_only);
  }
  if(n30_display_take_lora_toggle(micros())&&!update_gate.latched())(void)n30_lora_cycle();
}
[[gnu::noinline]] void clock_fault_progress(){
  const auto rx=v12::transport_status();const auto tx=v12::diagnostic_tx_status();
  const auto direct=v12::direct_status();const auto lora=v12::lora_status();
  uint32_t mask=0;uint8_t api=0;int32_t rc=0;uint64_t parent=0;bool relay=false,applied=true;
  // Unadmitted legacy SDK-reset markers retain their diagnostic meaning;
  // admitted owner faults and the active field clock feed supervision.
  if(rx.client.state==v12::Lifecycle::Fault&&rx.lease.generation)mask|=v12::FaultRx;
  if(tx.fault&&tx.lease.generation)mask|=v12::FaultTx;
  if(direct.stage==v12::DirectStage::Fault&&direct.incarnation)mask|=v12::FaultDirect;
  if(lora.failed&&lora.request_generation)mask|=v12::FaultLoRa;
  if(update_gate.stage==v12::UpdateStage::Fault)mask|=v12::FaultUpdate;
#ifdef V12_R2_ENABLED
  const auto mesh=v12::r2_status();const auto source_now=micros();
  n30_lora_control(n30::ble_locked(mesh,source_now),source_now,mesh.requested&&!mesh.fault&&!update_gate.latched());
  n30_lora_view lora_time;n30_lora_snapshot(lora_time);
  v12::fault_watchdog_sync_progress(mesh.requested&&!update_gate.latched(),n30::synchronized_until(mesh,lora_time,micros()));
  if(mesh.fault||mesh.client.deadline_exceeded||mesh.rx_capture.fault)mask|=v12::FaultR2;
  api=uint8_t(mesh.first_error_api);rc=mesh.first_error_rc;
  parent=mesh.rc_parent.target;relay=mesh.rc_parent.relay;
  applied=mesh.rc_parent.state==0||mesh.rc_parent.state==3;
  const auto wd=v12::fault_watchdog_status();
  if(wd.parent_pending&&mesh.rc_parent.state==3&&mesh.rc_parent.target==wd.parent&&mesh.rc_parent.relay==wd.parent_relay)
    v12::fault_watchdog_parent_restored();
  else if(wd.parent_pending&&!update_gate.latched()&&mesh.client.running&&!mesh.rc_parent.pending()&&!mesh.fault)
    (void)(wd.parent_relay?v12::r2_rc_parent(wd.parent):v12::r2_gm_parent(wd.parent));
#endif
  v12::fault_watchdog_main_progress(mask,api,rc,parent,relay,applied);
}
#ifdef V12_R2_ENABLED
[[gnu::noinline]] bool r2_authority_update_needed(){return dashboard.needs_authority_update(v12::r2_status());}
#endif
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13 && !defined(V12_GPS_SHADOW_OBSERVER)
[[gnu::noinline]] void aid_gnss_from_ble(){
  // Preserve the existing authenticated JOIN snapshot and aid gates. Do not
  // reserve this large snapshot across every other operation in app_main().
  const auto aid=v12::r2_status();
  if(!update_gate.latched() && aid.requested && !aid.fault && aid.following && aid.group_member)
    gnss_pps::aid_time({true,aid.follower_utc_us,aid.follower_snapshot_us,aid.follower_received_us,
      aid.service_until_us,aid.root_identity,aid.root_boot,uint32_t(aid.g1_anchor_guard_us),aid.source_epoch,aid.publication},micros());
}
#endif

}
extern "C" bool IRAM_ATTR clock_reference_read(uint64_t* count){
  return count&&clock_timer&&gptimer_get_raw_count(clock_timer,count)==ESP_OK;
}
extern "C" bool IRAM_ATTR clock_reference_bt_bracket(uint64_t* before,uint64_t* after,adv_rx::BleTime* time){
  w12_ble_time_t observed{};
  if(!time||w12_s3_ble_time_bracket(clock_timer,before,after,&observed)!=ESP_OK)return false;
  time->half_slots=observed.half_slots;time->half_us=observed.half_us;return true;
}
extern "C" void app_main(){
  uint8_t factory[6]{};esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY);
  std::snprintf(factory_text,sizeof(factory_text),"%02x:%02x:%02x:%02x:%02x:%02x",
    factory[0],factory[1],factory[2],factory[3],factory[4],factory[5]);
  const auto device=v12::native_device_binding();
  identity=device&&!std::memcmp(factory,device->factory,6);esp_fill_random(&session,sizeof(session));
  std::printf("W12_V12_BOOT unit=%s factory=%s identity=%u session=%016" PRIx64
    " source=%s build=%s profile=V12_NATIVE_CONSOLE_R3 controller=PINNED_5_5_5"
    " RF=OFF legacy_PA_us=31250 root_bytes=158 relay_bytes=182 discovery_grant_bytes=190"
    " max_data=191 max_GMs=4 activities=6 syncs=2 adv_sets=2 mtu=247"
    " precision=UNAVAILABLE GM=DISABLED LoRa=OFF WiFi=OFF PTS=OFF\n",
    V12_UNIT,factory_text,unsigned(identity),session,V12_SOURCE_REVISION,V12_BUILD_ID);
  std::printf("N30_TARGET version=0.2.13 profile=RC GPS=ABSENT flash_mb=8 console=UART0_43_44 hardware=UNVALIDATED\n");
  if(!identity||!session){std::printf("W12_V12_FATAL reason=FACTORY_OR_BOOT_BINDING peripherals=UNTOUCHED\n");return;}
  for(const auto byte:factory)fine_factory=(fine_factory<<8)|byte;
  n30_timing_init();
  if(!v12::fault_watchdog_start(factory,session)){std::printf("W12_V12_FATAL reason=FAULT_SUPERVISOR\n");return;}
  // This initial diagnostic candidate never initializes, migrates or writes
  // legacy NVS. Native credentials remain refused until that separate gate.
  gptimer_config_t timer{};timer.clk_src=GPTIMER_CLK_SRC_XTAL;
  timer.direction=GPTIMER_COUNT_UP;timer.resolution_hz=20000000;
  if(gptimer_new_timer(&timer,&clock_timer)!=ESP_OK||gptimer_enable(clock_timer)!=ESP_OK||gptimer_start(clock_timer)!=ESP_OK){
    v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=XTAL_TIMER\n");return;}
  if(!n30_console::init()){v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=CONSOLE_DRIVER\n");return;}
  if(!v12::log_start()){v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=CONSOLE_WORKER\n");return;}
  gpio_config_t button{};button.pin_bit_mask=1;button.mode=GPIO_MODE_INPUT;button.pull_up_en=GPIO_PULLUP_ENABLE;
  if(gpio_config(&button)!=ESP_OK){v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=BUTTON\n");return;}
  // The N30 display worker is the sole button interpreter.
  ui_ready=display_init();gps_ready=gnss_pps::init(clock_timer,pps);
#ifdef V12_R2_ENABLED
  if(!v12::r2_init(session,factory)){v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=R2_ENROLLMENT\n");return;}
  gnss_pps::set_nmea_consumer([](const uint8_t* bytes,unsigned n){v12::r2_nmea(bytes,n,micros());});
#ifdef V12_R2_FIELD_CLIENT
#if V12_R2_CONFIG == 13
  std::printf("W12_R2_BOOT profile=%s enrollment_sha=%s self=3 freshness=CLIENT_CAUSAL client_capabilities=0 client_control=0 RF=AUTO_CLIENT_AFTER_HOST_SYNC active=PERSISTENT_WITH_VERIFIED_SDK_RESET_RECOVERY precision=UNAVAILABLE\n",V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA);
#else
  std::printf("W12_R2_BOOT profile=%s enrollment_sha=%s self=3 roster=4 RF=AUTO_CLIENT_AFTER_HOST_SYNC active=PERSISTENT_ONCE_PER_BOOT precision=UNAVAILABLE\n",V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA);
#endif
#else
  std::printf("W12_R2_BOOT profile=%s enrollment_sha=%s self=3 roster=4 RF=OFF active=FINITE_ONCE_PER_BOOT precision=UNAVAILABLE\n",V12_R2_PROFILE,V12_R2_ENROLLMENT_SHA);
#endif
#endif
  v12::diagnostic_tx_identity(factory,session);
  const uint8_t wire_id=device->wire_id;
  if(!ui_ready||!v12::lora_start_worker()||!v12::direct_init(factory,wire_id)||!v12::public_observer_init()||!v12::transport_init()){
    v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=UI_OR_NATIVE_HOST_INIT RF=OFF\n");return;}
  v12::fault_watchdog_host_expected();
  while(true){
    // Independent of output and the NimBLE task: expiry remains visible even
    // when that owner is yielding inside an HCI enable/stop. This observer only
    // latches/queues cleanup; it cannot certify physical quiet or retry RF.
    v12::transport_observe_lease(micros());v12::diagnostic_tx_observe_lease(micros());
#ifdef V12_R2_ENABLED
    v12::r2_observe(micros());
#endif
    #ifndef V12_GPS_SHADOW_OBSERVER
#ifdef PTOB_G1_PPS_DIAGNOSTIC
    observe_production_pps_diagnostic();
#endif
    gnss_pps::poll(ticks());
    #endif
    input();
    // Physical display-worker action; the main owner calls the unchanged W12
    // mode API, which fences the old source and retains its original2s drain.
    poll_display_action();
#ifdef V12_GPS_SHADOW_OBSERVER
    observe_gps_actuator();
#else
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    aid_gnss_from_ble();
#endif
#endif
#ifdef V12_GPS_SHADOW_OBSERVER
    gnss_pps::poll(ticks());
    const bool capturing=v12::log_status().capture_active;
    if(capturing&&!shadow_capture){
      shadow_dump=0;const auto summary=gps_comparison.view();
      std::printf("W12_GPS_PPS_SESSION session=%" PRIu32 " root=%016" PRIx64 " root_boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32 " GPS_epoch=%" PRIu32
        " samples=%" PRIu32 " mean_ns=%" PRId32 " rms_ns=%" PRIu32 " jitter_ns=%" PRIu32 " max_ns=%" PRIu32 " last20=%" PRIu64
        " edges=%" PRIu32 " missing=%" PRIu32 " invalid_reference=%" PRIu32 " overwritten=%" PRIu32 " duplicates=%" PRIu32 " reason=%u coverage=AGGREGATE_AND_LAST16_RAM_ONLY calibration=UNQUALIFIED absolute_UTC=UNKNOWN\n",
        summary.session,summary.root,summary.boot,summary.source,summary.train,summary.gps_epoch,summary.samples,summary.mean_ns,summary.rms_ns,summary.jitter_ns,summary.max_ns,summary.last20,
        summary.edges,summary.missing,summary.invalid_reference,summary.overwritten,summary.duplicates,unsigned(summary.reason));
    }
    if(capturing&&shadow_dump<v12::G1GpsShadow::Tail){v12::G1GpsShadow::Record row;
      if(gps_comparison.read(shadow_dump,row)){std::printf("W12_GPS_PPS_SAVED number=%" PRIu32 " edge20=%" PRIu64 " phase_ns=%" PRId32 " model_age_us=%" PRIu32 " session=%" PRIu32 " reason=%u reference=%u following=%u permission=%u enclosure_ns=%" PRIu32 " coverage=RETAINED_LAST16_RAM_ONLY\n",row.number,row.edge20,row.phase_ns,row.age_us,row.session,unsigned(row.reason),unsigned(row.reference),unsigned(row.following),unsigned(row.permission),row.enclosure_ns);++shadow_dump;}else shadow_dump=v12::G1GpsShadow::Tail;}
    shadow_capture=capturing;
#endif
    v12::Report row;for(unsigned budget=0;budget<8&&v12::transport_pop(row);++budget){
      if(!row.refusal&&(row.kind==1||row.kind==3))v12::public_observer_enqueue(row.bytes,row.length);
      const uint64_t begin=micros();
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
      if(row.kind==5&&(!v12::log_status().capture_active||!g1_record_budget.emit(row)))continue;
      char diagnostic[v12::G1DiagnosticLineBytes];int rendered=0;
      if(row.kind==21&&row.length==4){
        const auto sequence=uint32_t(v12::g1_diag_le(row.bytes,4));v12::G1ParentEvent event{};
        if(v12::r2_parent_event(sequence,event))rendered=v12::g1_render_parent(diagnostic,sizeof diagnostic,event);
        else rendered=std::snprintf(diagnostic,sizeof diagnostic,"W12_G1_PARENT_MISSING seq=%" PRIu32 " reason=RAM_HISTORY_OVERRUN\n",sequence);
      }else if(row.kind==22)rendered=v12::g1_render_relay_owner(diagnostic,sizeof diagnostic,row);
      else if(row.kind==23)rendered=v12::g1_render_tx_estimate(diagnostic,sizeof diagnostic,row);
      else if(row.kind==24)rendered=v12::g1_render_tx_path(diagnostic,sizeof diagnostic,row);
      else rendered=row.kind==8?v12::g1_render_link(diagnostic,sizeof diagnostic,row):v12::g1_render_scope(diagnostic,sizeof diagnostic,row);
      if(rendered>0&&unsigned(rendered)<sizeof diagnostic){const int n=std::printf("%s",diagnostic);if(n>0)console_bytes+=unsigned(n);const auto work=micros()-begin;console_work+=work;if(work>console_max_us)console_max_us=uint32_t(work);continue;}
#endif
      char hex[2*v12::MaxData+1];const char digits[]="0123456789abcdef";
      for(unsigned i=0;i<row.length;++i){hex[2*i]=digits[row.bytes[i]>>4];hex[2*i+1]=digits[row.bytes[i]&15];}
      hex[2*row.length]=0;
      const int printed=std::printf("W12_V12_CALLBACK callback_us=%" PRIu64 " kind=%u cycle=%" PRIu32
        " handle=%u sid=%u address_type=%u address_le=%02x%02x%02x%02x%02x%02x status=%u rssi=%d actual_bytes=%u retained_bytes=%u refusal=%u"
        " metadata_version=1 metadata_mask=%u props=%u legacy_event_type=%u primary_phy=%u secondary_phy=%u advertised_tx_power_dbm=%d PA_interval=%u"
        " hex=%s physical_tick=UNAVAILABLE"
        " event_counter=UNAVAILABLE selected_generation=UNAVAILABLE authority=NONE\n",
        row.callback_us,unsigned(row.kind),row.cycle,unsigned(row.handle),unsigned(row.sid),unsigned(row.address_type),row.address_le[0],row.address_le[1],row.address_le[2],row.address_le[3],row.address_le[4],row.address_le[5],unsigned(row.status),int(row.rssi),unsigned(row.actual_length),unsigned(row.length),unsigned(row.refusal),
        unsigned(row.metadata_mask),unsigned(row.discovery_props),unsigned(row.legacy_event_type),unsigned(row.primary_phy),unsigned(row.secondary_phy),int(row.advertised_tx_power),unsigned(row.periodic_interval),hex);
      if(printed>0)console_bytes+=unsigned(printed);
      if(row.kind==6){const auto& c=row.rx_stamp;
        std::printf("W12_R2_RX_SHADOW callback_us=%" PRIu64 " handle=%u actual=%u descriptor_tick20=%" PRIu64 " available20=%" PRIu64
          " epoch=%" PRIu32 " sequence=%" PRIu32 " coarse=%" PRIu32 " fine=%u raw_metadata=%u activity=%u map_enclosure_ns=%" PRIu32
          " raw_tick20=%" PRIu64 " refinement_ns=%d refinement_applied=%u event_candidate=%u channel_candidate=%u association_observed=%u event_valid=%u channel_valid=%u edge_calibrated=%u rx_reference_version=1 controller_phy=%u controller_extra_us=%u reference_valid=%u reference_applied=%u output_correction_ns=0 precision=UNAVAILABLE qualified_BLE_training=NONE functional_match=WORKER_ONLY\n",
          row.callback_us,unsigned(row.handle),unsigned(c.actual),c.tick20,c.available20,c.epoch,c.sequence,c.coarse,unsigned(c.fine),unsigned(c.raw_metadata),unsigned(c.activity),c.map_enclosure_ns,
          uint64_t(int64_t(c.raw_tick20())-(c.refinement_applied?c.refinement_ticks:0)),int(c.refinement_ticks)*50,unsigned(c.refinement_applied),unsigned(c.event),unsigned(c.channel),unsigned(c.association_observed),unsigned(c.event_valid),unsigned(c.channel_valid),unsigned(c.edge_calibrated),unsigned(c.phy),unsigned(c.radio_extra_us),unsigned(c.radio_reference_valid),unsigned(c.radio_reference_applied));}
      const uint64_t work=micros()-begin;console_work+=work;if(work>console_max_us)console_max_us=uint32_t(work);
    }
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    v12::G1HciTrace hci_row;
    for(unsigned budget=0;budget<8&&v12::g1_hci_pop(hci_row);++budget){
      if(!v12::log_status().capture_active)continue;
      const auto begin=micros();const int n=std::printf("W12_G1_HCI at_us=%" PRIu64 " seq=%" PRIu32 " kind=%u opcode=0x%04x result=%" PRId32 " event=0x%02x duration_us=%" PRIu32 " reference=HCI_API_ONLY\n",
        hci_row.at_us,hci_row.sequence,unsigned(hci_row.kind),unsigned(hci_row.opcode),hci_row.result,unsigned(hci_row.event),hci_row.duration_us);
      if(n>0){console_bytes+=unsigned(n);}
      const auto work=micros()-begin;console_work+=work;if(work>console_max_us)console_max_us=uint32_t(work);
    }
#endif
    const uint64_t now=micros();
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    g1_task_diagnostics(now);
#endif
    poll_update_drain(now);
    bool update_ui=now-last_ui>=125000;
#ifdef V12_R2_ENABLED
    // Clear a revoked root/fault promptly;1s is only the routine repaint cadence.
    update_ui|=r2_authority_update_needed();
#endif
    if(update_ui){last_ui=now;ui();}
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
    if(ui_ready&&now-last_indicator_us>=20000){
      last_indicator_us=now;
      if(!n30_display_minimal()){
        const auto now20=ticks();const auto gps=gnss_pps::activity(now20);
        const auto radio=v12::r2_activity();
        const auto lamps=indicator_model.update(now20,radio.rx,radio.tx,
          radio.requested&&!radio.fault&&!radio.mode_pending,radio.tx_armed,gps.packet20,gps.pps20,gps.fix);
        if(ui_subus&&!lamps.subus)last_ui=0; // Withdraw the header on the next main pass, too.
        ui_subus=lamps.subus;ui_gps_present=false;
        ui_gps_lamps=0;
        // N30 has no physical GNSS/PPS lamps; its RF page uses semantic counters.
      }
    }
#endif
    n30_fine_poll(micros());
    clock_fault_progress();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
