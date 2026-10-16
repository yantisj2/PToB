/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "group_auth.h"
#define R2_GROUP_JOIN_BYTES 164u
#define R2_GROUP_JOIN_TTL_US 250000u /* Time extraction only; authentication uses exchange lifetime. */
#define R2_GROUP_JOIN_LIFETIME_US 5000000u
enum r2_join_kind {
  R2_JOIN_REQUEST = 1,
  R2_JOIN_CHALLENGE,
  R2_JOIN_PROOF,
  R2_JOIN_ACCEPT,
  R2_JOIN_BUSY
};
/* Admission identity and name never index the four control participants.
 * Source fields are an original, bounded bootstrap permission, not GM
 * admission. */
struct r2_join_record {
  uint64_t sender, receiver, sender_boot, receiver_boot, client_nonce,
      server_nonce;
  uint64_t sequence, created_us, echo_us, root, root_boot, utc_us;
  uint32_t source_epoch, publication, remaining_us, source_bound_us;
  uint8_t kind, capabilities, source_flags, control;
  uint8_t demand; /* JOINv4: 0none/1request/2accepted/3refused/4pending; local only. */
  char name[R2_GROUP_NAME_MAX + 1];
};
int r2_group_join_encode(uint8_t out[R2_GROUP_JOIN_BYTES],
                         const struct r2_join_record *,
                         const struct r2_group_config *);
int r2_group_join_decode(struct r2_join_record *, const uint8_t *, size_t,
                         const struct r2_group_config *);
/* One bounded transient exchange. Each connection gets a fresh nonce from the
 * boot-random monotonic nonce allocator. Caller destroys this on disconnect. */
struct r2_join_exchange {
  uint64_t self, peer, boot, peer_boot, nonce, remote_nonce, until, request_at,
      challenge_at, proof_at, last_sequence;
  bool server, challenged, proved, accepted, control;
  uint8_t peer_capabilities;
  char name[R2_GROUP_NAME_MAX + 1];
  uint8_t capabilities;
};
int r2_group_join_begin(struct r2_join_exchange *, bool server, uint64_t self,
                        uint64_t peer, uint64_t boot, uint64_t nonce,
                        uint64_t now);
int r2_group_join_receive(struct r2_join_exchange *,
                          const struct r2_join_record *, uint64_t now);
/* After current-exchange ACCEPT authentication. Coarse bootstrap only; subtract
 * the entire measured causal RTT and independent source-map uncertainty. */
struct r2_join_time {
  uint64_t utc_us, until;
  uint32_t uncertainty_us;
};
bool r2_group_join_time(struct r2_join_time *, const struct r2_join_record *,
                        uint64_t proof_sent, uint64_t received,
                        uint32_t source_map_bound_us);
/* Coarse replay anchor only; does not renew the original source permission. */
uint64_t r2_group_anchor_deadline(uint64_t at,uint32_t uncertainty_us);
bool r2_group_coarse_upper(const struct r2_join_time *,uint64_t at,uint64_t now,uint64_t *upper_utc_us);
#define R2_GROUP_DISPLAY_CACHE 8u
struct r2_join_display {
  uint64_t identity, boot, seen;
  char name[25];
};
/* Optional last-seen display metadata; never consulted for MAC authorization,
 * join admission, source permission, identity allocation or packet recipients.
 */
void r2_group_join_remember(
    struct r2_join_display cache[R2_GROUP_DISPLAY_CACHE],
    const struct r2_join_exchange *, uint64_t now);
