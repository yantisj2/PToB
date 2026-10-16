/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "group_auth.h"
#include "precision_codec.h"
#include "relay_codec.h"
int r2_group_slot(const uint64_t ids[4], uint64_t id);
int r2_group_precision_encode(uint8_t ad[158],
                              const struct r2_precision_packet *p,
                              const struct r2_group_config *g,
                              const uint64_t ids[4]);
int r2_group_precision_decode(struct r2_precision_packet *p, const uint8_t *ad,
                              size_t size, const struct r2_group_config *g,
                              const uint64_t ids[4]);
int r2_group_origin_sign(uint8_t proof[32], const struct r2_relay_origin *o,
                         const struct r2_group_config *g,
                         const uint64_t ids[4]);
int r2_group_relay_encode(uint8_t ad[190], const struct r2_relay_packet *p,
                          const uint8_t proof[32],
                          const struct r2_group_config *g,
                          const uint64_t ids[4]);
int r2_group_relay_decode(struct r2_relay_packet *p, const uint8_t *ad,
                          size_t size, const struct r2_group_config *g,
                          const uint64_t ids[4]);

/* Explicit GRv2 only: no fallback to named-relay GRv1 semantics. ids is a
 * private timing-path context and does not allocate GM control membership. */
int r2_group_origin_sign2(uint8_t proof[32],const struct r2_relay_origin *,
 const struct r2_group_config *,const uint64_t ids[4]);
int r2_group_relay_encode2(uint8_t ad[190],const struct r2_relay_packet *,
 const uint8_t proof[32],const struct r2_group_config *,const uint64_t ids[4]);
int r2_group_relay_decode2(struct r2_relay_packet *,const uint8_t *,size_t,
 const struct r2_group_config *,const uint64_t ids[4]);

/* V14 GRv3: incompatible domain/version, signed held horizon and budget.
 * Legacy GRv1/v2 remain available only to their explicit callers/tests. */
int r2_group_origin_sign3(uint8_t proof[32],const struct r2_relay_origin *,
 const struct r2_group_config *,const uint64_t ids[4]);
int r2_group_relay_encode3(uint8_t ad[190],const struct r2_relay_packet *,
 const uint8_t proof[32],const struct r2_group_config *,const uint64_t ids[4]);
int r2_group_relay_decode3(struct r2_relay_packet *,const uint8_t *,size_t,
 const struct r2_group_config *,const uint64_t ids[4]);
