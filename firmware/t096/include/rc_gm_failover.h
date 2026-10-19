/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define RC_GM_PENDING 1u
#define RC_GM_SELECTED 2u
#define RC_GM_TRIED 4u
#define RC_GM_RESERVE_USED 8u
/* Scheduling evidence only. No map, authentication or source permission. */
struct rc_gm_failover {
 uint64_t id,boot;
 uint32_t epoch,sequence,seen_ms,valid_us,reserve_ms;
 uint8_t flags;
};
_Static_assert(sizeof(struct rc_gm_failover)==40,"Bounded GM failover state");
static inline bool rc_gm_hint_fresh(const struct rc_gm_failover *s,uint32_t now){
 return s->id&&(s->flags&RC_GM_PENDING)&&s->valid_us&&s->valid_us<=2000000&&
   (uint32_t)(now-s->seen_ms)<(s->valid_us/1000);
}
static inline bool rc_gm_hint_observe(struct rc_gm_failover *s,uint64_t id,uint64_t boot,
 uint32_t epoch,uint32_t sequence,uint32_t at,uint32_t now,uint32_t valid_us,bool already_direct){
 if(!id||!boot||!epoch||!sequence||!valid_us||valid_us>2000000||
    (uint32_t)(now-at)>=valid_us/1000)return false;
 bool same=s->id==id&&s->boot==boot&&s->epoch==epoch;
 if(s->id==id&&s->boot==boot&&sequence<=s->sequence)return false;
 /* Finish one live event before another broadcaster can replace it. */
 if(!same&&rc_gm_hint_fresh(s,now))return false;
 uint8_t retained=s->flags&RC_GM_RESERVE_USED;
 if(!same){s->flags=retained;s->id=id;s->boot=boot;s->epoch=epoch;}
 s->sequence=sequence;s->seen_ms=at;s->valid_us=valid_us;
 if(already_direct){s->flags=(s->flags&~(RC_GM_PENDING|RC_GM_SELECTED))|RC_GM_TRIED;return false;}
 if(!(s->flags&RC_GM_TRIED))s->flags|=RC_GM_PENDING;
 return !same&&(s->flags&RC_GM_PENDING);
}
static inline bool rc_gm_reserve_available(const struct rc_gm_failover *s,uint32_t now){
 return !(s->flags&RC_GM_RESERVE_USED)||(uint32_t)(now-s->reserve_ms)>=60000;
}
static inline bool rc_gm_expedited(const struct rc_gm_failover *s,uint32_t now){
 return rc_gm_hint_fresh(s,now)&&(s->flags&RC_GM_SELECTED)&&rc_gm_reserve_available(s,now);
}
static inline void rc_gm_attempted(struct rc_gm_failover *s,uint32_t now,bool extra){
 if(extra){s->reserve_ms=now;s->flags|=RC_GM_RESERVE_USED;}
 s->flags=(s->flags&~(RC_GM_PENDING|RC_GM_SELECTED))|RC_GM_TRIED;
}
