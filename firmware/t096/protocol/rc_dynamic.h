/* SPDX-License-Identifier: Apache-2.0
 * RC7/RQ7: bounded dynamic timing path, no controller or GM-control policy.
 * Canonical portable codec for the later exact W12 adoption.
 */
#pragma once
#include "../include/rc_boundary.h"
#include "group_auth.h"
#define RC_DYNAMIC_CODEC_REVISION 2u
#define RC_DYNAMIC_WIRE_VERSION 7u
#define RC_DYNAMIC_PROFILE 5u
#define RC_DYNAMIC_TIMING_BYTES 190u
#define RC_DYNAMIC_CONTROL_BYTES 212u
#define RC_DYNAMIC_CAP_ADV_BYTES 144u
/* Existing T096 owner spelling; the shared spec name above is canonical. */
#define RC_DYNAMIC_CAPABILITY_BYTES RC_DYNAMIC_CAP_ADV_BYTES
#define RC_DYNAMIC_MAX_DEPTH 2u
#define RC_DYNAMIC_PATH_NODES 3u
#define RC_DYNAMIC_RENEW_US UINT64_C(300000000)
#define RC_DYNAMIC_LEASE_US UINT32_C(360000000)
#define RC_DYNAMIC_SOURCE_NS UINT64_C(1650000000)
enum rc7_type { RC7_CAPABILITY = 1, RC7_REQUEST, RC7_RESPONSE };
struct rc7_node {
  uint64_t id, boot;
  uint32_t train, epoch;
  uint8_t slot, role;
};
struct rc7_path {
  uint32_t source_epoch;
  uint8_t depth;
  struct rc7_node node[3];
};
struct rc7_control {
  struct rc7_path path;
  uint8_t type, operation;
  bool ready, publishing;
  uint64_t sender, target, sender_boot, target_boot, challenge, nonce,
      next_challenge;
  uint32_t sequence, lease_us, publication_floor;
  uint64_t root_lo_ns, root_hi_ns, source_expires_ns;
};
struct rc7_packet {
  struct rc5_origin origin;
  uint8_t proof[16], depth, slot, count;
  uint64_t sender, boot;
  uint32_t train, publication, epoch;
  struct rc5_correction correction[2];
};
bool rc7_path_valid(const struct rc7_path *);
bool rc7_path_same(const struct rc7_path *, const struct rc7_path *);
bool rc7_path_empty(const struct rc7_path *);
int rc7_path_mac(uint8_t[16], const struct rc7_path *,
                 const struct r2_group_config *);
int rc7_origin_sign(uint8_t[16], const struct rc5_origin *,
                    const struct r2_group_config *);
int rc7_control_encode(uint8_t[212], const struct rc7_control *,
                       const struct r2_group_config *);
int rc7_control_decode(struct rc7_control *, const uint8_t *, size_t,
                       const struct r2_group_config *);
/* CAPABILITY-only lossless projection; canonical zeros and MAC are retained. */
int rc7_capability_encode(uint8_t[144], const struct rc7_control *,
                          const struct r2_group_config *);
int rc7_capability_decode(struct rc7_control *, const uint8_t *, size_t,
                          const struct r2_group_config *);
int rc7_encode(uint8_t[190], const struct rc7_packet *, const struct rc7_path *,
               const struct r2_group_config *);
int rc7_decode(struct rc7_packet *, const uint8_t *, size_t,
               const struct rc7_path *, const struct r2_group_config *);
/* Existing T096 boundary-map helpers use a 16MHz normalized capture counter
 * for receiver state. Wire timestamps are nanoseconds; another target must
 * supply its measured capture conversion or its own clock mapper. No global
 * roster, enrolled leaf list, or wire route-table index is introduced. */
struct rc7_receiver {
  struct rc7_path path;
  struct rc5_state clock;
  uint64_t self;
  bool transmitter;
};
bool rc7_bind(struct rc7_receiver *, const struct rc7_path *, uint64_t self,
              bool transmitter, uint8_t own_slot, const struct rc5_admission *);
bool rc7_receive(struct rc7_receiver *, const struct rc7_packet *, uint64_t rx,
                 uint16_t event, uint8_t channel, uint64_t now);
