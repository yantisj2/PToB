/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GM7_PENDING 4u
enum gm7_endpoint { GM7_ACTUAL_RF_ENDPOINTS, GM7_HOST_CAUSAL_BOUND_NOT_RF };
#define GM7_RTT_US UINT64_C(250000)
#define GM7_SESSION_US UINT64_C(360000000)
#define GM7_RENEW_MIN_US UINT64_C(225000000)
#define GM7_RENEW_SPAN_US UINT64_C(15000001)
#define GM7_OFFSET_MAX_US UINT64_C(500000)
/* Randomness is an explicit platform boundary. The production caller uses
 * sys_csrand_get / esp_fill_random, never identity, boot or deterministic PRNG.
 */
typedef int (*gm7_random_fn)(void *context, uint8_t *bytes, size_t size);
struct gm7_pending {
  struct gm7_request request;
  uint64_t allocated, tx, deadline;
  uint32_t quantization_us;
  uint8_t endpoint;
  bool used, sent;
};
struct gm7_requester {
  struct r2_group_config group;
  struct gm7_pending pending[GM7_PENDING];
  uint64_t identity, boot, sequence, last_now;
  uint8_t nonce_seed[32];
  bool failed, seeded;
};
/* After the one-shot nonce is consumed, keep only its bound clock/scope.
 * Request echo and duplicate identity fields remain in no authority cache. */
struct gm7_bound_anchor {
  uint64_t sampled_mono_us, utc_us, birth_us, founder, term, active,
      active_boot;
  uint32_t timing_epoch, source_gen, capture_gen, train_gen;
};
struct gm7_session {
  uint64_t group, peer, peer_boot, local_boot, floor, established, until,
      renew_at, drift_from;
  uint64_t endpoint_lower, endpoint_upper;
  uint32_t conservative_elapsed_us;
  uint8_t endpoint;
  uint64_t last_sequence, last_sent, source_until, last_diag[2], control_until;
  struct gm7_control control;
  int64_t offset_lo, offset_hi;
  uint32_t key_epoch;
  struct gm7_bound_anchor descriptor;
  uint16_t anchor_flags;
  bool valid, serving;
};
int gm7_requester_init(struct gm7_requester *, const struct r2_group_config *,
                       uint64_t identity, uint64_t boot, uint64_t now);
void gm7_requester_expire(struct gm7_requester *, uint64_t now);
int gm7_request_begin(struct gm7_requester *, struct gm7_packet *,
                      uint64_t peer, uint64_t boot, uint64_t now, gm7_random_fn,
                      void *);
/* Actual first RF start, not HCI submission. A repeated TX cannot renew it. */
/* B1: freeze outward-rounded HOST lower/deadline before transport exposure.
 * Later submission/actual-TX callbacks cannot move either endpoint. */
int gm7_request_host_bound(struct gm7_requester *, const struct gm7_request *,
                           uint32_t timestamp_quantization_us, uint64_t now);
int gm7_request_sent(struct gm7_requester *, const struct gm7_request *,
                     uint64_t actual_tx, uint64_t now);
int gm7_anchor_accept(struct gm7_requester *, struct gm7_session *,
                      const struct gm7_packet *, uint64_t actual_rx,
                      uint64_t now);
bool gm7_session_live(const struct gm7_session *, uint64_t now);
bool gm7_session_due(const struct gm7_session *, uint64_t now);
/* Both results are conservative local monotonic bounds on ORIGINAL send. */
int gm7_session_map(const struct gm7_session *, uint64_t remote, uint64_t now,
                    uint64_t *earliest, uint64_t *latest);
/* MAC/structural decode precedes this contextual admission. Duplicates never
 * update any sequence, timestamp, expiry or source permission. */
int gm7_session_control(struct gm7_session *, const struct gm7_packet *,
                        uint64_t actual_rx, uint64_t now,
                        uint64_t *original_earliest, uint64_t *until);
int gm7_session_diagnostic(struct gm7_session *, const struct gm7_packet *,
                           uint64_t actual_rx, uint64_t now, uint64_t *until);
void gm7_session_clear(struct gm7_session *);
#ifdef __cplusplus
}
#endif
