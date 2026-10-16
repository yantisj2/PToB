/* SPDX-License-Identifier: Apache-2.0
 * Portable V4 causal servo extracted from legacy include/ptob_tc_oscillator.h
 * at3f556f4. Wire assembly, legacy permission and hardware control excluded.
 * Input local/remote are extended16MHz capture ticks, never kernel uptime.
 * Caller must bind actual original/correction captures to authenticated scope.
 * Output residual RMS includes bias and tails; it is not absolute accuracy.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define R2_CAPTURE_HZ UINT64_C(16000000)
static inline void r2_math_inc(uint32_t *v) {
  if (*v != UINT32_MAX)
    ++*v;
}
static inline uint64_t r2_math_div(uint64_t n, uint32_t d) {
  uint64_t q = 0;
  uint32_t r = 0;
  for (int i = 63; i >= 0; --i) {
    r = (r << 1) | (uint32_t)((n >> i) & 1);
    if (r >= d) {
      r -= d;
      q |= UINT64_C(1) << i;
    }
  }
  return q;
}
static inline int64_t r2_math_sdiv(int64_t n, uint32_t d) {
  uint64_t m = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
  uint64_t q = r2_math_div(m, d);
  return n < 0 ? -(int64_t)q : (int64_t)q;
}
static inline bool r2_math_elapsed(uint64_t ticks, int64_t *ns) {
  if (ticks > (uint64_t)INT64_MAX / 125)
    return false;
  *ns = (int64_t)(ticks * 125 / 2);
  return true;
}
#define R2_SERVO_WINDOW 32u
#define R2_SERVO_SPAN (32 * R2_CAPTURE_HZ)
#define R2_SERVO_SLEW_NS_S 1000u
#define R2_SERVO_GATE_NS 200u
/* Experimental delivered software map only; strict200 remains a separate gate.
 * Physical-time buckets retain EVERY causal innovation, including tails. */
#define R2_SERVO_ATTEMPT_GATE_NS 1000u
#define R2_SERVO_ATTEMPT_BUCKETS 8u
#define R2_SERVO_ATTEMPT_SPAN (7 * R2_CAPTURE_HZ)
/* Four unique originals/s across the3s assembly deadline, plus margin. */
#define R2_SERVO_RX_PENDING 16u
#define R2_SERVO_RX_DONE 16u
#define R2_SERVO_PLL_DWELL (10 * R2_CAPTURE_HZ)
#define R2_SERVO_PLL_PHASE_ENTER_NS 125u
#define R2_SERVO_PLL_PHASE_EXIT_NS 250u
#define R2_SERVO_PLL_TREND_ENTER_PPB 10u
#define R2_SERVO_PLL_TREND_EXIT_PPB 20u
static inline bool r2_servo_predict(int64_t anchor, int64_t phase, int32_t rate,
                                    uint64_t ticks, int64_t *out) {
  int64_t ns = 0;
  if (rate > 50000 || rate < -50000 || phase == INT64_MIN)
    return false;
  if (anchor < 0 || !r2_math_elapsed(ticks, &ns) || ns > INT64_MAX - anchor)
    return false;
  uint64_t seconds = r2_math_div(ticks, 16000000);
  uint64_t remainder = ticks - seconds * 16000000;
  if (seconds > (uint64_t)INT64_MAX / 50000)
    return false;
  int64_t extra =
      (int64_t)seconds * rate +
      r2_math_sdiv((int64_t)(remainder * 125 / 2) * rate, 1000000000);
  int64_t slew = (int64_t)r2_math_div((uint64_t)ns, 2000);
  if (phase < 0) {
    if (slew > -(phase + 1) + 1)
      slew = -(phase + 1) + 1;
    slew = -slew;
  } else if (slew > phase)
    slew = phase;
  int64_t v = anchor + ns;
  if (extra > 0 && v > INT64_MAX - extra)
    return false;
  if (extra < 0 && v < -extra)
    return false;
  v += extra;
  if (slew > 0 && v > INT64_MAX - slew)
    return false;
  if (slew < 0 && v < -slew)
    return false;
  *out = v + slew;
  return true;
}
typedef struct {
  uint32_t session, sequence;
  uint64_t local, remote, available;
  bool peer_pps_below200;
} r2_servo_sample_t;
/* Portable division: no target floating point or unavailable 64-bit ABI helper.
 */
