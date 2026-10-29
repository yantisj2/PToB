/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_promote.h"
#include <errno.h>
int gm7_promote_clock(struct r2_source_clock *out, const r2_servo_map_t *m,
                      int64_t offset, struct r2_capture now, uint32_t epoch,
                      unsigned association) {
  if (!out || !m || !m->valid || !epoch || !now.epoch ||
      now.kind != R2_CAPTURE_NOW || now.tick < m->delivery ||
      now.tick - m->delivery > 5 * R2_CAPTURE_HZ || m->phase == INT64_MIN ||
      m->rate > 50000 || m->rate < -50000 || now.tick > UINT64_MAX - 24000000 ||
      offset < 0)
    return -EINVAL;
  int64_t anchor, elapsed;
  if (!r2_servo_mapped(m, now.tick, &anchor) || anchor < 0 ||
      !r2_math_elapsed(now.tick - m->delivery, &elapsed))
    return -ERANGE;
  uint64_t magnitude = m->phase < 0 ? (uint64_t)-m->phase : (uint64_t)m->phase;
  uint64_t slewed = (uint64_t)elapsed / 1000000;
  uint64_t remaining = magnitude > slewed ? magnitude - slewed : 0;
  if (remaining > 8000000)
    return -ERANGE;
  const int64_t nominal = INT64_C(16000000) * 65536;
  /* N/(1+r/1e9) = N - N*r/(1e9+r). Product fits signed64;
   * rate quantum <1/N. No first-order reciprocal approximation. */
  int64_t rate = nominal - nominal * m->rate / (INT64_C(1000000000) + m->rate);
  struct r2_source_clock c = {.epoch = now.epoch,
                              .last_edge = now.tick,
                              .generation = 1,
                              .count = 1,
                              .head = 1,
                              .utc_offset_ns = offset,
                              .utc_source_epoch = epoch,
                              .utc_association = association,
                              .utc_bound = true,
                              .utc_ever_bound = true};
  uint64_t end = now.tick + 24000000, split = end;
  if (remaining && remaining < 1500)
    split = now.tick + remaining * 16000;
  int32_t phase = remaining ? (m->phase < 0 ? 1000 : -1000) : 0;
  c.maps[0] = (struct r2_source_map){now.tick,       split, (uint64_t)anchor,
                                     (uint64_t)rate, 1,     phase};
  if (split < end) {
    int64_t at_split;
    if (!r2_servo_mapped(m, split, &at_split) || at_split < anchor)
      return -ERANGE;
    c.maps[1] = (struct r2_source_map){split,          end, (uint64_t)at_split,
                                       (uint64_t)rate, 1,   0};
    c.count = c.head = 2;
  }
  *out = c;
  return 0;
}
