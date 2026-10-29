/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Functional scheduler envelope only. These are explicitly unverified
 * operating allowances, never oscillator/BOM/UTC or hardware qualification.
 * Actual chain ceilings: nRF52840 TX1M<=1us, RX1M<=10us. */
#define GM7_PHASE_TX_CHAIN_US 1u
#define GM7_PHASE_RX_CHAIN_US 10u
#define GM7_PHASE_CAPTURE_QUANTIZATION_US 2u
#define GM7_PHASE_PAIRED_ASSOCIATION_US                                        \
  200u /* two validated <=100us brackets */
#define GM7_PHASE_PROPAGATION_ALLOWANCE_US 100u
#define GM7_PHASE_IMPLEMENTATION_ALLOWANCE_US 250u
#define GM7_PHASE_BASE_US                                                      \
  (GM7_PHASE_TX_CHAIN_US + GM7_PHASE_RX_CHAIN_US +                             \
   GM7_PHASE_CAPTURE_QUANTIZATION_US + GM7_PHASE_PAIRED_ASSOCIATION_US +       \
   GM7_PHASE_PROPAGATION_ALLOWANCE_US + GM7_PHASE_IMPLEMENTATION_ALLOWANCE_US)
/* Add only elapsed time after the saved evaluation; no reset or double age. */
static inline bool gm7_phase_grow(uint32_t original, uint64_t from_tick,
                                  uint64_t now_tick, uint32_t *out) {
  if (!out || original == UINT32_MAX || now_tick < from_tick ||
      now_tick - from_tick > 80000000)
    return false;
  uint64_t growth = (now_tick - from_tick + 79999) / 80000; /*200ppm at16MHz*/
  if (growth > UINT32_MAX - original)
    return false;
  *out = original + (uint32_t)growth;
  return true;
}

/* Prospective recurring G1 placement, exact62.5ms train on UTC quarter-frame
 * boundaries. RX ends58/120ms; <=2.5ms prepare+0.5ms arbitration margin leaves
 * >=1.5/2ms separation, and <=3ms complete G1 slot stays outside GM7 TX/RX. */
static inline bool gm7_phase_plan(uint64_t now, uint64_t earliest,
                                  uint32_t phase, uint32_t prepare_us,
                                  uint32_t slot_us, uint64_t deadline,
                                  uint64_t *start) {
  if (!start || phase >= 62500 || prepare_us > 2500 || slot_us > 3000 ||
      !slot_us || now > UINT64_MAX - 65000)
    return false;
  uint64_t minimum = now + prepare_us + 500;
  if (earliest > minimum)
    minimum = earliest;
  uint64_t target = (minimum / 62500) * 62500;
  if (target > UINT64_MAX - phase)
    return false;
  target += phase;
  if (target < minimum) {
    if (target > UINT64_MAX - 62500)
      return false;
    target += 62500;
  }
  if (target < minimum || target > UINT64_MAX - slot_us || deadline <= target ||
      deadline - target < slot_us)
    return false;
  *start = target;
  return true;
}

/* Existing SyncInfo advertises500ppm SCA. One8us nudge per distinct62500us
 * capture contributes<=128.1ppm; assumed200ppm clock plus2us implementation
 * allowance contributes<=232.1ppm, total<361ppm<500ppm. No repeated stale
 * sample nudges; an extreme clock can exhaust phase margin and must retire. */
static inline int gm7_phase_nudge(int32_t error, uint64_t sample, uint64_t now,
                                  uint64_t last_attempt) {
  if (!sample || sample <= last_attempt || now < sample ||
      now - sample > 4000000 ||
      (last_attempt && sample - last_attempt < 999360))
    return 0;
  return error > 250 ? -1 : error < -250 ? 1 : 0;
}
