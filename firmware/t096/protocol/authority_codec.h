/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define R2_AUTH_BYTES 154u
#define R2_AUTH_BODY_BYTES 146u
#define R2_AUTH_BASE_BYTES 124u
static inline size_t r2_auth_size(unsigned kind){return kind==10?R2_AUTH_BYTES:R2_AUTH_BASE_BYTES;}
#define R2_AUTH_ORIGINAL_TTL_US 250000u
#define R2_AUTH_MAX_GMS 4u
#define R2_AUTH_QUEUE_LIMIT 8u

/* Explicit LE encoding, no packed-struct ABI. This prepares the model's
 * authenticated control record. It does not bind a connection or grant roles.
 * Caller supplies the authenticated sender's key; no keys are embedded. */
struct r2_auth_record {
  uint8_t kind, sender, target, root, flags;
  uint8_t pair_root,pair_standby;
  uint64_t pair_root_boot,pair_standby_boot,pair_until_us;
  uint32_t pair_epoch;
  uint64_t boot, coordinator_boot, challenge, sequence, decision, root_boot;
  uint32_t source_epoch, config_epoch;
  uint8_t priority1,clock_class,accuracy,priority2;
  uint16_t variance;
  uint32_t boot_age_ms,source_age_ms;
  uint64_t source_second, evidence_us, created_us, expires_us, permit_until_us;
};
int r2_auth_encode(uint8_t out[R2_AUTH_BYTES],
                       const struct r2_auth_record *record,
                       const uint8_t key[16]);
int r2_auth_decode(struct r2_auth_record *record,
                       const uint8_t *wire, size_t length, const uint8_t key[16]);

#define R2_AUTH_CONFIG 5u
#define R2_AUTH_HELLO 10u
#define R2_AUTH_SERVICE 8u
#define R2_AUTH_ACK 9u
#define R2_AUTH_PREPARE 11u
#define R2_AUTH_READY 12u
#define R2_AUTH_COMMIT 13u

/* Internal parser: caller MUST verify a versioned MAC over the complete body. */
int r2_auth_parse_authenticated(struct r2_auth_record *,const uint8_t *wire);

/* Body serializer; caller must authenticate the complete outer record. */
int r2_auth_encode_body(uint8_t out[R2_AUTH_BYTES],const struct r2_auth_record *);
