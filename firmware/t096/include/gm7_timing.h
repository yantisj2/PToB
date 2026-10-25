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
