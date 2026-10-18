/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 */
#define R2_TIMING_EVENTS_TEST 1
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../src/timing_events.c"
#include "../src/initiator_diag.c"
#include "core.h"
#include "rc_admission_policy.h"
static int64_t fake_ms;
static unsigned notes;static bool blocked;
static char note[512];
int64_t k_uptime_get(void){return fake_ms;}
int r2_console_note(const char *bytes,size_t size,uint64_t expiry){
 assert(size<=sizeof note&&expiry==(uint64_t)fake_ms+150);
 if(blocked)return -EAGAIN;
 memcpy(note,bytes,size);note[size-1]=0;++notes;return 0;
}
int main(void){
 for(uint64_t n=0;n<81*17;n++){
  struct rc_admission_scan scan=rc_admission_scan(n);
  assert(scan.interval>=80&&scan.interval<=160&&scan.window>=16&&scan.window<=32&&scan.window<scan.interval);
 }
 assert(rc_admission_interval(false,false)==6&&rc_admission_interval(false,true)==6);
 assert(rc_admission_interval(true,false)==6&&rc_admission_interval(true,true)==40);
 char json[512];
 assert(r2_timing_json(json,sizeof json,1,0)>0&&strstr(json,"\"found\":false"));
 fake_ms=(INT64_C(1)<<33)+15;
 r2_timing_event(R2_TIMING_SYNC_TERM,8,2,1,3,4);
 assert(r2_timing_json(json,sizeof json,1,0)>0&&strstr(json,"\"ms\":8589934607")&&strstr(json,"\"result\":8"));
 blocked=true;r2_timing_console(1,(uint64_t)fake_ms);assert(!notes&&!stream_cursor);
 fake_ms+=100;blocked=false;r2_timing_console(1,(uint64_t)fake_ms);assert(notes==1&&stream_cursor==1&&strstr(note,"#R2_TIMING {"));
 r2_timing_console(1,(uint64_t)fake_ms);assert(notes==1);
 for(unsigned i=0;i<40;i++)r2_timing_event(R2_TIMING_RC_ERROR,-ENOTCONN,UINT64_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX);
 char first[40],missed[40];snprintf(first,sizeof first,"\"sequence\":%u",42-R2_TIMING_EVENT_CAPACITY);
 snprintf(missed,sizeof missed,"\"missed\":%u",41-R2_TIMING_EVENT_CAPACITY);
 assert(r2_timing_json(json,sizeof json,1,0)>0&&strstr(json,first)&&strstr(json,missed));
 assert(r2_timing_json(json,sizeof json,1,41)>0&&strstr(json,"\"found\":false"));
 assert(r2_timing_json(json,30,1,0)==-ENOSPC);
 /* States include long established connections: no acquisition-only gate. */
 struct r2_service_status s={.active=true,.profile_rc=true,.root_boot=7,.source_epoch=1};
 s.identities[0]=2;s.relay.sync_active=true;s.precision.map_valid=s.precision.locked=true;
 r2_timing_sample(&s,(uint64_t)fake_ms);unsigned before=produced;
 r2_timing_sample(&s,(uint64_t)fake_ms+29999);assert(produced==before);
 fake_ms+=30000;r2_timing_sample(&s,(uint64_t)fake_ms);assert(produced==before+1&&records[(produced-1)%R2_TIMING_EVENT_CAPACITY].kind==R2_TIMING_HEARTBEAT);
 s.precision.locked=false;r2_timing_sample(&s,(uint64_t)fake_ms+1);assert(produced==before+2);
 s.precision.locked=true;r2_timing_sample(&s,(uint64_t)fake_ms+2);assert(produced==before+3);
 s.root_boot=8;r2_timing_sample(&s,(uint64_t)fake_ms+3);assert(produced==before+4);
 s.profile_rc=false;s.gm_capable=s.hot_ready=true;s.hot_refusal=7;
 before=produced;r2_timing_sample(&s,(uint64_t)fake_ms+4);assert(produced==before+2);
 assert(records[(produced-2)%R2_TIMING_EVENT_CAPACITY].kind==R2_TIMING_HOT);
 s.hot_ready=false;s.hot_withdrawals=1;s.hot_withdrawal_reason=3;
 before=produced;r2_timing_sample(&s,(uint64_t)fake_ms+5);assert(produced==before+2);
 assert(records[(produced-2)%R2_TIMING_EVENT_CAPACITY].result==3&&records[(produced-2)%R2_TIMING_EVENT_CAPACITY].b==1);
 /* Sticky attempt evidence survives actual ring overrun, without increasing
  * record RAM. Frame the maximal reply through the real CRC formatter. */
 struct r2_init_diag baseline;r2_init_diag_snapshot(&baseline);
 const uint8_t address[6]={1,2,3,4,5,0xc6};r2_init_diag_target(address,1,2);
 r2_init_diag_aux_rsp(true,true,false);r2_init_diag_emit(UINT64_MAX,INT32_MIN,&baseline);
 char sticky[1024]="{}",again[1024]="{}";int sticky_n=r2_init_diag_append(sticky,sizeof sticky,2);assert(sticky_n>0);
 for(unsigned i=0;i<R2_TIMING_EVENT_CAPACITY*4;i++)r2_timing_event(1,0,0,0,0,0);
 assert(r2_init_diag_append(again,sizeof again,2)==sticky_n&&!strcmp(sticky,again));
 char wide[2977],frame[3072];memset(wide,'x',2070);memcpy(wide,"{\"padding\":\"",12);wide[2068]='\"';wide[2069]='}';wide[2070]=0;
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++)state.count[i]=UINT32_MAX;
 memset(&baseline,0,sizeof baseline);r2_init_diag_emit(UINT64_MAX,INT32_MIN,&baseline);
 int wide_n=r2_init_diag_append(wide,sizeof wide,2070);assert(wide_n>2070&&wide_n<2976);
 int frame_n=r2_frame(frame,sizeof frame,UINT32_MAX,UINT32_MAX,wide);assert(frame_n>wide_n&&frame_n<3072);
 unsigned request,sequence,size,crc;assert(sscanf(frame,"R2 %x %x %u %x",&request,&sequence,&size,&crc)==4);
 const char *payload=strchr(frame,'\n')+1;assert(size==(unsigned)wide_n&&!memcmp(payload,wide,size)&&r2_crc32(payload,size)==crc);
 /* Do not alias a new record to an old cursor after sequence exhaustion. */
 produced=UINT32_MAX-2;
 r2_timing_event(1,0,0,0,0,0);assert(records[(UINT32_MAX-2)%R2_TIMING_EVENT_CAPACITY].sequence==UINT32_MAX-1);
 r2_timing_event(1,0,0,0,0,0);assert(records[(UINT32_MAX-1)%R2_TIMING_EVENT_CAPACITY].sequence==UINT32_MAX);
 r2_timing_event(1,INT_MIN,UINT64_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX);assert(produced==UINT32_MAX&&exhausted==1);
 assert(r2_timing_json(json,sizeof json,UINT64_MAX,UINT32_MAX)>0&&strstr(json,"\"exhausted\":1"));
 puts("PASS timing events: boot/cursor binding, overwrite gaps, 64-bit uptime, backpressure, heartbeat, established lock loss/recovery, scope change, exhaustion");
}
