/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/gm7_owner.h"
#include "clock.h"
#include "precision.h"
/* Immutable snapshots from the actual time/capture owners. Routine PPS/fit
 * publications are not source, capture or UTC-association generations. */
int gm7_source_snapshot(struct gm7_source *, const struct r2_clock *,
                        const struct r2_source_clock *, struct r2_capture,
                        uint64_t sampled_us);
/* Descriptive producer only; no capture, radio, election or sampling requests.
 */
uint32_t gm7_duration_ms(uint64_t now, uint64_t since);
int gm7_role_snapshot(struct gm7_owner *, struct gm7_packet *, uint64_t now);
int gm7_gnss_snapshot(struct gm7_owner *, struct gm7_packet *,
                      const struct r2_clock_view *, uint64_t now);
