/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "time_zone_selector.h"
#ifndef R2_TZ_MODE
#define R2_TZ_MODE R2_TZ_AUTOMATIC
#endif
#ifndef R2_TZ_MANUAL_ID
#define R2_TZ_MANUAL_ID 2
#endif
#ifndef R2_TZ_MANUAL_MIN
#define R2_TZ_MANUAL_MIN (-300)
#endif
struct r2_tz_local_status {
 uint64_t position_age_us, confirmed_age_us;
 uint16_t selected;
 uint8_t mode,reason;
 bool hold;
};
/* Thread-safe metadata snapshots. Neither call services UART or grants time. */
struct r2_tz r2_time_tz_at(uint64_t now,uint64_t utc_us,bool utc_valid);
void r2_time_tz_status(struct r2_tz_local_status *,uint64_t now);
void r2_time_position_snapshot(struct r2_position *);
