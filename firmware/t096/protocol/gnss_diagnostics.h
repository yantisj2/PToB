/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_auth.h"

#define R2_GNSS_AD_BYTES 88u
#define R2_GNSS_AD_BODY 72u
#define R2_GNSS_MAX_VALID_US 30000000u
#define R2_GNSS_AGE_UNKNOWN UINT16_MAX
#define R2_GNSS_AGE_SATURATED (UINT16_MAX - 1u)
#define R2_GNSS_SATS_UNKNOWN UINT8_MAX
#define R2_GNSS_SOURCE_LOCAL 1u /* GD1 enum: local GNSS, not IEEE timeSource. */
#define R2_GNSS_K_FIX 1u
#define R2_GNSS_K_PPS 2u
#define R2_GNSS_K_QUALIFIED 4u
#define R2_GNSS_K_SATS 8u
#define R2_GNSS_K_PTP 16u
#define R2_GNSS_V_FIX 1u
#define R2_GNSS_V_PPS 2u
#define R2_GNSS_V_QUALIFIED 4u

/* Occasional descriptive snapshot from the serving root. No election, JOIN,
 * timing, qualification, traceability or calibrated precision authority.
 * Ages describe the original sampled_us snapshot; 65534 means saturated,
 * 65535 unknown. A known PTP-shaped dataset is the actual election tuple,
 * including its unknown accuracy/variance values, not PTP wire conformance.
 * The sender is the root, so its identity and boot are encoded only once.
 * Receivers separately bind the canonical advertiser and current timing scope,
 * preserve the first receipt deadline on repeats, and fence replay by boot.
 */
struct r2_gnss_record {
 uint64_t root,root_boot,sampled_us;
 uint32_t mesh_epoch,sequence,valid_us;
 uint8_t known,values,sats,source_kind;
 uint16_t rmc_age_ms,pps_age_ms,gga_age_ms;
 uint8_t priority1,clock_class,accuracy,priority2;
 uint16_t variance;
};
int r2_gnss_encode(uint8_t [R2_GNSS_AD_BYTES],const struct r2_gnss_record *,const struct r2_group_config *);
int r2_gnss_decode(struct r2_gnss_record *,const uint8_t *,size_t,const struct r2_group_config *);
