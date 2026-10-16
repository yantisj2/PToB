/* SPDX-License-Identifier: Apache-2.0
 * Native relay profile2. Distinct from frozen simulation-only R1. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define R2_RELAY_AD_BYTES 190u
#define R2_RELAY_RECIPIENTS 4u
#define R2_RELAY_BOUND_UNKNOWN UINT32_MAX
#define R2_RELAY_ORIGIN_BYTES 46u
#define R2_RELAY_HEADER_BYTES 6u
#define R2_RELAY_ROUTE_BYTES 32u
#define R2_RELAY_CORRECTION_BYTES 19u
struct r2_relay_origin {
  uint8_t root, relay;
  uint64_t boot;
  uint32_t source_epoch;
  uint64_t created_ns, expires_ns, frame_ns;
  int64_t utc_offset_ns;
  /* GRv3 signed scope. Zero horizon means fresh-only, never implicit permission. */
  uint32_t hold_horizon_us,hold_budget_ns;
};
struct r2_relay_route {
  uint8_t transmitter, parent, depth, slot;
  uint64_t boot;
  uint32_t train, publication, capture_epoch, map_generation, bound_ns;
};
struct r2_relay_correction {
  uint32_t publication;
  uint16_t event;
  uint8_t channel;
  uint64_t ticks;
  uint32_t generation;
};
struct r2_relay_packet {
  struct r2_relay_origin origin;
  struct r2_relay_route route;
  struct r2_relay_correction correction[2];
  uint8_t count;
};
/* Root signs an immutable envelope, relay copies its tags without root keys.
 * Sender signs full header/envelope/root tags/route/corrections. */
int r2_relay_origin_sign(uint8_t tags[32], const struct r2_relay_origin *origin,
                   const uint8_t root_keys[4][16]);
int r2_relay_encode(uint8_t ad[R2_RELAY_AD_BYTES], const struct r2_relay_packet *packet,
               const uint8_t origin_tags[32], const uint8_t sender_keys[4][16]);
int r2_relay_decode(struct r2_relay_packet *packet, const uint8_t *ad, size_t size,
               unsigned recipient, const uint8_t root_key[16],
               const uint8_t sender_key[16]);
uint32_t r2_relay_bound_add(uint32_t upstream, uint32_t hop);

/* Internal: only after versioned origin and transmitter MAC checks. */
int r2_relay_parse_authenticated(struct r2_relay_packet *,const uint8_t *ad);

/* Canonical packet validation including explicitly signed GRv3 horizon. */
int r2_relay_validate(const struct r2_relay_packet *);
/* V14 rev2 signed serving permission; source freshness remains independent. */
int r2_relay_validate3(const struct r2_relay_packet *);
