/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define R2_GROUP_VERSION 1u
#define R2_GROUP_NAME_MAX 24u
#define R2_GROUP_TAG_BYTES 16u
/* Group membership is symmetric trust, NOT individual hardware attestation. */
struct r2_group_config {
  uint8_t psk[32];
  uint64_t id;
  uint32_t epoch;
};
uint64_t r2_group_get(const uint8_t *p, unsigned n);
void r2_group_put(uint8_t *p, uint64_t v, unsigned n);
bool r2_group_valid(const struct r2_group_config *g);
bool r2_group_name(const char *name, size_t length);
int r2_group_mac(uint8_t out[16], const struct r2_group_config *g,
                 const char domain[8], const void *body, size_t size);
bool r2_group_verify(const uint8_t tag[16], const struct r2_group_config *g,
                     const char domain[8], const void *body, size_t size);
void r2_group_pair_key(uint8_t out[16], const struct r2_group_config *g,
                       uint64_t sender, uint64_t receiver);
