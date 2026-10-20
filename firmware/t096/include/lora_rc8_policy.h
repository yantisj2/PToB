/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Portable wire3 policy. Hardware ownership remains outside this module. */
#pragma once
#include "../protocol/lora_rc8.h"
#define RC8_CORRECTION_US UINT64_C(8000000)
#define RC8_HOLD_US UINT64_C(12000000)
#define RC8_CONTEXT_US UINT64_C(240000000)
#define RC8_META_REFRESH_US UINT64_C(120000000)
#define RC8_ANCHOR_MAX_US UINT64_C(3600000000)
#define RC8_ANCHOR_WIDTH_NS UINT64_C(1000000000)
#define RC8_DRAIN_US UINT64_C(200000)
#define RC8_HISTORY 2u
enum rc8_profile {RC8_PROFILE_GM,RC8_PROFILE_RC};
enum rc8_command {RC8_CMD_RX,RC8_CMD_TX,RC8_CMD_RANGING};
struct rc8_enrollment {uint64_t factory,transmitter_factory;bool enrolled;};
struct rc8_requests {bool tx,rx;};
bool rc8_is_transmitter(const struct rc8_enrollment *);
struct rc8_requests rc8_requests_default(const struct rc8_enrollment *);
bool rc8_command_allowed(const struct rc8_enrollment *,enum rc8_profile,bool,enum rc8_command);
/* Owner-frozen original map. until_tick is the exclusive MINIMUM original
 * map/source/session/permission limit. Transitions atomically shorten it.
 * All ns fields share scope.scale; tick domain is16MHz TIMER3+capture epoch. */
enum rc8_map_mode {RC8_MAP_LINEAR,RC8_MAP_SOURCE_Q16,RC8_MAP_DELIVERED};
struct rc8_export {
 struct rc8_scope scope;
 uint64_t from_tick,until_tick,anchor_tick,anchor_ns,capture_epoch,created_ns,expires_ns;
 uint32_t map_generation,map_age_us,upstream_bound_ns;
 uint64_t rate_q16;
 int64_t phase_ns;
 int32_t rate_ppb;
 uint8_t mode;
 bool authenticated,valid;
};
enum rc8_event_kind {RC8_EVENT_CALLBACK,RC8_EVENT_RX_DONE,RC8_EVENT_TX_DONE};
struct rc8_event {uint64_t tick,capture_epoch,local_us;uint32_t generation;enum rc8_event_kind kind;};
bool rc8_export_select(struct rc8_export *,const struct rc8_export *,const struct rc8_export *,bool actually_gm,uint64_t factory,uint64_t boot);
bool rc8_export_close(struct rc8_export *,uint64_t cut_tick,uint64_t epoch);
/* Admission projection only: this does not fabricate a completed radio event. */
bool rc8_export_valid_at(const struct rc8_export *,uint64_t tick,uint64_t epoch);
bool rc8_export_event(struct rc8_correction *,const struct rc8_export *,uint32_t,struct rc8_event);
struct rc8_transmission {
 struct rc8_export map;
 struct rc8_packet packet;
 struct rc8_correction previous;
 uint64_t preparation_tick;
 uint32_t generation;
 bool pending,previous_valid;
};
bool rc8_tx_prepare(struct rc8_transmission *,const struct rc8_enrollment *,enum rc8_profile,bool requested,
 const struct rc8_export *,const struct r2_group_config *,uint32_t sequence,uint32_t generation,uint64_t tick,uint64_t epoch);
bool rc8_tx_complete(struct rc8_correction *,struct rc8_transmission *,struct rc8_event);
/* Independent optional time anchor, NOT a live-source permission grant.
 * Bound width<=100ms initially, oscillator bound<=100000ppb, assurance<=1h
 * and<=1s width. These are unqualified policy limits, not measured accuracy. */
struct rc8_bootstrap {
 struct rc8_scope scope;
 uint64_t at_us,until_us,lower_ns,upper_ns;
 uint32_t rate_bound_ppb,min_sequence;
};
struct rc8_history {struct rc8_event rx;uint32_t sequence;bool occupied;};
struct rc8_sample {struct rc8_event rx;struct rc8_correction tx;bool valid;};
/* One non-overlapping block of at most16 accepted physical event pairs.
 * X=elapsed RX ticks/1600 (100us), Y=TX elapsed minus nominal RX elapsed ns.
 * This affine residual describes relative link consistency, never accuracy. */
