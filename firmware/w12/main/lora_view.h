// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Immutable worker snapshot. Unknown bounds/boots stay unknown; polling cannot
// renew an original event, companion, source envelope or functional12s hold.
struct w12_lora_view {
 uint64_t sampled_us,utc_us,until_us,edge_us,edge_tick,capture_epoch;
 uint64_t emitter,emitter_boot,root,root_boot,parent,parent_boot,stream;
 uint64_t created_ns,expires_ns,event_ns,metadata_us;
 uint32_t source_epoch,generation,sequence,corrected_sequence,map_generation,map_age_us;
 uint32_t accepted,rejected,replayed,overflow,unmatched,metadata,metadata_misses;
 uint32_t link_rms_ns,age_ms,snapshot_age_ms,diagnostic_sequence;
 uint32_t parent_bound_ns,correction_age_ms,map_life_ms,residual_ns;
 int32_t rate_ppb;uint16_t quality_known,quality_values,events;
 uint8_t context[16],state,assurance,authority,kind,scale,link_samples;
 uint8_t role,hop,parent_class,parent_accuracy,losses,recoveries;
 bool requested,active,fault,metadata_known,time_valid,parent_quality;
 uint32_t rx_done,crc_bad,malformed,spi_ok,spi_error,invalid_capture,capture_overflow;
 uint32_t map_accepted,map_rejected,capture_bound_ns,work_max_us,stack_unused,heartbeat_ms;
 uint32_t request_generation,off_ack_generation;
 int32_t error;int16_t rssi_dbm,snr_quarter_db;
 uint8_t mode,stage,opcode;uint32_t transition_remaining_ms;
 bool off_acknowledged,spi_retired,capture_released,reset_asserted;
};
#ifdef __cplusplus
}
#endif
