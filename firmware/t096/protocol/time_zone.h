/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define R2_TZ_GM_PERIOD_US UINT64_C(250000)
/* Existing nominal 100 ms discovery advertising period. */
#define R2_TZ_RC_PERIOD_US UINT64_C(100000)
struct r2_tz { uint16_t id; int16_t offset_min; };
struct r2_tz_lease {
 uint64_t until_us;
 struct r2_tz value;
 uint16_t received, conflicts;
};
static inline struct r2_tz r2_tz_unknown(void) { return (struct r2_tz){0,INT16_MIN}; }
static inline struct r2_tz r2_tz_manual(void) { return (struct r2_tz){2,-300}; }
static inline bool r2_tz_valid(struct r2_tz t) {
 return t.id && t.offset_min>=-840 && t.offset_min<=840 && (t.id!=1 || !t.offset_min);
}
static inline void r2_tz_put(uint8_t w[4],struct r2_tz t) {
 if(!r2_tz_valid(t))t=r2_tz_unknown();
 uint16_t v=(uint16_t)t.offset_min;
 w[0]=(uint8_t)t.id;w[1]=(uint8_t)(t.id>>8);w[2]=(uint8_t)v;w[3]=(uint8_t)(v>>8);
}
static inline struct r2_tz r2_tz_get(const uint8_t w[4]) {
 uint16_t v=(uint16_t)w[2]|((uint16_t)w[3]<<8);
 struct r2_tz t={(uint16_t)w[0]|((uint16_t)w[1]<<8),
  v<=INT16_MAX?(int16_t)v:(int16_t)(-1-(int32_t)(UINT16_MAX-v))};
 return r2_tz_valid(t)?t:r2_tz_unknown();
}
/* Coarse mesh configuration only. A repeated matching advertisement renews
 * this lease; no precision sample, source permit, clock fit or epoch is touched.
 * One mesh keeps its first configured value through lease gaps/GM handovers. */
static inline bool r2_tz_observe(struct r2_tz_lease *s,
 struct r2_tz value,uint64_t received,uint64_t now,uint64_t period) {
 if(!s||!period||period>UINT64_MAX/3||received>now||
    received>UINT64_MAX-period*3||now>=received+period*3||!r2_tz_valid(value))return false;
 if(s->value.id&&(s->value.id!=value.id||s->value.offset_min!=value.offset_min)){
  if(s->conflicts<UINT16_MAX)++s->conflicts;
  return false;
 }
 s->value=value;s->until_us=received+period*3;
 if(s->received<UINT16_MAX)++s->received;
 return true;
}
/* Presence renews a mesh's fixed coarse configuration, including an
 * authenticated periodic advertisement with no usable precision correction.
 * The lease never consults a precision map or clock lock. */
static inline void r2_tz_renew(struct r2_tz_lease *s,uint64_t now,uint64_t period) {
 if(s && s->value.id && period && period<=UINT64_MAX/3 && now<=UINT64_MAX-3*period)
  s->until_us=now+3*period;
}
static inline struct r2_tz r2_tz_current(const struct r2_tz_lease *s,uint64_t now) {
 return s&&s->value.id&&now<s->until_us?s->value:r2_tz_unknown();
}
static inline bool r2_tz_local_us(struct r2_tz t,int64_t utc,int64_t *out) {
 if(!out||!r2_tz_valid(t))return false;
 int64_t delta=(int64_t)t.offset_min*INT64_C(60000000);
 if((delta>0&&utc>INT64_MAX-delta)||(delta<0&&utc<INT64_MIN-delta))return false;
 *out=utc+delta;return true;
}
