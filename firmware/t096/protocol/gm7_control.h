/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_codec.h"
struct gm7_peer_control {
  struct gm7_control control;
  uint64_t control_until;
  uint64_t last_diag[2];
};
/* Authenticated receipt policy, NOT proof of remote original-send age/UTC.
 * All results are original native-receipt anchored, processing consumes them.
 * Caller applies owner scope/terminal policy before committing any result. */
int gm7_control_check(const struct gm7_peer_control *,const struct gm7_packet *,
 uint64_t rx,uint64_t now,uint64_t *original,uint64_t *until);
int gm7_control_diagnostic(struct gm7_peer_control *,const struct gm7_packet *,
 uint64_t rx,uint64_t now,uint64_t *until);
bool gm7_control_live(const struct gm7_peer_control *,uint64_t now);
bool gm7_control_timing_current(const struct gm7_peer_control *);
