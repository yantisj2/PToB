/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "authority_codec.h"
#include "group_auth.h"
#define R2_GROUP_CONTROL_MAX 223u
/* Four ID dictionary entries describe this sender's bounded CONTROL slots;
 * they are not a recipient roster, stable identity or group membership limit.
 */
size_t r2_group_control_size(unsigned kind);
size_t r2_control_wire_size(const uint8_t *wire);
int r2_group_control_encode(uint8_t out[R2_GROUP_CONTROL_MAX],
                            const struct r2_auth_record *r,
                            const struct r2_group_config *g,
                            const uint64_t identities[4], const char *name,
                            uint8_t capabilities);
int r2_group_control_decode(struct r2_auth_record *r, const uint8_t *wire,
                            size_t size, const struct r2_group_config *g,
                            const uint64_t identities[4],
                            uint64_t expected_sender,
                            char name[R2_GROUP_NAME_MAX + 1],
                            uint8_t *capabilities);

/* V14 local durability/fencing semantics require this explicit envelope.
 * Inner Config5 record layout is preserved; old group peers reject type15. */
int r2_group_control_encode14(uint8_t *,const struct r2_auth_record *,const struct r2_group_config *,const uint64_t [4],const char *,uint8_t);
int r2_group_control_decode14(struct r2_auth_record *,const uint8_t *,size_t,const struct r2_group_config *,const uint64_t [4],uint64_t,char [25],uint8_t *);
/* Internal discovery/yield evidence from the existing authenticated dictionary.
 * An unknown partner remains absent from r2_auth_record's local control slots.
 * This optional value never grants membership, source permission or HOT. */
struct r2_group_pair_hint {uint64_t root,standby,root_boot,standby_boot,until;uint32_t epoch;};
int r2_group_control_decode14_hint(struct r2_auth_record *,const uint8_t *,size_t,const struct r2_group_config *,const uint64_t [4],uint64_t,char [25],uint8_t *,struct r2_group_pair_hint *);
