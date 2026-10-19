/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "rc_shadow.h"

#define PC_RECORDS 32u
#define PC_TICKS_MS UINT64_C(16000)
#define PC_PACKET_TICKS UINT64_C(1000000) /* Direct-GM 62.5 ms train. */
#define PC_PHASE_GUARD_TICKS (UINT64_C(8)*PC_TICKS_MS)
enum pc_phase {PC_BASELINE, PC_ACQUIRE, PC_LISTEN, PC_HOLD, PC_DONE, PC_FAILED};
/* Modes: fixed-budget warm/cold estimator, shadow/radio HOLD. No sleep. */
enum pc_mode {PC_WARM_SHADOW=1, PC_COLD_SHADOW, PC_WARM_RADIO, PC_COLD_RADIO, PC_CADENCE_SHADOW, PC_ALIGNED_SHADOW};
struct pc_record {
 uint64_t rx,available,remote_ns,predicted_ns,epoch;
 int64_t error_ns;
 uint32_t sequence,trial,phase,generation,cycle;
 bool valid,train;
};
struct pc_experiment {
 struct rc_shadow_map latest,frozen;
 struct pc_record records[PC_RECORDS];
 uint64_t requested,first_pair,listen_until,hold_from,hold_until,rf_closed;
 uint64_t last_tick,epoch,reset_from;
 uint64_t phase_rx,phase_available,phase_epoch,phase_root,phase_boot;
 uint64_t commanded,alignment_rx;
 uint32_t phase_root_epoch;
 uint32_t trial,phase,mode,listen_ms,hold_ms,produced,overwritten;
 uint32_t pairs,training_pairs,first_return_sequence,failures;
 uint32_t cycle,cycles,missed_bursts,burst_pairs,cycle_generation;
 bool have_map,have_frozen,reset_pending;
};
void pc_init(struct pc_experiment *);
int pc_begin(struct pc_experiment *,unsigned,uint32_t,uint32_t,uint64_t,uint64_t);
int pc_begin_cadence(struct pc_experiment *,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t);
int pc_begin_aligned(struct pc_experiment *,uint32_t,uint32_t,uint32_t,uint64_t,uint64_t);
void pc_tick(struct pc_experiment *,uint64_t,uint64_t);
bool pc_observe(struct pc_experiment *,const struct rc5_reference *,uint64_t);
void pc_offer(struct pc_experiment *,const struct rc_shadow_map *);
bool pc_witness_map(const struct pc_experiment *,struct r2_capture,struct rc_shadow_map *);
bool pc_read(const struct pc_experiment *,uint32_t,struct pc_record *);
bool pc_quiet(const struct pc_experiment *);
/* Parent health during deliberate training gaps, never output permission. */
bool pc_ingress_live(bool,bool,const struct rc5_state *,uint64_t);

#ifdef R2_PC_PROFILE
void r2_pc_init(void);
int r2_pc_begin(unsigned,uint64_t);
int r2_pc_begin_cadence(uint32_t,uint64_t);
int r2_pc_begin_aligned(uint32_t,uint64_t);
void r2_pc_poll(void);
bool r2_pc_reset(uint64_t,uint64_t);
bool r2_pc_observe(const struct rc5_reference *,uint64_t);
void r2_pc_offer(const struct rc_shadow_map *);
bool r2_pc_witness_map(struct r2_capture,struct rc_shadow_map *);
int r2_pc_json(char *,size_t,uint32_t,int);
bool r2_pc_quiet(void);
void r2_pc_closed(uint64_t);
#endif
