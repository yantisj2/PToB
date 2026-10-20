/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_auth.h"
#define RC8_WIRE_VERSION 2u
#define RC8_META_BYTES 68u
#define RC8_BYTES 80u
#define RC8_BODY_BYTES 64u
#define RC8_CONTEXT_BYTES 16u
#define RC8_UNKNOWN UINT32_MAX
#define RC8_MAX_PREP_NS UINT64_C(1000000000)
enum rc8_scale { RC8_RELATIVE=0, RC8_UTC=1 };
enum rc8_origin_kind { RC8_OWN_GM=1, RC8_SELECTED_BLE=2 };
struct rc8_scope {
 uint64_t emitter,emitter_boot,stream,root,root_boot;
 uint32_t source_epoch;
 uint8_t scale,kind;
};
/* Full metadata MAC is the exact authenticated context reference. Stream is
 * a nonzero monotonically increasing incarnation within each emitter boot. */
struct rc8_correction {
 uint64_t event_ns,created_ns,expires_ns;
 uint32_t sequence,map_generation,map_age_us,upstream_bound_ns;
};
struct rc8_packet {
 uint8_t context[RC8_CONTEXT_BYTES];
 struct rc8_correction correction;
 uint32_t sequence;
 bool corrected;
};
bool rc8_scope_equal(const struct rc8_scope *,const struct rc8_scope *);
int rc8_scope_validate(const struct rc8_scope *);
int rc8_metadata_encode(uint8_t out[RC8_META_BYTES],const struct rc8_scope *,const struct r2_group_config *);
int rc8_metadata_decode(struct rc8_scope *,uint8_t context[RC8_CONTEXT_BYTES],const uint8_t *,size_t,const struct r2_group_config *);
int rc8_context_id(uint8_t out[RC8_CONTEXT_BYTES],const struct rc8_scope *,const struct r2_group_config *);
int rc8_packet_validate(const struct rc8_packet *);
int rc8_encode(uint8_t out[RC8_BYTES],const struct rc8_packet *,const struct r2_group_config *);
/* Output cleared on failure. Symmetric group authentication is not individual
 * hardware attestation, current-source authority, or absolute replay freshness. */
int rc8_decode(struct rc8_packet *,const uint8_t *,size_t,const struct r2_group_config *);
