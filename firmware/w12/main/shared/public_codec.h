/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* protocol/v12_public_wire_candidate.json revision1. Manufacturer body uses
 * explicit LE integers; raw X/Y/r/s are big-endian byte strings. AD included.
 * This codec never grants authority or claims a captured physical event. */
#define V12_PUBLIC_MAX_AD 190u
#define V12_PUBLIC_ROOT_AD 158u
#define V12_PUBLIC_RELAY_AD 182u
#define V12_PUBLIC_GRANT_AD 190u
#define V12_PUBLIC_MAX_AGE_MS 500u
#define V12_PUBLIC_ROOT_TIMING 1u
#define V12_PUBLIC_RELAY_TIMING 2u
#define V12_PUBLIC_ROOT_GRANT 129u
#define V12_PUBLIC_RELAY_GRANT 130u
#define V12_PUBLIC_ORIGINAL 1u
#define V12_PUBLIC_SOURCE_QUALIFIED 2u
#define V12_PUBLIC_DEPTH_ONE 4u
#define V12_PUBLIC_PUBLICATION_ALLOWED 1u

struct v12_public_correction {
  uint32_t event,payload_generation;
  uint64_t UTC_ns;
  uint16_t source_age_ceil_ms,uncertainty_ceil_ns;
};
struct v12_public_timing {
  uint8_t kind,publisher,root,flags,correction_count;
  uint64_t root_boot,root_train;
  uint32_t source_epoch;
  uint64_t source_second,root_permit_id;
  uint32_t payload_generation;
  uint16_t source_age_ceil_ms,run_phase_125us;
  uint64_t emitter_boot,emitter_train,upstream_original_id;
  struct v12_public_correction corrections[2];
  uint8_t signature[64];
};
struct v12_public_grant {
  uint8_t kind,issuer,publisher,root,depth,flags,algorithm;
  uint64_t coordinator_boot;
  /* Root boot/train/source/config exist only in ROOT_GRANT. A relay inherits
   * them from the exact unchanged authenticated parent, never local copies. */
  uint64_t root_boot,root_train;
  uint32_t source_epoch,config_epoch;
  uint64_t emitter_boot,emitter_train,root_permit_id,relay_grant_id;
  uint64_t expires_UTC_ns;
  uint16_t duration_ceil_100us;
  uint8_t publisher_public_key_XY[64],signature[64];
};
struct v12_public_auth_view {
  const uint8_t *domain,*body,*signature;
  size_t domain_bytes,body_bytes;
};
int v12_public_timing_pack(uint8_t out[V12_PUBLIC_MAX_AD],size_t *length,
                           const struct v12_public_timing *record);
/* Produce ONLY the exact unsigned manufacturer body for digest/signing; no
 * AD, placeholder signature or transmit-ready packet is manufactured. */
int v12_public_timing_body(uint8_t out[116],size_t *length,const struct v12_public_timing *record);
int v12_public_timing_unpack(struct v12_public_timing *record,const uint8_t *ad,size_t length);
int v12_public_grant_pack(uint8_t out[V12_PUBLIC_MAX_AD],const struct v12_public_grant *record);
int v12_public_grant_body(uint8_t out[124],const struct v12_public_grant *record);
int v12_public_grant_unpack(struct v12_public_grant *record,const uint8_t *ad,size_t length);
/* Hash exact domain+body; caller MUST authenticate signature under trusted
 * coordinator/publisher key. This view performs structural/canonical checks,
 * and is not successful authentication or freshness/source admission. */
int v12_public_auth_view(struct v12_public_auth_view *out,const uint8_t *ad,size_t length);
const uint8_t *v12_public_domain(uint8_t kind,size_t *length);
int v12_public_duration(uint64_t created_UTC_ns,uint64_t expires_UTC_ns,uint16_t *units);
int v12_public_created_bounds(const struct v12_public_grant *grant,uint64_t *lower,uint64_t *upper);
/* Complete unchanged parent matching. Cryptographic verification and trusted
 * current UTC bounds are explicit separate prerequisites, never inferred. */
int v12_public_parent_match(const struct v12_public_grant *root,const struct v12_public_grant *relay);
int v12_public_timing_scope(const struct v12_public_timing *timing,const struct v12_public_grant *root,
                            const struct v12_public_grant *relay);
int v12_public_freshness(const struct v12_public_grant *grant,bool signature_verified,
                        bool trusted_current_UTC_bound,uint64_t UTC_lower,uint64_t UTC_upper);
