/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#if defined(R2_LORA_FIXED500_EXPERIMENT) && (!defined(R2_LORA_PASSIVE_SLOW) || defined(R2_LORA_BW_EXPERIMENT) || defined(R2_LORA_PASSIVE_FAST))
#error "Fixed500 requires slow without another experiment"
#endif
#if defined(R2_LORA_TX_DIAGNOSTIC) && (!defined(R2_LORA_PASSIVE_FAST) || defined(R2_LORA_PASSIVE_SLOW))
#error "Diagnostic TX requires explicit passive fast only"
#endif
#if defined(R2_LORA_BW_EXPERIMENT) && ((!defined(R2_LORA_PASSIVE_FAST) && !defined(R2_LORA_PASSIVE_SLOW)) || (defined(R2_LORA_PASSIVE_FAST) && defined(R2_LORA_PASSIVE_SLOW)) || defined(R2_LORA_TX_DIAGNOSTIC))
#error "Bandwidth experiment requires one passive schedule without diagnostic TX"
#endif
#define LP_FAST 1u
#define LP_SLOW 3u
#define LP_HISTORY_CAP_US UINT64_C(1080000000)
#ifdef R2_LORA_FIXED500_EXPERIMENT
#define LP_ORDER_BYTES 1u /* Fixed PHY never derives or reads a hop order. */
#define LP_CONTROL_LAUNCH_LATE_US UINT64_C(3000000)
#define LP_CONTROL_LAUNCH_PAD_US UINT64_C(3250000)
#else
#define LP_ORDER_BYTES 64u
#define LP_CONTROL_LAUNCH_LATE_US LP_LAUNCH_LATE_US
#define LP_CONTROL_LAUNCH_PAD_US LP_LAUNCH_PAD_US
#endif
#define LP_TRAIN_US UINT64_C(180000000)
#define LP_WINDOW_US UINT64_C(300000000)
#define LP_HOUR_US UINT64_C(3600000000)
#define LP_MINUTE_US UINT64_C(60000000)
#ifdef R2_LORA_BW_EXPERIMENT
static inline unsigned lp_bw_arm(uint32_t cursor){
#ifdef R2_LORA_PASSIVE_SLOW
 unsigned step=(cursor/2)%10;return step<5?step:9-step;
#else
 unsigned slot=(cursor/2)%4;return slot==1||slot==2;
#endif
}
static inline uint32_t lp_bw_packet_budget_us(uint32_t cursor,unsigned bytes){
 if(bytes!=24&&bytes!=32)return 0;
#ifdef R2_LORA_PASSIVE_SLOW
 static const uint32_t sync[5]={231951,157968,83984,46992,28496};
 static const uint32_t follow[5]={268814,182544,96272,53136,31568};
 return bytes==24?sync[lp_bw_arm(cursor)]:follow[lp_bw_arm(cursor)];
#else
 return lp_bw_arm(cursor)?(bytes==24?28496:31568):(bytes==24?231951:268814);
#endif
}
#endif
#ifdef R2_LORA_PASSIVE_SLOW
#ifdef R2_LORA_FIXED500_EXPERIMENT
#define LP_SCHEDULE 8u
/* Frozen build-time prior only. Positive values advance the TX event UTC to
 * its RX HeaderValid event. This is not learned from the live PPS witness. */
#ifndef R2_LORA_FIXED500_EVENT_DELAY_NS
#define R2_LORA_FIXED500_EVENT_DELAY_NS 0
#endif
#define LP_FIXED_EVENT_DELAY_NS ((int64_t)R2_LORA_FIXED500_EVENT_DELAY_NS)
#if R2_LORA_FIXED500_EVENT_DELAY_NS < -1000000000 || R2_LORA_FIXED500_EVENT_DELAY_NS > 1000000000
#error "Fixed500 event correction exceeds one second"
#endif
#elif defined(R2_LORA_BW_EXPERIMENT)
#define LP_SCHEDULE 7u
#else
#define LP_SCHEDULE LP_SLOW
#endif
/* P30/D120/M300. Schedule2 is deliberately incompatible. Each precision
 * slot retains the original RF/source/capture fences, with or without DTS. */
#define LP_BEAT_US UINT64_C(30000000)
#define LP_META_BEATS 10u
#define LP_DISC_BEATS 4u
#define LP_DISC_PHASE 1u
#ifdef R2_LORA_FIXED500_EXPERIMENT
#undef LP_DISC_BEATS
#define LP_DISC_BEATS 0u
#endif
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
#ifdef R2_LORA_FIXED500_EXPERIMENT
#undef LP_CAP_US
#define LP_CAP_US UINT64_C(90000000)
#define LP_PROFILE_NAME "RAM_FIXED500_30_300"
#elif defined(R2_LORA_BW_EXPERIMENT)
#define LP_PROFILE_NAME "RAM_BW_SWEEP_30"
#else
#define LP_PROFILE_NAME "RAM_30_120_300"
#endif
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
/* Fixed500 META retains HeaderValid until CRC/RxDone. Its full physical
 * packet airtime must not consume the original post-packet worker allowance.
 * This is a processing limit only; the captured event and leases stay fixed. */
#ifdef R2_LORA_FIXED500_EXPERIMENT
#define LP_META_PROCESS_US (LP_CONTROL_PROCESS_US+UINT64_C(787456))
#else
#define LP_META_PROCESS_US LP_CONTROL_PROCESS_US
#endif
/* Maximum normal SYNC-to-SYNC interval from two independent control/SYNC
 * start pads; holdover lifetime is deliberately not the cadence gate. */
#define LP_CADENCE_BOUND_US (LP_BEAT_US+2*LP_LAUNCH_PAD_US+UINT64_C(10000))
#define LP_OWED_PAIR 1u
#define LP_OWED_META 2u
