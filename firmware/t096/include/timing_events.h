/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Three former40B journal records fund the sticky initiation summary and
 * AUX counters. Keep the same total RAM/precision buffers and >=1KB margin. */
#define R2_TIMING_EVENT_CAPACITY 29u
enum r2_timing_kind {
 R2_TIMING_STATE=1, R2_TIMING_HEARTBEAT, R2_TIMING_SCOPE,
 R2_TIMING_PARENT, R2_TIMING_SYNC_CREATE, R2_TIMING_SYNC_READY,
 R2_TIMING_SYNC_TERM, R2_TIMING_SYNC_CANCEL, R2_TIMING_RC_CONNECT,
 R2_TIMING_RC_CONNECTED, R2_TIMING_RC_ERROR, R2_TIMING_RC_CLOSE,
 R2_TIMING_RC_DISCONNECTED, R2_TIMING_GM_ADMISSION, R2_TIMING_OWNER_FAULT,
 R2_TIMING_GM_CONNECTED, R2_TIMING_GM_DISCONNECTED, R2_TIMING_GM_MTU_ERROR,
 R2_TIMING_HOLDOVER_EXPIRE, R2_TIMING_HOLDOVER_RECOVER, R2_TIMING_HOT,
 R2_TIMING_INIT_TARGET, R2_TIMING_INIT_RX, R2_TIMING_INIT_REQUEST,
 R2_TIMING_INIT_ABORT, R2_TIMING_RC_DRAIN, R2_TIMING_RC_RETRY,
 R2_TIMING_RC_ADVERTISING, R2_TIMING_INIT_RX_RESERVE, R2_TIMING_RC_DELIVERY,
 R2_TIMING_RC_PHASE, R2_TIMING_GM_CONTROL, R2_TIMING_INIT_AUX
};
struct r2_service_status;
/* A cursor is scoped to one source/MCU boot, survives logical owner restarts.
 * Fixed RAM only. Never grants authority, writes flash or starts recovery. */
#if defined(CONFIG_ARM) || defined(R2_TIMING_EVENTS_TEST)
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b);
void r2_timing_sample(const struct r2_service_status *,uint64_t now_ms);
int r2_timing_json(char *,size_t,uint64_t boot,uint32_t after);
void r2_timing_console(uint64_t boot,uint64_t now_ms);
#else
static inline void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){(void)k;(void)r;(void)p;(void)d;(void)a;(void)b;}
static inline void r2_timing_sample(const struct r2_service_status *s,uint64_t n){(void)s;(void)n;}
static inline int r2_timing_json(char *s,size_t n,uint64_t b,uint32_t a){(void)s;(void)n;(void)b;(void)a;return -1;}
static inline void r2_timing_console(uint64_t b,uint64_t n){(void)b;(void)n;}
#endif
