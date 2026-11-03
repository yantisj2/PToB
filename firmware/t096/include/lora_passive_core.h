/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/lora_passive.h"
#define LP_FIT_CAPACITY 16u
/* Rolling accounting conservatively retains complete minute buckets through
 * minute end + one hour. Extra refusals are permitted, undercharging is not. */
/* Per-boot RAM accounting. The historical checkpoint layout is retained for
 * offline evidence readers only; native owners never restore or save it. */
struct lp_checkpoint {
 uint64_t minute,next_slot_us;
 uint32_t buckets[61],channel_end_ms[64];
 uint32_t seed,cursor,slot,serial;
 uint8_t pending,fh_disabled,schedule,owed;
};
typedef bool (*lp_save_fn)(void *,const struct lp_checkpoint *);
struct lp_tx {
 struct lp_checkpoint durable;
 uint8_t order[64];
 lp_save_fn save;void *storage;
 uint64_t slot_us,sync_end_us;
 bool ready,pair,unlaunched,manual;uint8_t reserved_control;
};
struct lp_reservation {uint32_t cursor,serial;uint8_t control;bool has_pair;uint64_t slot_us;};
/* A physical boot starts clean. Repeated service/profile transitions must
 * retain this object rather than calling start again. NULL save means RAM. */
bool lp_tx_start(struct lp_tx *,uint32_t seed,uint64_t now_us,uint64_t first_slot_us,uint32_t boot_ms);
#define LP_RETIRED_BOOTS 8u
struct lp_replay {
 uint64_t boot,meta_ns,retired[LP_RETIRED_BOOTS];
 uint32_t seed,cursor,serial;uint8_t next;
};
/* Preview then commit only after authenticated META and native admission. A
 * monotonic META UTC floor is an ordering guard, never an age/accuracy bound. */
bool lp_replay_admit(const struct lp_replay *,const struct lp_packet *);
void lp_replay_commit(struct lp_replay *,const struct lp_packet *);
uint32_t lp_duration(unsigned kind);
uint64_t lp_charged(const struct lp_checkpoint *,uint64_t now_us);
/* Historical offline wire2 restore API, not linked into native wire3.
 * Unknown old clock: retain ALL recorded debt for a full new hour, preserve
 * seed/cursor/index/serial, and impose channel cooldown from boot. A pending
 * pair still latches FHSS disabled. No elapsed old time is credited. */
bool lp_checkpoint_carry(struct lp_checkpoint *,uint64_t now_us,uint64_t first_slot_us,uint32_t boot_ms);
/* Historical offline restore API. Native wire3 uses lp_tx_start instead.
 * An initial record must be explicitly provisioned, never fabricated by boot.
 * Restoring an interrupted pair disables FHSS. now uses the checkpoint's
 * trustworthy time coordinate; an untrusted reboot must stay inhibited. */
bool lp_tx_restore(struct lp_tx *,const struct lp_checkpoint *,lp_save_fn,void *,bool time_trusted);
bool lp_tx_reserve(struct lp_tx *,uint64_t now_us,struct lp_reservation *);
bool lp_tx_reserve_control(struct lp_tx *,uint64_t now_us,struct lp_reservation *,bool force_meta);
/* One explicit slow-lab META+pair. RAM only: extra work preserves the periodic
 * grid/debt; due/owed work coalesces its ordinary event (manual=false).
 * Optional cap override applies to this reservation only. Full charge, cursor,
 * cooldown, physical completion and no-RF refund policies remain shared. */
bool lp_tx_reserve_utc(struct lp_tx *,uint64_t now_us,struct lp_reservation *,bool allow_over_cap);
bool lp_tx_confirm(struct lp_tx *,unsigned kind,uint64_t actual_end_us);
/* Capture progress in the per-boot RAM accounting object. Historical callers
 * may supply a save callback; the native owner has none and never flushes NV. */
bool lp_tx_capture_sync(struct lp_tx *,uint64_t actual_end_us);
bool lp_tx_capture_follow(struct lp_tx *,uint64_t actual_end_us);
bool lp_tx_flush(struct lp_tx *);
/* Slow scheduler: preserve an overdue logical obligation across a proven
 * pre-SYNC cancellation. Completion never replays a historical timestamp. */
