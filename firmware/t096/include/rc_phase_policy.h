/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Correct slow relative RF drift before consuming slot clearance. Keep a 250us
 * deadband, one fractional 8us step per >=62.5ms using the ticker remainder,
 * and never salvage an illegal/stale event. Numerical clock maps are separate.
 * This is an experimental bounded RF-placement servo, not a precision bound. */
static inline int rc_phase_step(int64_t error_ns, int64_t clearance_ns,
                                uint64_t tx, uint64_t now, uint64_t previous,
                                bool admitted) {
  if (!admitted || clearance_ns <= 250000 || now < tx || now-tx >= 1000000 ||
      (previous && (tx < previous || tx-previous < 1000000)) ||
      error_ns > clearance_ns || error_ns < -clearance_ns)
    return 0;
  return error_ns > 250000 ? 1 : error_ns < -250000 ? -1 : 0;
}
