/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
/* Production source arithmetic; synthetic PPS/UTC labels are unqualified. */
#include "clock.h"
#include "precision.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static const int64_t second=INT64_C(1000000000);
static const int64_t base=INT64_C(1792800000000000000);

static void ready(struct r2_source_clock *s,struct r2_pps_precision *p){
 r2_source_clock_init(s);r2_pps_precision_init(p);
 assert(r2_pps_precision_add(p,(struct r2_capture){100,1,R2_CAPTURE_PPS}));
 assert(r2_pps_precision_add(p,(struct r2_capture){16000100,1,R2_CAPTURE_PPS}));
 assert(r2_source_clock_publish(s,p,(struct r2_capture){16080100,1,R2_CAPTURE_NOW}));
}

static void phase(int64_t coarse_phase){
 struct r2_source_clock s;struct r2_pps_precision p;ready(&s,&p);
 struct r2_capture at={16080100,1,R2_CAPTURE_NOW};uint64_t ns,gen;int64_t utc;
 assert(r2_source_clock_read(&s,at,&ns,&gen));
 /* Reproduce an initial arbitrary observation carrying UART/servo phase.
  * The relative coordinate already has its zero on a physical PPS. */
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+coarse_phase,7,R2_ASSOC_UNKNOWN)==1);
 assert(s.utc_offset_ns==base&&s.utc_offset_ns%second==0);
 assert(s.utc_association==R2_ASSOC_UNKNOWN);
 assert(r2_source_utc_read(&s,at,7,&utc)&&utc==base+(int64_t)ns);
 for(unsigned i=0;i<40;i++){
  struct r2_capture edge={p.last+16000000,1,R2_CAPTURE_PPS};
  assert(r2_source_clock_read(&s,edge,&ns,&gen));
  assert(r2_source_utc_read(&s,edge,7,&utc));
  assert(ns%second==0&&utc%second==0);
  assert(r2_pps_precision_add(&p,edge));at.tick=edge.tick+80000;
  assert(r2_source_clock_publish(&s,&p,at));
  assert(r2_source_clock_read(&s,at,&ns,&gen));
  assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+(i%2?400000000:-400000000),7,R2_ASSOC_UNKNOWN)==1);
  assert(s.utc_offset_ns==base);
 }
 assert(!r2_source_utc_read(&s,(struct r2_capture){p.last+24000000,1,R2_CAPTURE_NOW},7,&utc));
 assert(!r2_source_utc_read(&s,at,8,&utc));
 assert(!r2_source_utc_read(&s,(struct r2_capture){at.tick,2,R2_CAPTURE_NOW},7,&utc));
}

int main(void){
 const int64_t phases[]={-28835875,28835875,-499999999,499999999,0,-1,1};
 for(unsigned i=0;i<sizeof phases/sizeof phases[0];i++)phase(phases[i]);
 struct r2_source_clock s;struct r2_pps_precision p;ready(&s,&p);
 struct r2_capture at={16080100,1,R2_CAPTURE_NOW};uint64_t ns,gen;
 assert(r2_source_clock_read(&s,at,&ns,&gen));
 /* An exactly half-second candidate has no unique nearest label. */
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+second/2,1,R2_ASSOC_UNKNOWN)==0&&!s.utc_bound);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns-second/2,1,R2_ASSOC_UNKNOWN)==0&&!s.utc_bound);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+second/2+1,1,R2_ASSOC_UNKNOWN)==1);
 assert(s.utc_offset_ns==base+second);
 ready(&s,&p);
 assert(r2_source_utc_bind(&s,at,0,1,R2_ASSOC_UNKNOWN)==1&&s.utc_offset_ns==0);
 ready(&s,&p);
 /* Nearest whole-second values outside int64 are refused before multiply. */
 assert(r2_source_utc_bind(&s,at,INT64_MAX,1,R2_ASSOC_UNKNOWN)==0&&!s.utc_bound);
 /* A negative offset is also rounded without signed overflow. */
 s.maps[0].anchor_ns=(uint64_t)base+5000000;
 assert(r2_source_utc_bind(&s,at,0,1,R2_ASSOC_UNKNOWN)==1&&s.utc_offset_ns==-base);
 puts("PASS UTC basis: initial -28.835875ms and both signs, 280 subsequent PPS, immutable translation, half-second ambiguity, overflow and original expiry/epochs; absolute UTC association UNKNOWN");
}
