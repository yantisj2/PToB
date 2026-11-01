/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/lora_passive.h"
#define LP_BEAT_US UINT64_C(4000000)
#define LP_HOLD_US UINT64_C(6000000)
#define LP_LEASE_US UINT64_C(18000000)
#define LP_TRAIN_US UINT64_C(180000000)
#define LP_WINDOW_US UINT64_C(300000000)
#define LP_CAP_US UINT64_C(1080000000)
#define LP_HOUR_US UINT64_C(3600000000)
#define LP_MINUTE_US UINT64_C(60000000)
#define LP_FIT_CAPACITY 16u
/* Rolling accounting conservatively retains complete minute buckets through
 * minute end + one hour. Extra refusals are permitted, undercharging is not. */
struct lp_checkpoint {
 uint64_t minute,next_slot_us;
 uint32_t buckets[61],channel_end_ms[64];
 uint32_t seed,cursor,slot,serial;
 uint8_t pending,fh_disabled;
};
typedef bool (*lp_save_fn)(void *,const struct lp_checkpoint *);
struct lp_tx {
 struct lp_checkpoint durable;
 uint8_t order[64];
 lp_save_fn save;void *storage;
 uint64_t slot_us,sync_end_us;
 bool ready,pair;
};
struct lp_reservation {uint32_t cursor,serial;uint8_t control;bool has_pair;uint64_t slot_us;};
uint32_t lp_duration(unsigned kind);
uint64_t lp_charged(const struct lp_checkpoint *,uint64_t now_us);
/* Unknown old clock: retain ALL recorded debt for a full new hour, preserve
 * seed/cursor/index/serial, and impose channel cooldown from boot. A pending
 * pair still latches FHSS disabled. No elapsed old time is credited. */
bool lp_checkpoint_carry(struct lp_checkpoint *,uint64_t now_us,uint64_t first_slot_us,uint32_t boot_ms);
/* An initial record must be explicitly provisioned, never fabricated by boot.
 * Restoring an interrupted pair disables FHSS. now uses the checkpoint's
 * trustworthy time coordinate; an untrusted reboot must stay inhibited. */
bool lp_tx_restore(struct lp_tx *,const struct lp_checkpoint *,lp_save_fn,void *,bool time_trusted);
bool lp_tx_reserve(struct lp_tx *,uint64_t now_us,struct lp_reservation *);
bool lp_tx_confirm(struct lp_tx *,unsigned kind,uint64_t actual_end_us);
/* Capture pair progress in RAM. The durable precharge stays pending until
 * the next reservation includes the completed cursor/cooldowns, or a clean
 * quiescent stop flushes it. Reset before then disables FHSS; no uncharged
 * RF is enabled. Flush must run only after confirmed radio quiescence. */
bool lp_tx_capture_sync(struct lp_tx *,uint64_t actual_end_us);
bool lp_tx_capture_follow(struct lp_tx *,uint64_t actual_end_us);
bool lp_tx_flush(struct lp_tx *);
void lp_tx_cancel(struct lp_tx *,bool proven_no_rf);
void lp_tx_fault(struct lp_tx *);

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
/* Original envelope only. This never qualifies absolute uncertainty or grants
 * GM/election, BLE relay, RC7/RC9 or USB authority. */
bool lp_rx_envelope_current(const struct lp_rx *,const struct lp_age *,uint64_t now_us);
