/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define V12_CONTROL_BYTES 110u
#define V12_CONTROL_BODY_BYTES 102u
#define V12_CONTROL_ORIGINAL_TTL_US 250000u
#define V12_CONTROL_MAX_GMS 4u
#define V12_CONTROL_QUEUE_LIMIT 8u

/* Explicit LE encoding, no packed-struct ABI. This prepares the model's
 * authenticated control record. It does not bind a connection or grant roles.
 * Caller supplies the authenticated sender's key; no keys are embedded. */
struct v12_control_record {
  uint8_t kind, sender, target, root, flags;
  uint64_t boot, coordinator_boot, challenge, sequence, decision, root_boot;
  uint32_t source_epoch, config_epoch;
  uint64_t source_second, evidence_us, created_us, expires_us, permit_until_us;
};
int v12_control_encode(uint8_t out[V12_CONTROL_BYTES],
                       const struct v12_control_record *record,
                       const uint8_t key[16]);
int v12_control_decode(struct v12_control_record *record,
                       const uint8_t *wire, size_t length, const uint8_t key[16]);
