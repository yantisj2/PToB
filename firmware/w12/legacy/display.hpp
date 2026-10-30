#pragma once
#include <cstdint>
#ifdef R2_TZ_WIRE
#include "coarse_time_zone.hpp"
#endif
#include "display_pages.hpp"
#include "display_timing.hpp"
#include "ptob_ui_role.h"

struct DisplayState {
#ifdef R2_TZ_WIRE
    r2_tz tz{};uint64_t tz_until_us=0,tz_sampled_us=0;
#endif
    ptob_ui_role_t ui_role{};
    struct SourcePanel {
        bool enabled=false,present=false,fresh=false,fit=false,hold=false;
        uint32_t precision_ns=UINT32_MAX,age_s=UINT32_MAX;
        bool offset_valid=false;
        int64_t offset_ns=0;
    } comparison[3]; // BLE, PTS, LoRa; precision is relative, not absolute UTC.
#ifdef PTOB_W12_DUAL_UI
    uint8_t lora_mode=0,lora_state=0,lora_link_samples=0;bool lora_active=false,lora_fault=false,lora_configured=false,lora_time_valid=false;
    uint32_t lora_client_hold_ms=0;bool rc9_serving=false;uint8_t rc9_clients=0;
    uint32_t lora_transition_ms=0,lora_link_rms_ns=UINT32_MAX,lora_accepted=0,lora_crc=0;
    bool precision_live=false,precision_fine=false,precision_lora=false;int32_t precision_rate_ppb=0;int64_t precision_phase_ns=0;
#endif
    uint8_t page=0;
    enum TimingSource : uint8_t { NoSource, Ble, Lora, Pts, Gps, Rc9 } timing_source=NoSource;
    bool gps_inputs=false,gps_valid=false,gps_source=false,gps_qualified=false;
    uint8_t gps_class=7;
    uint32_t gps_pps_age_ms=UINT32_MAX,gps_rmc_age_ms=UINT32_MAX;
    uint32_t gps_raw_rms_ns=UINT32_MAX,gps_mapped_rms_ns=UINT32_MAX;
    uint32_t gps_map_enclosure_ns=UINT32_MAX;
    bool gps_compare=false,gps_compare_fresh=false;uint32_t gps_compare_samples=0;
    int32_t gps_compare_mean_ns=0;uint32_t gps_compare_rms_ns=UINT32_MAX,gps_compare_max_ns=UINT32_MAX;
    bool valid=false, pps_blink=false;
    bool d1_rx=false, d2_rx=false, scan=false,ble_rx_enabled=false;
    bool fit=false, gm_trial=false, basic_profile=false, copy_profile=false, copy_stale=false, copy_local=false;
    uint8_t copy_channel=0;
    uint32_t copy_id=0,copy_delay_us=0,copy_fault=0;
    uint32_t copy_p95_ns=UINT32_MAX,copy_p99_ns=UINT32_MAX,copy_tail_max_ns=UINT32_MAX,copy_tail_samples=0;
    uint32_t copy_hold_p95_ms=0,copy_hold_p99_ms=0;
    uint32_t basic_session=0,basic_callbacks=0,basic_malformed=0,basic_conflicts=0;
    uint32_t basic_queue_drops=0,basic_max_gap_ms=0,basic_scan_max_ms=0;
    uint32_t basic_scan_violations=0,basic_rejected=0;
    int8_t basic_rssi=0;
    bool basic_local_reference=false;
    uint32_t utc=0, age_s=0, source_id=0, root_id=0;
    uint32_t beats=0, reports=0, corrected=0, rejected=0;
    uint32_t duplicates=0, ambiguous=0, expired=0, no_stamp=0;
    uint32_t sequence_gaps=0, sequence_expected=0, map_max_us=0;
    uint32_t report_gaps=0, report_expected=0;
    uint32_t e0_seen=0, e0_valid=0, c4_age_s=UINT32_MAX;
    uint32_t c4_seen=0,c4_valid=0,c4_weighted=0,ble_noise_ns=UINT32_MAX;
    uint32_t c4_rms_ns=UINT32_MAX,c4_peak_ns=UINT32_MAX,c4_slot_peak_ns=UINT32_MAX;
    int32_t c4_prediction_ns=INT32_MIN;
    uint8_t c4_slot_pages=0;
    bool c4_trusted=false;
    uint32_t span_s=0, samples=0, rms_ns=UINT32_MAX;
    uint32_t precision_ns=UINT32_MAX, uncertainty_us=UINT32_MAX;
    uint32_t rate_sigma_ppb=UINT32_MAX;
    uint32_t relative_prediction_ns=UINT32_MAX;
    bool shadow_holdover=false;
    uint64_t shadow_p99_ns=UINT64_MAX;uint32_t shadow_samples=0;
    uint32_t shadow_age_ms=0,shadow_safe_ms=0,shadow_cross_ms=0;bool shadow_holding=false;
    uint32_t hold_trials=0,hold_precision_ns=UINT32_MAX,hold_age_s=UINT32_MAX;
    uint32_t hold_audit_samples=0;
    bool hold_audit_required=false;
    uint8_t hold_animation=0;
    uint8_t hold_percentile=95;
    bool hold_estimated=false;
    uint32_t hold_trial_ms=1,hold_1us_ms=0,hold_1us_s=0,hold_1ms_s=UINT32_MAX;
    bool pts_ms_hold=false,utc_holdover=false;
    bool lora_ready=false;
    bool lora_radio_timestamp=false;
    uint32_t lora_local_us=UINT32_MAX;
    uint32_t lora_source=0,lora_packets=0,lora_matched=0,lora_missing=0;
    uint8_t lora_profile=1;
    uint32_t lora_rejected=0,lora_conflicts=0,lora_errors=0;
    uint32_t lora_age_s=UINT32_MAX;
    uint16_t lora_sequence=0;
    int16_t lora_rssi=0,lora_snr_tenths=0;
    int32_t drift_mppb=0, trim_mppb=0, resid_mppb=0;
    bool pts_available=false,pts_enabled=false,pts_window=false,pts_fit=false,pts_holdover=true,pts_offset_valid=false;
    bool pts_ready=false;uint32_t pts_precision_ns=UINT32_MAX,ble_precision_ns=UINT32_MAX,ble_age_s=UINT32_MAX;
    uint8_t pts_gm_role=0,ftm_peers=0,ftm_status=255,ftm_samples=0;
    bool pts_gm_eligible=false,ftm_enabled=false,ftm_active=false,ftm_root_native=false,ftm_ranging_valid=false,ftm_diagnostic=false,ftm_from_peer=false;
    uint32_t pts_gm_precision_ns=UINT32_MAX,pts_map_ns=UINT32_MAX;
    uint32_t ftm_rtt_ns=UINT32_MAX,ftm_age_s=UINT32_MAX,ftm_successes=0,ftm_starts=0;
    uint32_t pts_age_s=UINT32_MAX,pts_rms_ns=UINT32_MAX;
    int32_t pts_offset_us=0;
    bool ble_fit=false,lora_fit=false,source_offset_valid=false;
    uint32_t ble_accuracy_us=UINT32_MAX;
    uint32_t lora_relative_ns=UINT32_MAX;
    int32_t source_offset_us=0;
    int32_t ble_drift_ppb=0,ble_frequency_ppb=0;
    int32_t lora_drift_ppb=0,lora_frequency_ppb=0;
    int32_t rtt_offset_ns=0;
    uint32_t rtt_bound_ns=UINT32_MAX;
    uint8_t rtt_status=0;
    uint32_t rtt_age_s=UINT32_MAX;
    bool alignment_fit=false;
    int64_t alignment_offset_ns=0;
    int32_t alignment_drift_ppb=0;
    uint32_t alignment_scatter_ns=UINT32_MAX,alignment_age_s=UINT32_MAX;
    uint32_t alignment_ble_gaps=0,alignment_lora_gaps=0,alignment_resets=0;
    uint8_t alignment_samples=0;
    const char* unit_label="C-?";
    bool r2_dashboard=false;
    bool r2_relative_lock=false;
    bool r2_relay_only=false,r2_mode_pending=false;
    uint64_t r2_lock_seconds=0;
    const char* followed_clock="NONE";
    const char* r2_root_clock="NONE";
    const char* r2_connection="NONE";
    bool boundary=false,boundary_enabled=false,boundary_qualified=false;
    bool boundary_air_confirmed=false;
    uint32_t boundary_air_packets=0;
    bool boundary_fault=false;
    bool boundary_stale=false;
    uint32_t boundary_source=0,boundary_root=0,boundary_phase_us=630000;
    uint32_t boundary_age_ms=UINT32_MAX,boundary_skips=0;
    uint32_t boundary_capture_failures=0;
    uint32_t boundary_tx_bound_ns=UINT32_MAX;
    uint16_t boundary_sequence=0;
    bool mesh_profile=false,mesh_serving=false;uint8_t mesh_depth=0;uint32_t mesh_age_ms=UINT32_MAX;
    const char* role="WAIT";
};
bool display_init();
bool display_set_scan(DisplayTiming::Profile profile);
void display_show(const DisplayState& state);
void display_update_indicators(bool pps,bool d1,bool d2);
void display_sleep();
void display_wake();
void display_blank();