static inline uint64_t r2_servo_div64(uint64_t n, uint64_t d) {
  if (!d)
    return UINT64_MAX;
  uint64_t q = 0, r = 0;
  for (int i = 63; i >= 0; --i) {
    bool carry = (r >> 63) != 0;
    r = (r << 1) | ((n >> i) & 1);
    if (carry || r >= d) {
      r -= d;
      q |= UINT64_C(1) << i;
    }
  }
  return q;
}
static inline int64_t r2_servo_scaled_ratio(int64_t n, uint32_t scale,
                                            uint64_t d) {
  if (!d || n == INT64_MIN)
    return INT64_MAX;
  uint64_t m = n < 0 ? (uint64_t)-n : (uint64_t)n, q = r2_servo_div64(m, d),
           r = m - q * d, rem = 0, f = 0;
  if (q > (uint64_t)INT64_MAX / scale)
    return INT64_MAX;
  for (int i = 31; i >= 0; --i) {
    f <<= 1;
    bool carry = (rem >> 63) != 0;
    rem <<= 1;
    if (carry || rem >= d) {
      rem -= d;
      ++f;
    }
    if ((scale >> i) & 1) {
      if (rem >= d - r) {
        rem -= d - r;
        ++f;
      } else
        rem += r;
    }
  }
  uint64_t value = q * scale + f;
  if (value > INT64_MAX)
    return INT64_MAX;
  return n < 0 ? -(int64_t)value : (int64_t)value;
}
typedef struct {
  uint64_t delivery;
  int64_t anchor, phase;
  int32_t rate;
  bool valid;
} r2_servo_map_t;
static inline bool r2_servo_mapped(const r2_servo_map_t *m, uint64_t tick,
                                   int64_t *out) {
  if (!out || !m->valid || tick < m->delivery || m->phase == INT64_MIN)
    return false;
  int64_t v = 0, elapsed = 0;
  if (!r2_servo_predict(m->anchor, 0, m->rate, tick - m->delivery, &v) ||
      !r2_math_elapsed(tick - m->delivery, &elapsed))
    return false;
  int64_t slew = (int64_t)r2_math_div((uint64_t)elapsed, 1000000); /* 1 ppm */
  if (m->phase < 0) {
    if (slew > -m->phase)
      slew = -m->phase;
    slew = -slew;
  } else if (slew > m->phase)
    slew = m->phase;
  if ((slew > 0 && v > INT64_MAX - slew) || (slew < 0 && v < -slew))
    return false;
  *out = v + slew;
  return true;
}
typedef struct {
  uint64_t local, remote;
} r2_servo_point_t;
typedef struct {
  uint64_t ticks[32], squares[32];
  uint32_t predictions[32];
  int64_t signed_errors[32];
  uint64_t offsets[32];
  unsigned count, head;
  uint32_t streak;
  uint64_t last_local;
} r2_servo_output_window_t;
typedef enum {
  R2_SERVO_PLL_ACQUIRE,
  R2_SERVO_PLL_TRACKING,
  R2_SERVO_PLL_LOCKED
} r2_servo_pll_state_t;
typedef enum {
  R2_SERVO_PLL_RESET,
  R2_SERVO_PLL_NOT_ACTUATED,
  R2_SERVO_PLL_ESTIMATOR,
  R2_SERVO_PLL_OUTPUT_HISTORY,
  R2_SERVO_PLL_PHASE,
  R2_SERVO_PLL_TREND_UNAVAILABLE,
  R2_SERVO_PLL_TREND,
  R2_SERVO_PLL_DWELLING,
  R2_SERVO_PLL_CONVERGED
} r2_servo_pll_reason_t;
typedef struct {
  r2_servo_pll_state_t state;
  r2_servo_pll_reason_t reason;
  uint64_t last_update, dwell_started;
  uint32_t dwell_updates;
} r2_servo_pll_t;
typedef struct {
  r2_servo_point_t points[R2_SERVO_WINDOW];
  unsigned count;
  uint32_t session, last_sequence, observations, predictions, unavailable,
      resets, saturated, arithmetic_faults, streak;
  uint64_t last_local, last_remote, last_available, span;
  int64_t innovation, max_innovation, fit_intercept;
  int32_t rate;
  bool fit_valid, fault;
  r2_servo_map_t fit, output;
  uint64_t quality_ticks[32], quality_squares[32];
  unsigned quality_count, quality_head;
  uint32_t quality_predictions[32];
  r2_servo_output_window_t output_quality;
  r2_servo_pll_t pll;
  bool peer_pps_below200;
} r2_servo_clock_t;
static inline uint64_t r2_servo_quality_sum(const r2_servo_clock_t *c) {
  uint64_t total = 0;
  for (unsigned i = 0; i < c->quality_count; ++i) {
    uint64_t n = c->quality_squares[i];
    if (n == UINT64_MAX || total > UINT64_MAX - n)
      return UINT64_MAX;
    total += n;
  }
  return total;
}
static inline uint64_t r2_servo_quality_predictions(const r2_servo_clock_t *c) {
  uint64_t total = 0;
  for (unsigned i = 0; i < c->quality_count; ++i)
    total += c->quality_predictions[i];
  return total;
}
static inline uint32_t r2_servo_rms_floor(uint64_t sum, uint64_t n) {
  if (!n || sum == UINT64_MAX)
    return UINT32_MAX;
  uint64_t value = r2_servo_div64(sum, n), bit = UINT64_C(1) << 62, root = 0;
  while (bit > value)
    bit >>= 2;
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else
      root >>= 1;
    bit >>= 2;
  }
  return root > UINT32_MAX ? UINT32_MAX : (uint32_t)root;
}
static inline uint32_t r2_servo_quality_rms(const r2_servo_clock_t *c) {
  return r2_servo_rms_floor(r2_servo_quality_sum(c),
                            r2_servo_quality_predictions(c));
}
static inline bool r2_servo_quality(const r2_servo_clock_t *c) {
  if (!c->fit_valid || c->quality_count != 32 || c->streak < 4)
    return false;
  uint64_t first = c->quality_ticks[c->quality_head],
           n = r2_servo_quality_predictions(c);
  return n >= 32 && c->last_local >= first &&
         c->last_local - first >= 30 * R2_CAPTURE_HZ &&
         r2_servo_quality_sum(c) <
             (uint64_t)R2_SERVO_GATE_NS * R2_SERVO_GATE_NS * n;
}
static inline bool r2_servo_attempt_quality(const r2_servo_clock_t *c) {
  if (!c->fit_valid || c->quality_count < R2_SERVO_ATTEMPT_BUCKETS ||
      c->streak < 4)
    return false;
  uint64_t first =
               c->quality_ticks[c->quality_count == 32 ? c->quality_head : 0],
           n = r2_servo_quality_predictions(c);
  return n >= R2_SERVO_ATTEMPT_BUCKETS && c->last_local >= first &&
         c->last_local - first >= R2_SERVO_ATTEMPT_SPAN &&
         r2_servo_quality_sum(c) <
             (uint64_t)R2_SERVO_ATTEMPT_GATE_NS * R2_SERVO_ATTEMPT_GATE_NS * n;
}
static inline void r2_servo_bucket_add(uint64_t *ticks, uint64_t *squares,
                                       uint32_t *predictions, unsigned *count,
                                       unsigned *head, uint64_t tick,
                                       uint64_t square) {
  unsigned current = (*head + 31) % 32;
  if (*count && tick >= ticks[current] &&
      tick - ticks[current] < R2_CAPTURE_HZ) {
    /* A count or sum overflow remains unavailable until this bucket ages out.
     */
    if (predictions[current] == UINT32_MAX || square == UINT64_MAX ||
        squares[current] > UINT64_MAX - square)
      squares[current] = UINT64_MAX;
    else
      squares[current] += square;
    if (predictions[current] < UINT32_MAX)
      ++predictions[current];
    return;
  }
  predictions[*head] = 1;
  ticks[*head] = tick;
  squares[*head] = square;
  *head = (*head + 1) % 32;
  if (*count < 32)
    ++*count;
}
static inline void r2_servo_pll_clear(r2_servo_clock_t *c) {
  memset(&c->pll, 0, sizeof c->pll);
}
static inline void r2_servo_output_quality_clear(r2_servo_clock_t *c) {
  c->output_quality.count = c->output_quality.head = c->output_quality.streak =
      0;
  c->output_quality.last_local = 0;
  r2_servo_pll_clear(c);
}
static inline void r2_servo_quality_clear(r2_servo_clock_t *c) {
  c->quality_count = c->quality_head = 0;
  r2_servo_output_quality_clear(c);
}
static inline void r2_servo_quality_add(r2_servo_clock_t *c, uint64_t tick,
                                        uint64_t magnitude) {
  uint64_t square = magnitude > 1000000 ? UINT64_MAX : magnitude * magnitude;
  r2_servo_bucket_add(c->quality_ticks, c->quality_squares,
                      c->quality_predictions, &c->quality_count,
                      &c->quality_head, tick, square);
}
static inline unsigned r2_servo_output_quality_bins(const r2_servo_clock_t *c) {
  return c->output_quality.count;
}
static inline uint64_t
r2_servo_output_quality_predictions(const r2_servo_clock_t *c) {
  uint64_t total = 0;
  for (unsigned i = 0; i < c->output_quality.count; ++i)
    total += c->output_quality.predictions[i];
  return total;
}
static inline uint64_t r2_servo_output_quality_sum(const r2_servo_clock_t *c) {
  uint64_t total = 0;
  for (unsigned i = 0; i < c->output_quality.count; ++i) {
    uint64_t n = c->output_quality.squares[i];
    if (n == UINT64_MAX || total > UINT64_MAX - n)
      return UINT64_MAX;
    total += n;
  }
  return total;
}
static inline uint32_t r2_servo_output_quality_rms(const r2_servo_clock_t *c) {
  return r2_servo_rms_floor(r2_servo_output_quality_sum(c),
                            r2_servo_output_quality_predictions(c));
}
static inline bool r2_servo_output_quality(const r2_servo_clock_t *c) {
  const r2_servo_output_window_t *q = &c->output_quality;
  if (c->fault || !c->fit_valid || !c->output.valid || q->count != 32 ||
      q->streak < 4 || q->last_local != c->last_local)
    return false;
  uint64_t first = q->ticks[q->head],
           n = r2_servo_output_quality_predictions(c);
  return n >= 32 && q->last_local >= first &&
         q->last_local - first >= 30 * R2_CAPTURE_HZ &&
         r2_servo_output_quality_sum(c) <
             (uint64_t)R2_SERVO_GATE_NS * R2_SERVO_GATE_NS * n;
}
static inline uint64_t r2_servo_pll_phase_remaining(const r2_servo_clock_t *c,
                                                    uint64_t now) {
  if (!c->output.valid || now < c->output.delivery ||
      c->output.phase == INT64_MIN)
    return UINT64_MAX;
  int64_t elapsed = 0;
  if (!r2_math_elapsed(now - c->output.delivery, &elapsed))
    return UINT64_MAX;
  uint64_t phase = c->output.phase < 0 ? (uint64_t)-c->output.phase
                                       : (uint64_t)c->output.phase;
  uint64_t slewed = r2_math_div((uint64_t)elapsed, 1000000);
  return phase > slewed ? phase - slewed : 0;
}
static inline bool r2_servo_pll_trend_ppb(const r2_servo_clock_t *c,
                                          int32_t *out) {
  /* Equal physical-bucket weights; every signed delivered error contributes to
   * its bucket mean at its real mean ORIGINAL capture time. Neither an affine
   * fit rate nor equality of commanded rates is evidence of loop convergence.
   */
  const r2_servo_output_window_t *q = &c->output_quality;
  if (!out || q->count != 32 || r2_servo_output_quality_sum(c) == UINT64_MAX)
    return false;
  uint64_t base = q->ticks[q->head];
  if (q->last_local < base || q->last_local - base < 30 * R2_CAPTURE_HZ ||
      q->last_local - base > 96 * R2_CAPTURE_HZ)
    return false;
  int64_t sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (unsigned i = 0; i < 32; ++i) {
    uint32_t n = q->predictions[i];
    if (!n || q->signed_errors[i] == INT64_MIN || q->offsets[i] == UINT64_MAX ||
        q->ticks[i] < base || q->ticks[i] - base > 96 * R2_CAPTURE_HZ)
      return false;
    uint64_t offset = r2_servo_div64(q->offsets[i], n);
    if (offset >= R2_CAPTURE_HZ)
      return false;
    int64_t x = (int64_t)r2_servo_div64(q->ticks[i] - base + offset, 1600),
            y = r2_math_sdiv(q->signed_errors[i], n);
    if (y < -1000000 || y > 1000000)
      return false;
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
  }
  int64_t den = 32 * sxx - sx * sx, num = 32 * sxy - sx * sy;
  if (den <= 0)
    return false;
  int64_t trend = r2_servo_scaled_ratio(num, 10000, (uint64_t)den);
  if (trend < INT32_MIN || trend > INT32_MAX)
    return false;
  /* Round magnitude away from zero: integer truncation must not admit10.9ppb.
   */
  uint64_t magnitude = num < 0 ? (uint64_t)-num : (uint64_t)num;
  uint64_t remainder =
      magnitude - r2_servo_div64(magnitude, (uint64_t)den) * (uint64_t)den;
  if (remainder > UINT64_MAX / 10000)
    return false;
  uint64_t scaled = remainder * 10000;
  if (scaled - r2_servo_div64(scaled, (uint64_t)den) * (uint64_t)den) {
    if (num < 0)
      --trend;
    else
      ++trend;
  }
  if (trend < INT32_MIN || trend > INT32_MAX)
    return false;
  *out = (int32_t)trend;
  return true;
}
static inline uint64_t r2_servo_pll_dwell_ticks(const r2_servo_clock_t *c,
                                                uint64_t now) {
  uint64_t observed = now < c->pll.last_update ? now : c->pll.last_update;
  return c->pll.dwell_started && observed >= c->pll.dwell_started
             ? observed - c->pll.dwell_started
             : 0;
}
static inline bool r2_servo_pll_locked(const r2_servo_clock_t *c,
                                       uint64_t now) {
  int32_t trend = 0;
  return c->pll.state == R2_SERVO_PLL_LOCKED && !c->fault &&
         c->pll.last_update == c->output.delivery &&
         now >= c->pll.last_update && now >= c->last_local &&
         now - c->last_local < 3 * R2_CAPTURE_HZ && r2_servo_quality(c) &&
         r2_servo_output_quality(c) &&
         r2_servo_pll_phase_remaining(c, now) <= R2_SERVO_PLL_PHASE_EXIT_NS &&
         r2_servo_pll_trend_ppb(c, &trend) &&
         trend >= -(int32_t)R2_SERVO_PLL_TREND_EXIT_PPB &&
         trend <= (int32_t)R2_SERVO_PLL_TREND_EXIT_PPB;
}
static inline const char *r2_servo_pll_state(const r2_servo_clock_t *c,
                                             uint64_t now) {
  if (c->fault || !c->output.valid || !c->fit_valid || !c->pll.last_update ||
      now < c->pll.last_update || now < c->last_local ||
      now - c->last_local >= 3 * R2_CAPTURE_HZ)
    return "PLL_ACQUIRE";
  return r2_servo_pll_locked(c, now) ? "PLL_LOCKED" : "PLL_TRACKING";
}
static inline const char *r2_servo_pll_reason(const r2_servo_clock_t *c,
                                              uint64_t now) {
  if (c->fault)
    return "FAULT";
  if (now < c->last_local || now - c->last_local >= 3 * R2_CAPTURE_HZ)
    return "STALE_ORIGINAL";
  switch (c->pll.reason) {
  case R2_SERVO_PLL_NOT_ACTUATED:
    return "NOT_ACTUATED";
  case R2_SERVO_PLL_ESTIMATOR:
    return "ESTIMATOR_NOT_STRICT200";
  case R2_SERVO_PLL_OUTPUT_HISTORY:
    return "DELIVERED_HISTORY_NOT_STRICT200";
  case R2_SERVO_PLL_PHASE:
    return "PHASE_REMAINING";
  case R2_SERVO_PLL_TREND_UNAVAILABLE:
    return "DELIVERED_TREND_UNAVAILABLE";
  case R2_SERVO_PLL_TREND:
    return "DELIVERED_TREND";
  case R2_SERVO_PLL_DWELLING:
    return "CONVERGED_DWELL_PENDING";
  case R2_SERVO_PLL_CONVERGED:
    return "CONVERGED";
  default:
    return "RESET_OR_UNAVAILABLE";
  }
}
static inline void r2_servo_pll_update(r2_servo_clock_t *c, uint64_t now,
                                       bool actuated) {
  if (!actuated || c->fault || !c->output.valid || c->output.delivery != now ||
      c->last_available != now || now <= c->pll.last_update) {
    r2_servo_pll_clear(c);
    c->pll.reason = R2_SERVO_PLL_NOT_ACTUATED;
    return;
  }
  c->pll.last_update = now;
  c->pll.state = c->pll.state == R2_SERVO_PLL_LOCKED ? R2_SERVO_PLL_LOCKED
                                                     : R2_SERVO_PLL_TRACKING;
  r2_servo_pll_reason_t reason = R2_SERVO_PLL_CONVERGED;
  int32_t trend = 0;
  bool was_locked = c->pll.state == R2_SERVO_PLL_LOCKED;
  uint64_t phase_limit =
      was_locked ? R2_SERVO_PLL_PHASE_EXIT_NS : R2_SERVO_PLL_PHASE_ENTER_NS;
  int32_t trend_limit =
      was_locked ? R2_SERVO_PLL_TREND_EXIT_PPB : R2_SERVO_PLL_TREND_ENTER_PPB;
  if (!r2_servo_quality(c))
    reason = R2_SERVO_PLL_ESTIMATOR;
  else if (!r2_servo_output_quality(c))
    reason = R2_SERVO_PLL_OUTPUT_HISTORY;
  else if (r2_servo_pll_phase_remaining(c, now) > phase_limit)
    reason = R2_SERVO_PLL_PHASE;
  else if (!r2_servo_pll_trend_ppb(c, &trend))
    reason = R2_SERVO_PLL_TREND_UNAVAILABLE;
  else if (trend < -trend_limit || trend > trend_limit)
    reason = R2_SERVO_PLL_TREND;
  if (reason != R2_SERVO_PLL_CONVERGED) {
    c->pll.state = R2_SERVO_PLL_TRACKING;
    c->pll.dwell_started = 0;
    c->pll.dwell_updates = 0;
    c->pll.reason = reason;
    return;
  }
  if (!c->pll.dwell_started) {
    c->pll.dwell_started = now;
    c->pll.dwell_updates = 0;
  }
  r2_math_inc(&c->pll.dwell_updates);
  if (was_locked || (now >= c->pll.dwell_started &&
                     now - c->pll.dwell_started >= R2_SERVO_PLL_DWELL &&
                     c->pll.dwell_updates >= 4)) {
    c->pll.state = R2_SERVO_PLL_LOCKED;
    c->pll.reason = R2_SERVO_PLL_CONVERGED;
  } else
    c->pll.reason = R2_SERVO_PLL_DWELLING;
}
static inline const char *r2_servo_state(const r2_servo_clock_t *c,
                                         uint64_t now) {
  if (c->fault)
    return "FAULT";
  if (!c->last_local || now < c->last_local)
    return "ACQUIRE";
  uint64_t age = now - c->last_local;
  if (age >= 30 * R2_CAPTURE_HZ)
    return "STALE";
  if (age >= 3 * R2_CAPTURE_HZ)
    return "HOLDOVER";
  return r2_servo_quality(c)           ? "TRACK"
         : r2_servo_attempt_quality(c) ? "EXPERIMENT_TRACK"
                                       : "ACQUIRE";
}
static inline bool r2_servo_fit(r2_servo_clock_t *c) {
  c->fit_valid = false;
  unsigned n = c->count;
  if (n < 8)
    return false;
  const r2_servo_point_t *base = &c->points[0];
  c->span = c->points[n - 1].local - base->local;
  if (c->span < 10 * R2_CAPTURE_HZ || c->span > R2_SERVO_SPAN)
    return false;
  int64_t sx = 0, sy = 0, sxx = 0, sxy = 0, remote_ns = 0;
  if (!r2_math_elapsed(base->remote, &remote_ns))
    return false;
  for (unsigned i = 0; i < n; ++i) {
    int64_t a = 0, b = 0;
    uint64_t dt = c->points[i].local - base->local;
    if (dt > R2_SERVO_SPAN || c->points[i].remote < base->remote ||
        !r2_math_elapsed(c->points[i].remote - base->remote, &a) ||
        !r2_math_elapsed(dt, &b))
      return false;
    int64_t x = (int64_t)r2_math_div(dt, 1600), y = a - b;
    if (y < -4000000 || y > 4000000) {
      r2_math_inc(&c->saturated);
      return false;
    }
    sx += x;
    sy += y;
    sxx += x * x;
    sxy += x * y;
  }
  int64_t den = (int64_t)n * sxx - sx * sx, num = (int64_t)n * sxy - sx * sy;
  if (den <= 0)
    return false;
  int64_t rate = r2_servo_scaled_ratio(num, 10000, (uint64_t)den);
  if (rate < -50000 || rate > 50000) {
    r2_math_inc(&c->saturated);
    return false;
  }
  int64_t intercept = r2_math_sdiv(sy - r2_math_sdiv(rate * sx, 10000), n);
  if ((intercept < 0 && remote_ns < -intercept) ||
      (intercept > 0 && remote_ns > INT64_MAX - intercept))
    return false;
  c->rate = (int32_t)rate;
  c->fit_intercept = intercept;
  c->fit =
      (r2_servo_map_t){base->local, remote_ns + intercept, 0, c->rate, true};
  c->fit_valid = true;
  return true;
}
static inline bool r2_servo_observe_permitted(r2_servo_clock_t *c,
                                              const r2_servo_sample_t *s,
                                              bool servo,
                                              bool peer_permission) {
  int64_t remote_ns = 0;
  if (!s || !s->session || !s->sequence || !s->local || !s->remote ||
      s->available < s->local || s->available - s->local > 3 * R2_CAPTURE_HZ ||
      !r2_math_elapsed(s->remote, &remote_ns))
    return false;
  bool epoch = c->session != s->session;
  if (!epoch && (c->fault || s->sequence <= c->last_sequence ||
                 s->local <= c->last_local || s->remote <= c->last_remote ||
                 s->available <= c->last_available))
    return false;
  bool gap = !epoch && s->local - c->last_local > 5 * R2_CAPTURE_HZ;
  bool quality_gap = !epoch && s->local - c->last_local > 3 * R2_CAPTURE_HZ;
  if (epoch) {
    uint32_t resets = c->resets;
    memset(c, 0, sizeof *c);
    c->resets = resets;
    r2_math_inc(&c->resets);
    c->session = s->session;
  }
  bool causal = c->fit_valid && c->last_available <= s->local;
  int64_t predicted = 0;
  /* Score the actual delivered software map frozen BEFORE this correction.
   * The newly fitted map never retroactively improves this actuator error. */
  int64_t delivered = 0;
  bool output_causal = c->output.valid && c->output.delivery <= s->local &&
                       r2_servo_mapped(&c->output, s->local, &delivered);
  int64_t output_error = output_causal ? remote_ns - delivered : 0;
  if (!output_causal || quality_gap)
    r2_servo_output_quality_clear(c);
  bool innovation_causal =
      causal && r2_servo_mapped(&c->fit, s->local, &predicted);
  if (innovation_causal) {
    c->innovation = remote_ns - predicted;
    int64_t magnitude = c->innovation < 0 ? -c->innovation : c->innovation;
    if (magnitude > c->max_innovation)
      c->max_innovation = magnitude;
    r2_math_inc(&c->predictions);
    if (magnitude <= 1000 && !gap)
      r2_math_inc(&c->streak);
    else
      c->streak = 0;
    if (quality_gap)
      r2_servo_quality_clear(c);
    r2_servo_quality_add(c, s->local, (uint64_t)magnitude);
  } else {
    r2_math_inc(&c->unavailable);
    c->streak = 0;
    r2_servo_quality_clear(c);
  }
  if (output_causal && innovation_causal) {
    uint64_t magnitude =
        output_error < 0 ? (uint64_t)-output_error : (uint64_t)output_error;
    if (magnitude <= 1000 && !quality_gap)
      r2_math_inc(&c->output_quality.streak);
    else
      c->output_quality.streak = 0;
    r2_servo_output_window_t *q = &c->output_quality;
    unsigned current = (q->head + 31) % 32;
    bool continuing = q->count && s->local >= q->ticks[current] &&
                      s->local - q->ticks[current] < R2_CAPTURE_HZ;
    r2_servo_bucket_add(
        q->ticks, q->squares, q->predictions, &q->count, &q->head, s->local,
        magnitude > 1000000 ? UINT64_MAX : magnitude * magnitude);
    q->last_local = s->local;
    current = (q->head + 31) % 32;
    if (!continuing) {
      q->signed_errors[current] =
          magnitude > 1000000 ? INT64_MIN : output_error;
      q->offsets[current] = 0;
    } else if (magnitude > 1000000 || q->signed_errors[current] == INT64_MIN ||
               (output_error > 0 &&
                q->signed_errors[current] > INT64_MAX - output_error) ||
               (output_error < 0 &&
                q->signed_errors[current] < INT64_MIN - output_error) ||
               q->offsets[current] >
                   UINT64_MAX - (s->local - q->ticks[current])) {
      q->signed_errors[current] = INT64_MIN;
      q->offsets[current] = UINT64_MAX;
    } else {
      q->signed_errors[current] += output_error;
      q->offsets[current] += s->local - q->ticks[current];
    }
  } else
    r2_servo_output_quality_clear(c);
  if (gap) {
    c->count = 0;
    c->fit_valid = false;
    c->streak = 0;
    r2_servo_quality_clear(c);
    r2_math_inc(&c->resets);
  }
  c->last_sequence = s->sequence;
  c->last_local = s->local;
  c->last_remote = s->remote;
  c->last_available = s->available;
  r2_math_inc(&c->observations);
  /* Thin by elapsed physical time, never by residual. Keep every observation's
   * causal outcome in counters/logs even if not inserted into the fit. */
  while (c->count && s->local - c->points[0].local > R2_SERVO_SPAN) {
    memmove(c->points, c->points + 1, (--c->count) * sizeof c->points[0]);
  }
  if (!c->count ||
      s->local - c->points[c->count - 1].local >= R2_CAPTURE_HZ / 2) {
    if (c->count == R2_SERVO_WINDOW) {
      memmove(c->points, c->points + 1, (--c->count) * sizeof c->points[0]);
    }
    c->points[c->count++] = (r2_servo_point_t){s->local, s->remote};
  }
  c->peer_pps_below200 = s->peer_pps_below200;
  if (!r2_servo_fit(c)) {
    c->streak = 0;
    r2_servo_quality_clear(c);
    return true;
  }
  bool actuated = false;
  if (servo && peer_permission && r2_servo_attempt_quality(c)) {
    int64_t target = 0, current = 0;
    if (!r2_servo_mapped(&c->fit, s->available, &target)) {
      r2_math_inc(&c->arithmetic_faults);
      r2_servo_pll_clear(c);
      return true;
    }
    current = target;
    if (c->output.valid &&
        !r2_servo_mapped(&c->output, s->available, &current)) {
      r2_math_inc(&c->arithmetic_faults);
      r2_servo_pll_clear(c);
      return true;
    }
    c->output = (r2_servo_map_t){s->available, current, target - current,
                                 c->rate, true};
    actuated = true;
  }
  r2_servo_pll_update(c, s->available, actuated);
  return true;
}
