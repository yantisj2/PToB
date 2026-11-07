/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "radio_precision.h"
/* PT-owner publication. Kernel permission, HF capture and root source
 * coordinates are distinct. A retained map grants no received permission. */
struct r2_gm7_timing {
 struct r2_radio_scope scope;
 struct r2_radio_precision_view view;
 r2_servo_map_t map;
 uint64_t counter_epoch,last_correction,evaluated_tick,until_us,first_tx_lo,first_tx_hi,first_tx_derived_end_hi,last_tx;
 int32_t tx_phase_us;
 bool first_tx_observed_complete;
 int64_t utc_offset_ns;
 uint32_t capture_gen,shadow_gen,phase_enclosure_us;
 bool retained;
};
/* Coherent query-only follower estimator facts; no timing authority. */
struct r2_gm7_follow_stats {
 uint32_t count,span_us,observations,resets,saturated,arithmetic_faults,unavailable;
 int32_t rate;
 uint32_t quality_count,flags; /* fit1,output2,fault4,PLLstate<<8,reason<<16 */
};
_Static_assert(sizeof(struct r2_gm7_follow_stats)==40,"Existing firstTX storage");
enum r2_gm7_follow_refusal {
 R2_FOLLOW_UNSUPPORTED=1u<<0,R2_FOLLOW_STOPPED=1u<<1,
 R2_FOLLOW_UNMANAGED=1u<<2,R2_FOLLOW_OWNER_ERROR=1u<<3,
 R2_FOLLOW_BINDING=1u<<4,R2_FOLLOW_CAPTURE=1u<<5,
 R2_FOLLOW_COUNTER=1u<<6,R2_FOLLOW_BACKWARD=1u<<7,
 R2_FOLLOW_SCOPE_EXPIRED=1u<<8,R2_FOLLOW_GRANT_EXPIRED=1u<<9,
 R2_FOLLOW_PHYSICAL_MISSING=1u<<10,R2_FOLLOW_PHYSICAL_EXPIRED=1u<<11,
 R2_FOLLOW_DATA_MISSING=1u<<12,R2_FOLLOW_DATA_EXPIRED=1u<<13,
 R2_FOLLOW_FIT=1u<<14,R2_FOLLOW_OUTPUT=1u<<15,R2_FOLLOW_SERVO_FAULT=1u<<16,
 R2_FOLLOW_PT_UTC=1u<<17,R2_FOLLOW_EFFECTIVE_EXPIRED=1u<<18
};
struct r2_gm7_follow_diagnostic {
 struct r2_radio_scope scope;
 struct r2_gm7_follow_stats stats;
 uint64_t sampled,until,counter_epoch,last_correction;
 uint32_t train,capture_gen,scope_resets,scope_reset_bits,stops,stop_pc;
 bool current;
 /* One query capture while the PT publication and current input grant are
  * locked. HF ticks and kernel microseconds remain separate domains. */
 struct r2_radio_scope grant;
 uint64_t query_us,query_tick,query_epoch,physical_until_tick,data_until_tick,effective_until_us;
 uint64_t header_root,header_boot,header_tick,header_epoch;
 uint32_t header_source,header_train,header_capture,header_publication;
 uint32_t refusals,rms_ns,quality_bins;
 int owner_error;
 bool paired,managed,stopped,binding_current,scope_equal,utc_at_sample,utc_readable_now,map_at_sample;
};
void r2_radio_service_gm7_follow_diagnostic(struct r2_gm7_follow_diagnostic *,uint64_t now);
/* Last progressing authenticated DIRECT header, before fitting. No time grant. */
void r2_radio_service_gm7_contact(uint64_t *root,uint64_t *boot,uint64_t *tick,uint64_t *counter_epoch);
void r2_radio_service_gm7_timing(struct r2_gm7_timing *,uint64_t now);
struct r2_group_client;
struct r2_enrollment;
struct r2_gnss_record;
/* GM CONTROL publishes under its lifecycle lock; no HCI in this call. Resume
 * is bounded by an original live owner/control permission, never a new lease.
 * Native consumption follows after releasing that lock. */
void r2_radio_service_gm7_publish(struct r2_radio_scope,
 const struct r2_group_client *,const struct r2_enrollment *,int64_t utc_ns,
 uint64_t sampled_us,uint64_t resume_until_us);
void r2_radio_service_gm7_tick(uint64_t now,const struct r2_gnss_record *);
void r2_radio_service_gm7_state(bool *stopped,bool *managed,bool *binding);
/* Copy a scoped retained delivered map into an own source coordinate. Only
 * after old RF associations have drained; independent local GNSS permission
 * remains separately required for every subsequent publication. */
int r2_radio_service_gm7_promote(struct r2_source_clock *,uint64_t root,uint64_t boot,
 uint32_t timing_epoch,uint32_t train,uint32_t new_source_epoch,unsigned association,struct r2_capture now);

int r2_radio_service_gm7_phase_step(int step);

#ifdef R2_BLE_CAUSAL_EVENTS
/* Same withdrawal authority; descriptive owner reason/generation at caller. */
void r2_radio_service_gm7_withdraw_cause(uint32_t reason,uint32_t generation);
#endif
