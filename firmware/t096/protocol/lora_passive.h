/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_auth.h"
#include "time_zone.h"
#include "lora_passive_schedule.h"

/* A new passive wire, independent of RC8 wire2..5. Only FAST is executable.
 * All sizes include MAC16. Small packets authenticate the full cached context.
 * Hints never carry clock authority. No client-to-source packet exists. */
#define LP_VERSION 2u
#define LP_DISC_BYTES 28u
#define LP_META_BYTES 112u
#define LP_SYNC_BYTES 24u
#define LP_FOLLOW_BYTES 32u
enum lp_kind { LP_DISC=1, LP_META, LP_SYNC, LP_FOLLOW };
struct lp_scope {
 uint64_t emitter,boot,root,root_boot,parent,parent_boot;
 uint32_t epoch,stream,seed;
};
struct lp_packet {
 struct lp_scope scope;
 uint64_t utc_ns;
 uint32_t cursor,serial;
 struct r2_tz tz;
 uint16_t delay_10ms;
 uint8_t kind,schedule;
 bool has_pair,time_valid;
};
size_t lp_size(unsigned kind);
bool lp_scope_equal(const struct lp_scope *,const struct lp_scope *);
int lp_context(uint8_t out[16],const struct lp_scope *,unsigned schedule,const struct r2_group_config *);
int lp_encode(uint8_t *,size_t,const struct lp_packet *,const uint8_t context[16],const struct r2_group_config *);
int lp_decode(struct lp_packet *,const uint8_t *,size_t,const uint8_t context[16],const struct r2_group_config *);
/* Public SHA256 Fisher-Yates permutation; never derived from a PSK/mesh/boot. */
int lp_hop_order(uint8_t out[64],uint32_t seed);
unsigned lp_channel(const uint8_t order[64],uint32_t cursor);
