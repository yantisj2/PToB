/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/time_zone.h"
#include <stddef.h>

#define R2_TZ_POSITION_US UINT64_C(30000000)
#define R2_TZ_LOOKUP_US UINT64_C(60000000)
#define R2_TZ_HOLD_US UINT64_C(600000000)
enum r2_tz_mode { R2_TZ_MANUAL_OFFSET, R2_TZ_MANUAL_ZONE, R2_TZ_AUTOMATIC };
enum r2_tz_reason { R2_TZ_WAIT, R2_TZ_LIVE, R2_TZ_HOLD, R2_TZ_STALE,
 R2_TZ_AMBIGUOUS, R2_TZ_UNSUPPORTED, R2_TZ_NO_UTC, R2_TZ_DB_EXPIRED };
struct r2_position {
 int64_t received_us;
 int32_t latitude_e7, longitude_e7;
 bool valid;
};
struct r2_tz_selector {
 uint64_t lookup_at, confirmed_at, candidate_at, last_fix;
 uint16_t selected, candidate;
 uint8_t hits, reason;
 bool looked;
};
/* Read-only generated image; UINT16_MAX means an ambiguous geographic cell. */
bool r2_tz_db_offset(uint16_t id, uint64_t utc_us, int16_t *offset);
uint16_t r2_tz_db_geo(int32_t latitude_e7, int32_t longitude_e7);
const char *r2_tz_db_name(uint16_t id);
size_t r2_tz_db_size(void);
const char *r2_tz_db_version(void);
bool r2_tz_db_window(uint64_t utc_us);
void r2_tz_selector_step(struct r2_tz_selector *, const struct r2_position *,
                         uint64_t now, uint64_t utc_us, bool utc_valid);
struct r2_tz r2_tz_selector_value(const struct r2_tz_selector *, unsigned mode,
 struct r2_tz manual, uint64_t now, uint64_t utc_us, bool utc_valid);
const char *r2_tz_reason_name(unsigned reason);
