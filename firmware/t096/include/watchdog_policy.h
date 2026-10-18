/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define R2_WD_GM_MS 1200000u
/* Timing absence is handled by parent selection first. Only a continuous
 * half hour without usable timing requests the existing bounded recovery. */
#define R2_WD_RC_MS 1800000u
#define R2_WD_RETRIES 3u
#define R2_WD_RC_HEALTHY_MS 30000u
#define R2_WD_RECOVERY_MS 30000u
/* One initial attempt followed by three retries. Unsigned elapsed arithmetic
 * is valid across uptime wrap; every interval is less than 2^31 ms. */
enum r2_wd_action { R2_WD_NONE, R2_WD_RESTART, R2_WD_RESET };
struct r2_wd_policy {
 uint32_t since, generation, retries, recovery_at, healthy_at, observed_at;
 bool armed, rc, recovering, healthy_seen;
};
enum r2_wd_action r2_wd_policy_step(struct r2_wd_policy *,uint32_t now,
 bool enabled,bool rc,bool useful,bool pending,uint32_t generation);
