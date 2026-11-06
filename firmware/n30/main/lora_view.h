// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Immutable worker snapshot. Unknown bounds/boots stay unknown; polling cannot
// renew an original event, META context or physical120s timing lease.
struct n30_lora_view {
 uint16_t tz_id;int16_t tz_offset_min;
 // Boot-RAM descriptive cache and explicitly unqualified human watch.
 bool metadata_cached,watch_valid;uint8_t watch_state,watch_reason,watch_prediction_mode;
 uint64_t watch_sampled_us,watch_utc_us;
 uint32_t watch_pairs,watch_rejected,watch_missing,watch_unmatched,watch_age_ms,watch_error_age_ms,watch_span_ms,watch_prediction_age_ms;
 uint32_t watch_prediction_n,watch_total_prediction_n,watch_rms_ns,watch_total_rms_ns,watch_max_ns,watch_over1us;int32_t watch_last_ns,watch_rate_ppb;
 uint64_t sampled_us,utc_us,until_us,edge_us,edge_tick,capture_epoch;
 uint64_t emitter,emitter_boot,root,root_boot,parent,parent_boot,stream;
 uint64_t created_ns,expires_ns,event_ns,metadata_us;
 uint64_t model_tick16,model_ns,model_epoch;
 int64_t model_rate_milli_ppb,model_accel_micro_ppb_s,model_fast_accel_micro_ppb_s,model_slow_accel_micro_ppb_s;
 uint32_t prediction_n,prediction_rms_ns,prediction_recent_rms_ns,prediction_max_ns,prediction_over1us;
 uint32_t model_samples,model_span_ms;
 uint32_t frozen60_n,frozen60_rms_ns,frozen60_max_ns,frozen60_over1us;
 int32_t prediction_last_ns;uint8_t model_kind;bool raw_trained;
 uint64_t hold_id,hold_start_us,hold_until_us,hold_grant_until_us;
 uint32_t hold_runs,hold60_n,hold60_rms_ns,hold60_max_ns,hold60_over1us;
 bool hold_active;uint8_t hold_state,hold_abort;
 uint64_t hold_requested_id;uint8_t hold_result;
 uint32_t source_epoch,generation,sequence,corrected_sequence,map_generation,map_age_us;
 uint32_t accepted,rejected,replayed,overflow,unmatched,metadata,metadata_misses;
 uint32_t link_rms_ns,age_ms,reference_age_ms,snapshot_age_ms,diagnostic_sequence,fit_span_ms;
 uint32_t parent_bound_ns,correction_age_ms,map_life_ms,residual_ns;
 int32_t rate_ppb,phase_ns,model_rate_ppb,model_phase_ns;uint16_t quality_known,quality_values,events;
 uint8_t context[16],state,assurance,authority,kind,scale,link_samples;
 uint8_t role,hop,parent_class,parent_accuracy,losses,recoveries;
 bool requested,active,fault,metadata_known,time_valid,parent_quality,trained,slow_fit,model_valid;
 uint32_t rx_done,crc_bad,malformed,spi_ok,spi_error,invalid_capture,capture_overflow;
 uint32_t map_accepted,map_rejected,map_soft_refused,map_hard_reset,capture_bound_ns,map_bracket_ns,work_max_us,stack_unused,heartbeat_ms;
 uint32_t request_generation,off_ack_generation;
 int32_t error;int16_t rssi_dbm,snr_quarter_db;
 // First native CURRENT failure for this boot; stop/drain cannot erase it.
 int32_t current_rc;uint8_t current_reason;uint32_t current_raw,current_prev_raw;
 uint64_t current_us,current_prev_us,current_available_us,current_epoch;
 uint8_t mode,stage,opcode;uint32_t transition_remaining_ms;
 bool off_acknowledged,spi_retired,capture_released,reset_asserted,ble_locked;
};
#ifdef __cplusplus
}
#endif
