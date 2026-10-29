/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../include/precision.h"
/* Exact anchor and bounded fixed-point approximation of the retained affine
 * rate/remaining1ppm slew. Declared numerical enclosure <=4ns over1.5s; does
 * not include RF/fit/oscillator/UTC uncertainty or confer source permission. */
int gm7_promote_clock(struct r2_source_clock *, const r2_servo_map_t *,
                      int64_t utc_offset, struct r2_capture now,
                      uint32_t local_source_epoch, unsigned association);