void lp_tx_control_complete(struct lp_tx *);
bool lp_tx_has_deferred(const struct lp_tx *);
void lp_tx_cancel(struct lp_tx *,bool proven_no_fh_rf);
/* RAM-only, single-use rollback of this exact reservation. Caller MUST have
 * synchronous proof that no SetTx/RF occurred. Ordinary cancellation is not
 * proof: a DTS control may already have emitted. Owed work remains due. */
bool lp_tx_refund_no_rf(struct lp_tx *,const struct lp_reservation *);
void lp_tx_fault(struct lp_tx *);
/* Only after an actual quiescent ACK: retire both reserved hops, preserve
 * charge/cooldowns/owed work, then retry with fresh context/physical evidence. */
bool lp_tx_retire_quiescent(struct lp_tx *,uint64_t now_us);

/* Independent age interval; never filled from the packet being admitted. */
struct lp_age {
 /* Receiver interval evaluated at admission time, including oscillator error;
  * source_error_ns is an independently established source-event error bound. */
 uint64_t lower_ns,upper_ns,valid_until_us;
 uint32_t source_error_ns;bool source_bound_known;
};
struct lp_pair {uint64_t tick,tx_ns;};
struct lp_rx {
 struct lp_scope scope;
 struct lp_pair fit[LP_FIT_CAPACITY];
 uint8_t context[16],order[64];
 uint64_t meta_deadline_us,observed_deadline_us,tz_deadline_us,physical_deadline_us;
 uint64_t pending_tick,pending_epoch,pending_us,pending_deadline_us,last_us,last_tx_ns,last_tick;
 uint64_t hint_us,last_epoch,last_meta_ns;
 uint32_t serial,highest,pending_cursor,hint_cursor,accepted,rejected,missing;
 uint32_t rms_ns;int32_t rate_ppb;
 struct r2_tz tz;
 uint8_t fit_count;
 bool cached,bounded,pending,have_pair,hint,prediction,trained;
 /* Frozen last-trained UI map. New source fits cannot mutate this anchor.
  * Same emitter physical boot only; no source/RF/freshness/precision authority. */
 uint64_t display_tick,display_ns,display_us,display_epoch;
 int32_t display_rate_ppb;
 bool display_valid,display_acquiring;
};
void lp_rx_clear(struct lp_rx *);
bool lp_age_admit(const struct lp_age *,uint64_t event_ns,uint64_t now_us,uint64_t *maximum_age_us);
/* Packet arguments must have passed lp_decode with their current context.
 * Physical ingress must retain its original timestamp/incarnation. observed_us
 * is only the completion deadline check, never the precision event origin. */
bool lp_rx_meta(struct lp_rx *,const struct lp_packet *,const struct r2_group_config *,const struct lp_age *,
                uint64_t event_us,uint64_t observed_us);
bool lp_rx_hint(struct lp_rx *,const struct lp_packet *,uint64_t packet_end_us,uint64_t observed_us);
bool lp_rx_sync(struct lp_rx *,uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t event_us,uint64_t observed_us);
bool lp_rx_follow(struct lp_rx *,const struct lp_packet *,const struct lp_age *,uint64_t epoch,uint64_t observed_us);
void lp_rx_expire(struct lp_rx *,uint64_t now_us);
bool lp_rx_estimate(const struct lp_rx *,uint64_t tick,uint64_t epoch,uint64_t now_us,uint64_t *utc_ns);
/* Same-boot human watch extrapolation. Expiry cannot renew any permission.
 * Discontinuous local time/capture permanently discards this display cache. */
bool lp_rx_display_estimate(struct lp_rx *,uint64_t tick,uint64_t epoch,uint64_t now_us,uint64_t *utc_ns);
/* Original envelope only. This never qualifies absolute uncertainty or grants
 * GM/election, BLE relay, RC7/RC9 or USB authority. */
bool lp_rx_envelope_current(const struct lp_rx *,const struct lp_age *,uint64_t now_us);
