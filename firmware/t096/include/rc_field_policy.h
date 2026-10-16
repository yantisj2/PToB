/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "rc_boundary.h"
/* The field roster fixes allowed roles, not the selected address or boot.
 * A relay client accepts advertised RC capability; a dedicated RC accepts GM.
 * This is only candidate selection. Challenge admission still grants time. */
static inline bool rc5_field_candidate(const struct rc5_routes *r,unsigned self,unsigned candidate){
 if(!r||self>=r->count||candidate>=r->count||!self||candidate==self)return false;
 const struct rc5_node *p=&r->node[candidate];
#if defined(R2_RC_AUTO)
 return p->transmitter&&candidate==r->node[self].parent&&p->depth<=(r->node[self].transmitter?1:2);
#else
 return p->transmitter&&(r->node[self].transmitter?p->slot==0:p->slot>0);
#endif
}

/* Finite console treatment keeps its original arrival deadline. */
static inline bool rc5_field_deadline(uint64_t arrival_ms,uint32_t seconds,uint64_t now_us,uint64_t *out){
 if(!out||!seconds||arrival_ms>UINT64_MAX/1000)return false;
 uint64_t start=arrival_ms*1000,duration=(uint64_t)seconds*1000000;
 if(start>now_us||start>UINT64_MAX-duration||start+duration<=now_us)return false;
 *out=start+duration;return true;
}

/* Preserve the chosen phase when preparation misses a request: advance whole
 * 62.5 ms frames, never start late and move the train into a different slot. */
static inline bool rc5_field_advance_start(uint64_t *at,uint64_t now){
 if(!at)return false;
 if(*at>=now)return true;
 uint64_t frames=(now-*at)/1000000+((now-*at)%1000000!=0);
 if(frames>(UINT64_MAX-*at)/1000000)return false;
 *at+=frames*1000000;return true;
}
/* Nearest non-overdue request with the same frame phase. The single owner
 * waits at most one existing frame; no narrow polling opportunity is needed. */
static inline bool rc5_field_start_wait(uint64_t *at,uint64_t now,uint32_t *us){
 if(!at||!us||!rc5_field_advance_start(at,now))return false;
 uint64_t ticks=(*at-now)%1000000;
 *at=now+ticks;*us=(uint32_t)((ticks+15)/16);return true;
}
static inline bool rc5_field_tx_current(const struct rc5_packet *p,unsigned self,uint64_t boot,uint32_t train,uint32_t epoch,bool running,uint64_t tick,uint64_t requested){
 return p&&running&&p->sender==self&&p->boot==boot&&p->train==train&&p->epoch==epoch&&tick>=requested;
}
