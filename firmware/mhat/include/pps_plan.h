/* Copyright (c) 2026 Jonathan Yantis. MIT. Pure checked PPS planner. */
#pragma once
#include "rc_unicast.h"
#include <limits.h>
/* Re-project the latest admitted map until the edge is near. */
#define MHAT_PPS_ARM_MAX_TICKS UINT64_C(320000) /* 20 ms at 16 MHz */
static inline bool mhat_pps_commit_ready(uint64_t now,uint64_t edge) {
    return edge>now && edge-now>=48000 && edge-now<=MHAT_PPS_ARM_MAX_TICKS;
}
static inline bool mhat_utc_at(const struct r2_uc_clock *o,uint64_t tick,uint64_t *utc) {
    uint64_t ns,elapsed;
    if(!o->valid||tick>=o->until_tick||tick>=o->session_until||tick<o->map.effective||
       !rc5_map_project(&o->map,tick,&ns,&elapsed)||ns<o->origin.issued||ns>=o->origin.expires)return false;
    if(o->origin.utc_offset<0){uint64_t mag=(uint64_t)(-(o->origin.utc_offset+1))+1;if(ns<mag)return false;*utc=ns-mag;}
    else {if(ns>UINT64_MAX-(uint64_t)o->origin.utc_offset)return false;*utc=ns+(uint64_t)o->origin.utc_offset;}
    return true;
}
static inline bool mhat_pps_plan(const struct r2_uc_clock *o,uint64_t now,uint64_t last,uint64_t *edge,uint64_t *second) {
    uint64_t utc,value;
    if(now>UINT64_MAX-UINT64_C(16000000)||!mhat_utc_at(o,now,&utc))return false;
    uint64_t s=utc/1000000000+1;
    if(s<=last||s>UINT64_MAX/1000000000)return false;
    uint64_t target=s*UINT64_C(1000000000),lo=now,hi=now+UINT64_C(16000000);
    if(hi>=o->until_tick)hi=o->until_tick-1;
    if(hi>=o->session_until)hi=o->session_until-1;
    if(hi<=lo)return false;
    /* The source's projected expiry can precede the local/session deadline.
     * An invalid far endpoint does not make an earlier pulse invalid. Search
     * for the target OR the first invalid tick, then require a valid crossing
     * and the complete pulse below. This never extends any permission. */
    while(hi-lo>1){uint64_t mid=lo+(hi-lo)/2;
        if(!mhat_utc_at(o,mid,&value)||value>=target)hi=mid;else lo=mid;}
    /* Nearest attainable edge, rather than always rounding late. Fractional
     * phase remains in the disciplined map; output alone is quantized. */
    uint64_t lower,upper;
    if(!mhat_utc_at(o,lo,&lower)||!mhat_utc_at(o,hi,&upper)||lower>target||upper<target)return false;
    if(target-lower<upper-target)hi=lo;
    if(hi-now<48000||hi>UINT64_MAX-160000||!mhat_utc_at(o,hi+160000,&value))return false;
    *edge=hi;*second=s;return true;
}
