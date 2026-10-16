/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Bounded RAM journal; callbacks copy one record, main formats/exports.
 */
#include "timing_events.h"
#include "ble_service.h"
#include "build_identity.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#if defined(CONFIG_ARM)
#include <zephyr/kernel.h>
#define R2_LOCK_UNIT 16u
#include "health_lock.h"
static struct k_spinlock event_lock;
#else
/* Sequential host regression boundary; production uses the native spinlock. */
static unsigned event_lock;
typedef unsigned k_spinlock_key_t;
static unsigned k_spin_lock(unsigned *l){(void)l;return 0;}
static void k_spin_unlock(unsigned *l,unsigned k){(void)l;(void)k;}
extern int64_t k_uptime_get(void);
#endif
struct timing_record {
 uint64_t ms,peer;
 uint32_t sequence,kind,detail,a,b;
 int32_t result;
};
_Static_assert(sizeof(struct timing_record)==40,"Bounded event RAM");
static struct timing_record records[R2_TIMING_EVENT_CAPACITY];
static uint32_t produced,exhausted;
static uint32_t stream_cursor;
static uint64_t stream_due;
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b)
{
 struct timing_record record={.ms=(uint64_t)k_uptime_get(),.peer=peer,
  .kind=kind,.detail=detail,.a=a,.b=b,.result=result};
 k_spinlock_key_t key=k_spin_lock(&event_lock);
 if(produced==UINT32_MAX){if(exhausted<UINT32_MAX)++exhausted;}
 else {record.sequence=++produced;records[(produced-1)%R2_TIMING_EVENT_CAPACITY]=record;}
 k_spin_unlock(&event_lock,key);
}
/* Copy a single record under the lock; CRC/framing/USB stay outside it. */
static int format(char *out,size_t capacity,uint64_t boot,uint32_t after,uint32_t *sequence)
{
 struct timing_record r={0};uint32_t last,lost,first;
 k_spinlock_key_t key=k_spin_lock(&event_lock);
 last=produced;lost=exhausted;first=last>R2_TIMING_EVENT_CAPACITY?last-R2_TIMING_EVENT_CAPACITY+1:1;
 if(after<last){uint32_t next=after<first?first:after+1;r=records[(next-1)%R2_TIMING_EVENT_CAPACITY];}
 k_spin_unlock(&event_lock,key);
 uint32_t missed=r.sequence&&after<first?first-after-1:0;
 *sequence=r.sequence;
 int n=snprintf(out,capacity,
  "{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"found\":%s,\"last\":%u,\"overwritten\":%u,\"exhausted\":%u,\"missed\":%u,\"sequence\":%u,\"ms\":%llu,\"kind\":%u,\"result\":%d,\"peer\":\"%016llx\",\"detail\":%u,\"a\":%u,\"b\":%u}",
  R2_SOURCE_SHA,(unsigned long long)boot,r.sequence?"true":"false",last,
  last>R2_TIMING_EVENT_CAPACITY?last-R2_TIMING_EVENT_CAPACITY:0,lost,missed,
  r.sequence,(unsigned long long)r.ms,r.kind,r.result,(unsigned long long)r.peer,r.detail,r.a,r.b);
 return n<0||(size_t)n>=capacity?-ENOSPC:n;
}
int r2_timing_json(char *out,size_t capacity,uint64_t boot,uint32_t after)
{uint32_t sequence;return format(out,capacity,boot,after,&sequence);}
void r2_timing_console(uint64_t boot,uint64_t now)
{
 if(now<stream_due)return;
 stream_due=now+100; /* At most ten small best-effort notes/second. */
 char line[512];uint32_t sequence;
 int n=format(line+11,sizeof line-13,boot,stream_cursor,&sequence);
 if(n<=0||!sequence)return;
 memcpy(line,"#R2_TIMING ",11);line[11+n]='\n';
 /* Dedicated low-priority notes allow 512 bytes; console still reserves no
  * queue slot unless idle. Failure retains this cursor for the next poll. */
 if(!r2_console_note(line,(size_t)n+12,now+150))stream_cursor=sequence;
}
void r2_timing_sample(const struct r2_service_status *s,uint64_t now)
{
 static bool seen;static uint32_t previous,epoch;static int error;
 static uint64_t peer,root_boot,heartbeat;
 uint32_t flags=s->active|(s->profile_rc<<1)|(s->relay.sync_active<<2)|
  (s->relay.sync_creating<<3)|(s->precision.map_valid<<4)|(s->precision.locked<<5)|
  (s->precision.authorized<<6)|(s->serving<<7)|(s->follower<<8)|
  (s->closing<<9)|(s->profile_pending<<10)|(s->relay.phase_acquiring<<11);
 uint64_t selected=s->profile_rc?s->identities[0]:s->root<4?s->identities[s->root]:0;
 if(!seen||selected!=peer||s->root_boot!=root_boot||s->source_epoch!=epoch){
  r2_timing_event(R2_TIMING_SCOPE,0,selected,s->source_epoch,(uint32_t)s->root_boot,(uint32_t)(s->root_boot>>32));
  peer=selected;root_boot=s->root_boot;epoch=s->source_epoch;
 }
 if(!seen||flags!=previous||error!=s->error){
  r2_timing_event(R2_TIMING_STATE,s->error,selected,flags,s->precision.accepted,s->precision.rejected);
  previous=flags;error=s->error;heartbeat=now;
 }else if(now>=heartbeat&&now-heartbeat>=30000){
  r2_timing_event(R2_TIMING_HEARTBEAT,s->error,selected,flags,s->precision.accepted,s->precision.rejected);heartbeat=now;
 }
 seen=true;
}
