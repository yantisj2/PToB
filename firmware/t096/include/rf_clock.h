/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "precision.h"
/* Read-only RF-page observation. Both owners consume ONE caller-captured
 * HF tick/epoch and paired kernel time. Positive rate advances UTC faster
 * than nominal local HF. No value here is an authority/export permission.
 * fresh_until_us is an exclusive presentation deadline in kernel us; callers
 * must expire cached numeric comparisons at the earlier owner deadline.
 * Provisional is an authenticated observer estimate, not trained precision.
 * A first LoRa pair supplies time but has no measured rate. */
struct r2_rf_clock_sample {
 uint64_t time_ns,root,boot,fresh_until_us;
 uint32_t source_epoch,train;
 int32_t rate_ppb;
 bool available,fresh,utc,rate_valid,provisional;
};
/* GM7 direct BLE follower or qualified own-GM source map. No coarse clock,
 * retained handoff, LoRa feedback or rounded UTC-us fallback is consumed. */
void r2_radio_service_rf_clock(struct r2_rf_clock_sample *,struct r2_capture,uint64_t now_us);
/* Independent authenticated wire3 receiver observer, never the TX export. */
void r2_lora_rf_clock(struct r2_rf_clock_sample *,struct r2_capture,uint64_t now_us);
/* Runtime profile dispatcher: disabled/pending/failed profiles return empty. */
void r2_service_rf_clock(struct r2_rf_clock_sample *,struct r2_capture,uint64_t now_us);
void r2_normal_service_rf_clock(struct r2_rf_clock_sample *,struct r2_capture,uint64_t now_us);
void r2_dynamic_service_rf_clock(struct r2_rf_clock_sample *,struct r2_capture,uint64_t now_us);

#include "ui_read.h"
bool r2_service_rf_clock_try(struct r2_rf_clock_sample *,struct r2_capture,uint64_t,r2_ui_read_boundary_fn,void *);
bool r2_normal_service_rf_clock_try(struct r2_rf_clock_sample *,struct r2_capture,uint64_t,r2_ui_read_boundary_fn,void *);
bool r2_dynamic_service_rf_clock_try(struct r2_rf_clock_sample *,struct r2_capture,uint64_t,r2_ui_read_boundary_fn,void *);
bool r2_radio_service_rf_clock_try(struct r2_rf_clock_sample *,struct r2_capture,uint64_t);
