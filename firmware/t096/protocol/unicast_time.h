/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "group_auth.h"
#define UC1_STATE_BYTES 144u
#define UC1_SAMPLE_BYTES 144u
#define UC1_CONTROL_BYTES 72u
#define UC1_PROBE_BYTES 64u
#define UC1_LEASE_US UINT32_C(360000000)
#define UC1_UNKNOWN UINT32_MAX
#define UC1_STATE 1u
#define UC1_CONTROL 2u
#define UC1_PROBE 3u
#define UC1_SAMPLE 4u
#define UC1_ON 1u
#define UC1_RENEW 2u
#define UC1_OFF 3u
#define UC1_SOURCE_VALID 1u
#define UC1_ACTIVE 2u
#define UC1_HELD 4u
#define UC1_CLIENTS 4u
#define UC1_POOL_HZ 16u
#define UC1_ALLOW_DOWNGRADE 1u
#define UC1_SUPPORT_8HZ 2u
enum uc1_reason {
  UC1_OK,
  UC1_CAPACITY,
  UC1_SOURCE,
  UC1_MTU,
  UC1_EXPIRED,
  UC1_RATE
};
struct uc1_session {
  uint64_t boot, challenge, nonce, stream, until, last_probe_at, rate_next_at;
  uint32_t control_seq, probe_seq;
  uint8_t hz, reason, ceiling, options;
  uint8_t last_control[UC1_CONTROL_BYTES];
};
void uc1_key(struct r2_group_config *, const struct r2_group_config *,
             uint64_t);
bool uc1_verify(const uint8_t *, size_t, unsigned,
                const struct r2_group_config *);
int uc1_sign(uint8_t *, size_t, unsigned, const struct r2_group_config *);
/* Return 1 for a fresh command, 0 for exact duplicate, negative for refusal. */
int uc1_control(struct uc1_session *, const uint8_t *, size_t,
                const struct r2_group_config *, uint64_t, uint64_t, uint16_t,
                uint32_t);
bool uc1_probe(struct uc1_session *, const uint8_t *, size_t,
               const struct r2_group_config *, uint64_t);
bool uc1_live(const struct uc1_session *, uint64_t);
/* Admission only: halve the largest reducible grant, newest first on ties.
 * Never increase any grant. The caller supplies connection admission order. */
bool uc1_pool(struct uc1_session *const[UC1_CLIENTS], uint64_t);
bool uc1_credit(uint64_t *, uint64_t, unsigned, unsigned);
