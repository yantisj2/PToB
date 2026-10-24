/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "precision_servo.h"
#include <stddef.h>
#define R2_PPS_FIT_POINTS 241u
/* A new HF/capture incarnation must have a new epoch. UNKNOWN and callback
 * provenance cannot train a precision estimator, even with ns-shaped values. */
enum r2_capture_kind {
  R2_CAPTURE_UNKNOWN,
  R2_CAPTURE_PPS,
  R2_CAPTURE_RADIO,
  R2_CAPTURE_NOW,
  R2_CAPTURE_LORA
};
struct r2_capture {
  uint64_t tick, epoch;
  enum r2_capture_kind kind;
};
struct r2_pps_precision {
  uint64_t epoch, last, rate_q16, ticks[R2_PPS_FIT_POINTS];
  unsigned count, head;
  int32_t drift_ppb, raw_rate_ppb, last_error_ns;
  uint32_t fit_rms_ns, prediction_rms_ns, prediction_max_ns, rejected;
  int32_t errors[32];
  unsigned error_count, error_head;
  bool seen, rate_valid, fit_valid;
};
struct r2_pps_precision_view {
  uint64_t epoch;
  int64_t age_us;
  int32_t drift_ppb, trim_ppb, raw_rate_ppb;
  uint32_t fit_rms_ns, prediction_rms_ns, samples, rejected;
  bool rate_valid, fit_valid;
};
void r2_pps_precision_init(struct r2_pps_precision *s);
bool r2_pps_precision_add(struct r2_pps_precision *s, struct r2_capture edge);
/* Separate raw oscillator drift from the applied inverse mapping correction. */
int32_t r2_pps_mapping_trim_ppb(const struct r2_pps_precision *s);
void r2_precision_format(char *out, size_t capacity, uint32_t rms_ns,
                         bool valid);

/* Relative source clock, not UTC. Retain the maps actually delivered at each
 * availability boundary; a late event must never use a later fit. */
#define R2_SOURCE_MAP_HISTORY 8u
struct r2_source_map {
  uint64_t from, until, anchor_ns, rate_q16, generation;
  int32_t phase_ppb; /* Own-PPS phase slew, ns/second; never a witness input. */
};
struct r2_source_clock {
  struct r2_source_map maps[R2_SOURCE_MAP_HISTORY];
  uint64_t epoch, last_edge, generation;
  unsigned count, head;
  int64_t utc_offset_ns;
  uint32_t utc_source_epoch;
  unsigned utc_association;
  bool utc_bound, utc_ever_bound;
};
/* Whole-second translation of the PPS-relative coordinate. Coarse UTC chooses
 * the nearest integer offset; exact half-second ties/overflow refuse a bind.
 * Still an unqualified label estimate, with no absolute UTC accuracy bound. A -1
 * bind result requires a new authenticated source epoch before retrying. */
void r2_source_clock_invalidate(struct r2_source_clock *s);
int r2_source_utc_bind(struct r2_source_clock *s, struct r2_capture event,
                       int64_t coarse_ns, uint32_t source_epoch, unsigned association);
bool r2_source_utc_read(const struct r2_source_clock *s, struct r2_capture event,
                        uint32_t source_epoch, int64_t *utc_ns);
void r2_source_clock_init(struct r2_source_clock *s);
bool r2_source_clock_publish(struct r2_source_clock *s,
                             const struct r2_pps_precision *pps,
                             struct r2_capture available);
bool r2_source_clock_read(const struct r2_source_clock *s,
                          struct r2_capture event, uint64_t *ns,
                          uint64_t *generation);

bool r2_source_clock_expiry(const struct r2_source_clock *s,struct r2_capture event,uint64_t *expires_ns);
