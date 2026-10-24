/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_auth.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GM7_REVISION 2u
#define GM7_CONTROL_BYTES 174u
#define GM7_REQUEST_BYTES 96u
#define GM7_ANCHOR_BYTES 186u
#define GM7_ROLE_BYTES 168u
#define GM7_GNSS_BYTES 156u
#define GM7_MAX_BYTES GM7_ANCHOR_BYTES
#define GM7_MAX_AD_BYTES (GM7_MAX_BYTES + 4u)
#define GM7_UTC_MIN UINT64_C(946684800000000)
#define GM7_UTC_MAX UINT64_C(4102444800000000)
enum gm7_kind {
  GM7_ANNOUNCE = 1,
  GM7_STATUS,
  GM7_WITHDRAW,
  GM7_REQUEST,
  GM7_ANCHOR,
  GM7_ROLE,
  GM7_GNSS
};
enum gm7_control_flag {
  GM7_ELIGIBLE = 1,
  GM7_SHADOW = 2,
  GM7_HOT = 4,
  GM7_YIELD = 8,
  GM7_CAPABLE = 16
};
enum gm7_anchor_flag {
  GM7_A_INDEPENDENT = 1,
  GM7_A_SERVING = 2,
  GM7_A_UTC = 4,
  GM7_A_UTC_BOUND = 8,
  GM7_A_RELAY = 16
};
enum gm7_wire_role {
  GM7_W_STANDBY,
  GM7_W_ACTIVE,
  GM7_W_PREPARING,
  GM7_W_HOT,
  GM7_W_RC
};
enum gm7_wire_blocker {
  GM7_W_NO_GNSS = 1,
  GM7_W_NO_PPS = 2,
  GM7_W_DWELL = 4,
  GM7_W_SHADOW_UNREADY = 8,
  GM7_W_WRONG_NOMINEE = 16,
  GM7_W_WRONG_BOOT_TERM = 32,
  GM7_W_STALE_EVIDENCE = 64,
  GM7_W_WITHDRAWN = 128,
  GM7_W_MERGE_LOST = 256,
  GM7_W_UTC_UNAVAILABLE = 512,
  GM7_W_ALIGNMENT_UNTRUSTED = 1024,
  GM7_W_BOOTSTRAP_PENDING = 2048,
  GM7_W_RESOURCE_LIMIT = 4096
};
struct gm7_control {
  uint64_t sender;
  uint64_t boot;
  uint64_t sequence;
  uint64_t sent_utc_us;
  uint32_t max_age_us;
  uint32_t source_valid_us;
  uint32_t source_gen;
  uint32_t capture_gen;
  uint32_t train_gen;
  uint8_t priority1;
  uint8_t clock_class;
  uint8_t accuracy;
  uint8_t priority2;
  uint16_t variance;
  uint64_t birth_us;
  uint64_t founder;
  uint64_t term;
  uint64_t active;
  uint64_t active_boot;
  uint64_t hot;
  uint64_t hot_boot;
  uint64_t yield_utc_us;
  uint32_t shadow_gen;
  uint64_t sent_mono_us;
};
struct gm7_request {
  uint64_t requester;
  uint64_t requester_boot;
  uint64_t target;
  uint64_t target_boot;
  uint64_t nonce_hi;
  uint64_t nonce_lo;
  uint64_t request_sequence;
};
struct gm7_anchor {
  uint64_t requester;
  uint64_t requester_boot;
  uint64_t nonce_hi;
  uint64_t nonce_lo;
  uint64_t responder;
  uint64_t responder_boot;
  uint64_t response_sequence;
  uint64_t sampled_mono_us;
  uint64_t utc_us;
  uint64_t birth_us;
  uint64_t founder;
  uint64_t term;
  uint64_t active;
  uint64_t active_boot;
  uint32_t timing_epoch;
  uint32_t source_gen;
  uint32_t capture_gen;
  uint32_t train_gen;
  uint64_t control_sequence;
  uint32_t valid_us;
  uint32_t utc_error_us;
  uint8_t depth;
  uint8_t reserved;
};
struct gm7_role {
  uint64_t sender;
  uint64_t boot;
  uint64_t sequence;
  uint64_t sampled_mono_us;
  uint64_t birth_us;
  uint64_t founder;
  uint64_t term;
  uint64_t active;
  uint64_t active_boot;
  uint32_t timing_epoch;
  uint32_t valid_us;
  uint64_t hot;
  uint64_t hot_boot;
  uint32_t source_gen;
  uint32_t capture_gen;
  uint32_t train_gen;
  uint32_t shadow_gen;
  uint8_t role;
  uint8_t known;
  uint16_t blockers;
  uint32_t hot_duration_ms;
  uint32_t active_duration_ms;
  uint32_t pps_duration_ms;
};
struct gm7_gnss {
  uint64_t sender;
  uint64_t boot;
  uint64_t sequence;
  uint64_t sampled_mono_us;
  uint64_t birth_us;
  uint64_t founder;
  uint64_t term;
  uint64_t active;
  uint64_t active_boot;
  uint32_t timing_epoch;
  uint32_t valid_us;
  uint32_t source_gen;
  uint32_t capture_gen;
  uint32_t train_gen;
  uint16_t known;
  uint16_t values;
  uint8_t sats;
  uint8_t clock_class;
  uint8_t accuracy;
  uint8_t priority1;
  uint8_t priority2;
  uint8_t reserved;
  uint16_t variance;
  uint16_t hdop_centi;
  uint16_t rmc_age_ms;
  uint16_t pps_age_ms;
  uint16_t gga_age_ms;
  uint32_t utc_error_us;
};
struct gm7_packet {
  uint64_t group;
  uint32_t key_epoch;
  uint16_t flags;
  uint8_t kind;
  union {
    struct gm7_control control;
    struct gm7_request request;
    struct gm7_anchor anchor;
    struct gm7_role role;
    struct gm7_gnss gnss;
  } body;
};
size_t gm7_wire_size(uint8_t kind);
bool gm7_utc_valid(uint64_t utc);
int gm7_validate(const struct gm7_packet *);
/* Exact implementation2 domain, big endian, no packed-struct wire access. */
int gm7_encode(uint8_t *, size_t, const struct gm7_packet *,
               const struct r2_group_config *);
int gm7_decode(struct gm7_packet *, const uint8_t *, size_t,
               const struct r2_group_config *);
int gm7_ad_encode(uint8_t *, size_t, const struct gm7_packet *,
                  const struct r2_group_config *);
int gm7_ad_decode(struct gm7_packet *, const uint8_t *, size_t,
                  const struct r2_group_config *);
#ifdef __cplusplus
}
#endif