struct rc8_link_fit {
 uint64_t tick,ns;
 int64_t xx,xy,yy;
 int32_t x,y;
 uint32_t rms_ns;
 uint8_t n;
};
_Static_assert(sizeof(struct rc8_link_fit)==56,"RC8 link-fit reserve");
enum rc8_input_state {RC8_OFF,RC8_ACQUIRING,RC8_AVAILABLE,RC8_EXPIRED};
enum rc8_assurance {RC8_FRESHNESS_UNKNOWN,RC8_STREAM_CONTINUITY,RC8_ANCHORED_INTERVAL};
enum rc8_authority {RC8_AUTHORITY_UNKNOWN,RC8_ORIGINAL_ENVELOPE_CURRENT,RC8_ORIGINAL_ENVELOPE_EXPIRED};
enum rc8_selected {RC8_SELECT_NONE,RC8_SELECT_BLE,RC8_SELECT_LORA,RC8_SELECT_LOCAL_GM};
struct rc8_receiver {
 uint64_t factory,transmitter_factory,metadata_until_us,last_timing_us,retired_boot;
 struct rc8_scope scope;
 uint8_t context[RC8_CONTEXT_BYTES],retired_context[RC8_CONTEXT_BYTES];
 struct rc8_quality quality;
 uint64_t diagnostic_us;
 uint32_t diagnostic_sequence,snapshot_age_ms,withdrawal_sequence;uint16_t diagnostic_events;
 struct rc8_bootstrap trust;
 struct rc8_history history[RC8_HISTORY];
 struct rc8_sample sample;
 struct rc8_link_fit fit;
 struct rc8_event queued_event;
 uint8_t queued[RC8_META_BYTES];
 uint32_t generation,highest_sequence,accepted,rejected,replayed,overflow,unmatched,metadata_misses;
 uint8_t queued_size;
 bool enabled,cached,anchored,continuity,queued_valid,metadata_known,time_valid;
};
/* Cold init only. All policy calls serialized by one future driver worker;
 * ISR latches cause/edge/generation and wakes it. No concurrent queue implied. */
bool rc8_receiver_init(struct rc8_receiver *,const struct rc8_enrollment *);
void rc8_receiver_switch(struct rc8_receiver *,bool enabled);
/* Optional independent trust installation; never required for functional
 * UNKNOWN-freshness samples. It cannot be called based solely on RC8 input. */
bool rc8_receiver_bootstrap(struct rc8_receiver *,const struct rc8_bootstrap *,uint64_t now_us);
/* Freeze original payload+physicalRxDone before clear/FIFO reuse/rearm. */
bool rc8_receiver_freeze(struct rc8_receiver *,const uint8_t *,size_t,struct rc8_event);
bool rc8_receiver_process(struct rc8_receiver *,const struct r2_group_config *,uint64_t now_us);
/* Only a separately proved ACTUAL local GM origin may retire an older foreign
 * context via a monotonic matching-self companion. Never creates a sample. */
bool rc8_receiver_process_origin(struct rc8_receiver *,const struct r2_group_config *,uint64_t now_us,uint64_t actual_origin_boot,uint32_t actual_origin_epoch);
enum rc8_input_state rc8_receiver_state(const struct rc8_receiver *,uint64_t now_us);
enum rc8_assurance rc8_receiver_assurance(const struct rc8_receiver *,uint64_t now_us);
enum rc8_authority rc8_receiver_authority(const struct rc8_receiver *,uint64_t now_us);
bool rc8_receiver_read(const struct rc8_receiver *,uint64_t,struct rc8_sample *,struct rc8_scope *);
/* Explicit integration choice: unverified functional RC8 is allowed only when
 * normal BLE is unusable and allow_unverified is true. No RC7/GM/RC9 authority. */
enum rc8_selected rc8_select(bool ble_usable,enum rc8_input_state,enum rc8_assurance,bool allow_unverified);
uint32_t rc8_output_bound(const struct rc8_receiver *);
enum rc8_phase {RC8_PHASE_IDLE,RC8_PHASE_DMA,RC8_PHASE_RX,RC8_PHASE_RX_FROZEN,
 RC8_PHASE_TX_PREP,RC8_PHASE_TX_AIR,RC8_PHASE_TX_DONE,RC8_PHASE_FOLLOW};
enum rc8_stop_result {RC8_STOP_WAIT,RC8_STOP_QUIESCENT,RC8_STOP_FAULT};
struct rc8_stop {uint64_t started_us,deadline_us;enum rc8_phase from;bool stopping,fault,quiescent;};
void rc8_stop_begin(struct rc8_stop *,enum rc8_phase,uint64_t);
enum rc8_stop_result rc8_stop_poll(struct rc8_stop *,uint64_t,bool dma_retired,bool radio_standby,bool fem_off,bool capture_released);
struct rc8_gesture {uint64_t started_us;bool pressed,eligible,claimed,consumed;};
bool rc8_gesture_update(struct rc8_gesture *,bool pressed,bool eligible,uint64_t);
_Static_assert(sizeof(struct rc8_receiver)<=712,"RC8 receiver reserve");
_Static_assert(sizeof(struct rc8_export)<=168,"RC8 export reserve");
_Static_assert(sizeof(struct rc8_packet)<=80,"RC8 packet reserve");
_Static_assert(sizeof(struct rc8_transmission)<=320,"RC8 TX reserve");
_Static_assert(sizeof(struct rc8_stop)<=24,"RC8 stop reserve");
_Static_assert(sizeof(struct rc8_gesture)<=16,"RC8 gesture reserve");
