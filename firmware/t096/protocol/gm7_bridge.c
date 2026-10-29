/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_bridge.h"
#include <limits.h>
bool gm7_bridge_valid(struct gm7_bridge b) {
  return b.epoch && b.kernel_hi >= b.kernel_lo &&
         b.kernel_hi - b.kernel_lo <= 100 && b.hf <= INT64_MAX;
}
static bool clipped(uint64_t remaining, uint64_t *v) {
  if (remaining > 600000000)
    return false;
  uint64_t margin =
      (remaining + 4999) / 5000 + 32; /* rate plus kernel quantization */
  if (remaining <= margin)
    return false;
  *v = remaining - margin;
  return true;
}
bool gm7_bridge_to_hf(struct gm7_bridge b, uint64_t end, uint64_t *out) {
  uint64_t d;
  if (!out || !gm7_bridge_valid(b) || end <= b.kernel_hi ||
      !clipped(end - b.kernel_hi, &d) || b.hf > UINT64_MAX - d)
    return false;
  *out = b.hf + d;
  return true;
}
bool gm7_bridge_to_kernel(struct gm7_bridge b, uint64_t end, uint64_t *out) {
  uint64_t d;
  if (!out || !gm7_bridge_valid(b) || end <= b.hf || !clipped(end - b.hf, &d) ||
      b.kernel_lo > UINT64_MAX - d)
    return false;
  *out = b.kernel_lo + d;
  return true;
}
bool gm7_bridge_past(struct gm7_bridge b, uint64_t at, uint64_t *out) {
  if (!out || !gm7_bridge_valid(b) || at > b.kernel_lo)
    return false;
  uint64_t d = b.kernel_hi - at;
  if (d > 600000000)
    return false;
  uint64_t elapsed = d + (d + 4999) / 5000 + 32;
  if (elapsed > b.hf)
    return false;
  *out = b.hf - elapsed;
  return true;
}
