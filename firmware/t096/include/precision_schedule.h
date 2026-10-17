/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Controller role classification; no clock authority is inferred here. */
bool r2_precision_radio(void *owner);
/* Serialized RC PA owner only, after a fresh, legal captured TX. Eight us
 * per call; neither interval, event counter, payload nor train is recreated. */
int r2_precision_phase_step(uint8_t handle, int step);
