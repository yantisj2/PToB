/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "../protocol/precision_codec.h"
#include "precision.h"
#define R2_RADIO_PENDING 16u
#define R2_RADIO_MAX_AGE_TICKS (R2_CAPTURE_HZ / 4)
struct r2_radio_scope {
  uint64_t boot, until_us;
  uint64_t root_identity; /* Stable identity across public/control roster slots. */
  uint32_t source_epoch, publication_epoch;
  uint8_t root;
  bool gm, follower;
};
struct r2_radio_original {
  uint64_t local, created_ns, expires_ns;
  uint32_t publication;
  uint16_t event;
  uint8_t channel;
  bool used;
};
struct r2_radio_correction {
  struct r2_precise_correction value;
  uint64_t arrived;
  bool used;
};
struct r2_radio_precision {
  struct r2_radio_scope scope;
  uint64_t local_epoch, last_available, last_accepted, last_tx_ns;
#ifdef R2_LORA
  uint64_t origin_created_ns,origin_expires_ns;
#endif
  uint32_t train, remote_capture, sequence, accepted, rejected, duplicates,
      overflows;
  uint32_t tx_reject[4]; /* scope/capture, envelope, sequence, interval */
  struct r2_radio_original original[R2_RADIO_PENDING];
  struct r2_radio_correction correction[R2_RADIO_PENDING];
  struct r2_precise_correction sent[2];
  unsigned sent_count;
  r2_servo_clock_t servo;
  int32_t beat_errors[32];
  unsigned beat_count, beat_head;
  uint32_t beat_rms_ns;
  uint16_t last_tx_event;
  bool tx_seen;
};
struct r2_radio_precision_view {
  uint32_t rms_ns, samples, accepted, rejected, duplicates, overflows;
  uint32_t captured, invalid, train, publications;
  uint32_t tx_reject[4];
  uint32_t pt_cycles,pt_wall_max_us,pt_capture_age_max_us;
  bool authorized;
  int32_t trim_ppb, drift_ppb;
  bool drift_valid;
  bool rms_valid, map_valid, locked, fit_valid, correction_age_valid;
  uint32_t correction_age_ms;
};
/* Only after authenticated same root/source/parent boot and capture domain.
 * Retires all transport associations and availability, retaining causal fit. */
bool r2_radio_precision_rebind_train(struct r2_radio_precision *s,
                                     uint32_t train,uint32_t capture_epoch);
void r2_radio_precision_scope(struct r2_radio_precision *s,
                              struct r2_radio_scope scope,
                              uint64_t local_epoch);
/* Exact hardware RX identity + authenticated decoded PDU. expected_utc_ns is
 * coarse SERVICE time solely for a replay-age fence, never a servo sample. */
bool r2_radio_precision_rx(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture rx, uint16_t event,
                           uint8_t channel, struct r2_capture available,
                           uint64_t now_us, int64_t expected_utc_ns);
/* Native2 caller has authenticated the immutable root origin. Its original
 * common source expiry (≤1.5s lifetime) bounds permission; the250ms capture
 * and correction-reference age remains unchanged. This is not P1's fresh
 * publication-header age policy and cannot be used by the P1 owner. */
bool r2_radio_precision_rx_immutable_origin(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture rx,uint16_t event,uint8_t channel,
                           struct r2_capture available,uint64_t now_us,
                           int64_t expected_utc_ns);
bool r2_radio_precision_rx_serving_origin(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture rx,uint16_t event,uint8_t channel,
                           struct r2_capture available,uint64_t now_us,
                           int64_t expected_utc_ns);
bool r2_radio_precision_step(struct r2_radio_precision *s,
                             struct r2_capture available, uint64_t now_us);
bool r2_radio_precision_tx(struct r2_radio_precision *s,
                           const struct r2_precision_packet *p,
                           struct r2_capture tx, uint16_t event,
                           uint8_t channel, uint64_t relative_ns,
                           uint64_t generation, uint64_t expires_ns,
                           uint64_t now_us);
void r2_radio_precision_view(const struct r2_radio_precision *s,
                             struct r2_capture now, uint64_t now_us,
                             struct r2_radio_precision_view *out);
bool r2_radio_precision_read(const struct r2_radio_precision *s,
                             struct r2_capture now, uint64_t now_us,
                             int64_t *relative_ns);

/* GRv3 owner only, after original-reference signature/horizon/budget checks. */
bool r2_radio_precision_rx_held_origin(struct r2_radio_precision *,const struct r2_precision_packet *,
 struct r2_capture,uint16_t,uint8_t,struct r2_capture,uint64_t,int64_t,uint32_t signed_horizon_us);

/* Frozen delivered follower map with the declared empirical phase correction.
 * The raw fitter and matched RF samples remain unchanged. */
bool r2_radio_precision_map(const struct r2_radio_precision *,r2_servo_map_t *);
