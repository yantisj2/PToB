/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "group_auth.h"
#define R2_ROLE_AD_BYTES 116u
#define R2_ROLE_AD_BODY 100u
#define R2_ROLE_SERVING 1u
#define R2_ROLE_HOT 2u
#define R2_ROLE_MAX_VALID_US 2000000u
/* Public diagnostic announcement only. No election, JOIN, source permission,
 * control membership, calibrated timing or qualified precision authority. */
struct r2_role_record {
 uint64_t sender,sender_boot,root,root_boot,hot,hot_boot,coordinator,created_us;
 uint32_t source_epoch,sequence,valid_us;
 uint8_t flags,members;
};
int r2_role_encode(uint8_t [R2_ROLE_AD_BYTES],const struct r2_role_record *,const struct r2_group_config *);
int r2_role_decode(struct r2_role_record *,const uint8_t *,size_t,const struct r2_group_config *);
