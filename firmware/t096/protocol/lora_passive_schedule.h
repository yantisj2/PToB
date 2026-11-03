/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#if defined(R2_LORA_TX_DIAGNOSTIC) && (!defined(R2_LORA_PASSIVE_FAST) || defined(R2_LORA_PASSIVE_SLOW))
#error "Diagnostic TX requires explicit passive fast only"
#endif
#if defined(R2_LORA_BW_EXPERIMENT) && (!defined(R2_LORA_PASSIVE_FAST) || defined(R2_LORA_PASSIVE_SLOW) || defined(R2_LORA_TX_DIAGNOSTIC))
#error "Bandwidth experiment requires explicit passive fast only, without diagnostic TX"
#endif
#define LP_FAST 1u
#define LP_SLOW 3u
#define LP_HISTORY_CAP_US UINT64_C(1080000000)
#define LP_TRAIN_US UINT64_C(180000000)
#define LP_WINDOW_US UINT64_C(300000000)
#define LP_HOUR_US UINT64_C(3600000000)
#define LP_MINUTE_US UINT64_C(60000000)
#ifdef R2_LORA_BW_EXPERIMENT
static inline unsigned lp_bw_arm(uint32_t cursor){
 unsigned slot=(cursor/2)%4;return slot==1||slot==2;
}
#endif
#ifdef R2_LORA_PASSIVE_SLOW
#define LP_SCHEDULE LP_SLOW
/* P30/D120/M300. Schedule2 is deliberately incompatible. Each precision
 * slot retains the original RF/source/capture fences, with or without DTS. */
#define LP_BEAT_US UINT64_C(30000000)
#define LP_META_BEATS 10u
#define LP_DISC_BEATS 4u
#define LP_DISC_PHASE 1u
#define LP_SYNC_OFFSET_US UINT64_C(7000000)
#define LP_MAX_DELAY_10MS 700u
#define LP_FRAME_END_US UINT64_C(15000000)
#define LP_LAUNCH_LATE_US UINT64_C(1000000)
#define LP_HINT_RETIRE_US UINT64_C(3000000)
#define LP_PREDICT_HINT_RETIRE_US UINT64_C(4000000)
#define LP_PREDICT_HEADER_WINDOW_US UINT64_C(3000000)
#define LP_HEADER_WINDOW_US UINT64_C(1500000)
#define LP_PENDING_US UINT64_C(7000000)
#define LP_RX_TUNE_LATE_US UINT64_C(1000000)
#define LP_CONTROL_RETIRE_US UINT64_C(4000000)
#define LP_CONTROL_PROCESS_US UINT64_C(1000000)
#define LP_RX_TUNE_LEAD_US UINT64_C(4000000)
#define LP_LAUNCH_PAD_US UINT64_C(1250000)
#define LP_FOLLOW_OFFSET_US UINT64_C(2500000)
#define LP_FOLLOW_LATEST_US UINT64_C(4500000)
#define LP_HOLD_US UINT64_C(120000000)
#define LP_LEASE_US UINT64_C(420000000)
#define LP_CAP_US UINT64_C(108000000)
#define LP_PROFILE_NAME "RAM_30_120_300"
#define LP_SYNC_PROCESS_US UINT64_C(1000000)
#define LP_RETRY_US UINT64_C(3000000)
#else
#ifdef R2_LORA_BW_EXPERIMENT
/* Finite characterization namespace. Ordinary fast/slow decoders reject it. */
#define LP_SCHEDULE 5u
#else
#define LP_SCHEDULE LP_FAST
#endif
#define LP_SYNC_PROCESS_US UINT64_C(500000)
#define LP_RETRY_US UINT64_C(2000000)
#define LP_BEAT_US UINT64_C(4000000)
#define LP_META_BEATS 3u
#define LP_DISC_BEATS 1u
#define LP_DISC_PHASE 0u
#define LP_SYNC_OFFSET_US UINT64_C(2000000)
#define LP_MAX_DELAY_10MS 400u
#define LP_FRAME_END_US UINT64_C(3000000)
#define LP_LAUNCH_LATE_US UINT64_C(45000)
#define LP_HINT_RETIRE_US UINT64_C(250000)
#define LP_PREDICT_HINT_RETIRE_US LP_HINT_RETIRE_US
#define LP_PREDICT_HEADER_WINDOW_US LP_HEADER_WINDOW_US
#define LP_HEADER_WINDOW_US UINT64_C(250000)
#define LP_PENDING_US UINT64_C(1000000)
#define LP_RX_TUNE_LATE_US UINT64_C(200000)
#define LP_CONTROL_RETIRE_US UINT64_C(1300000)
#define LP_CONTROL_PROCESS_US UINT64_C(200000)
#define LP_RX_TUNE_LEAD_US UINT64_C(150000)
#define LP_LAUNCH_PAD_US UINT64_C(50000)
#define LP_FOLLOW_OFFSET_US UINT64_C(260000)
#define LP_FOLLOW_LATEST_US UINT64_C(300000)
#ifdef R2_LORA_BW_EXPERIMENT
/* Finite TX acquisition allowance only. Existing schedule5 receivers retain
 * their250ms header window and1s pending deadline; no lease is extended. */
#undef LP_FRAME_END_US
#undef LP_LAUNCH_LATE_US
#undef LP_LAUNCH_PAD_US
#undef LP_FOLLOW_LATEST_US
#define LP_FRAME_END_US UINT64_C(3500000)
#define LP_LAUNCH_LATE_US UINT64_C(150000)
#define LP_LAUNCH_PAD_US UINT64_C(160000)
#define LP_FOLLOW_LATEST_US UINT64_C(400000)
#endif
#define LP_HOLD_US UINT64_C(6000000)
#define LP_LEASE_US UINT64_C(18000000)
#ifdef R2_LORA_TX_DIAGNOSTIC
#define LP_CAP_US UINT64_C(72000000)
#define LP_PROFILE_NAME "RAM_DIAG_4_12"
#else
#define LP_CAP_US LP_HISTORY_CAP_US
#ifdef R2_LORA_BW_EXPERIMENT
#define LP_PROFILE_NAME "RAM_BW_AB_4_12"
#else
#define LP_PROFILE_NAME "RAM_LAB_4_12"
#endif
#endif
#endif
/* Maximum normal SYNC-to-SYNC interval from two independent control/SYNC
 * start pads; holdover lifetime is deliberately not the cadence gate. */
#define LP_CADENCE_BOUND_US (LP_BEAT_US+2*LP_LAUNCH_PAD_US+UINT64_C(10000))
#define LP_OWED_PAIR 1u
#define LP_OWED_META 2u
