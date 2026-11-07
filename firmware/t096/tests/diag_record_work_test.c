/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Complete diagnostic record differential replay; no hardware or storage. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "time_service.h"
#include "diagnostic.h"
#define __aligned(n) __attribute__((aligned(n)))
static int64_t now;
static struct r2_ui_status fixture;
int64_t k_uptime_get(void){return now;}
void r2_ui_snapshot(struct r2_ui_status *out){*out=fixture;}
void r2_ui_diag_snapshot(int *error,uint32_t *maximum,uint32_t *missed){*error=fixture.error;*maximum=fixture.max_frame_ms;*missed=fixture.missed;}
uint32_t r2_ui_stack_unused(void){return 1024;}
#include "diag_under_test.c"
static uint32_t random_state=0x478129ac;
static uint32_t next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void emit(uint32_t v){for(unsigned i=0;i<4;i++)putchar((v>>(8*i))&255);}
int main(void){
 struct r2_identity id={.boot=123};memcpy(id.silicon,"123456789abcdef0",16);
 assert(!r2_diag_init(&id)&&!r2_diag_arm(0,0));
 struct r2_service_status s={.active=true,.root=2,.coordinator=3,.standby=3,.root_boot=9,.standby_boot=10,.source_epoch=11};
 struct r2_clock_view c={0};
 for(unsigned i=0;i<20000;i++){
  now+=i%499==0?3600001:i%137==0?250:1;s.at=(uint64_t)now*1000;
  fixture.error=i%17==0?-(int)(next()%32):0;fixture.max_frame_ms=next();fixture.missed=next();
  s.serving=(i&1)!=0;s.follower=!s.serving;s.hot_ready=(i%3)==0;s.hot_refusal=next()%64;
  if(i%37==0)++s.root_boot;if(i%41==0)++s.source_epoch;if(i%43==0)++s.standby_boot;
  if(i%47==0){++s.hot_withdrawals;s.hot_withdrawal_at=s.at;s.hot_withdrawal_reason=next();}
  s.error=i%53==0?-5:0;if(i%59==0)++s.drops;if(i%61==0)++s.precision.overflows;
  c.reason=i%67==0?R2_INPUT_DRIVER:R2_INPUT_OK;if(i%71==0)++c.faults;
  s.service_until=s.at+(next()%2000000);s.pair_until=s.at+(next()%1000000);s.management_until=s.at+(next()%1000000);
  s.precision.rms_ns=next();s.record_age_us=next();s.record_age_max_us=next();s.rx_latency_max_us=next();
  s.backlog=next()%16;s.rx_queue_peak=next()%32;s.worker_max_us=next();s.rejected=next();
  r2_diag_sample(&s,&c);
  if(i%79==0)r2_diag_command(i,R2_UI,-1,now);
  if(i%83==0)(void)r2_diag_mark();
  if(i%89==0)r2_diag_watchdog(i,-1);
  if(i%97==0){uint32_t record[16]={0x4c525731};record[1]=next();uint64_t token=0;(void)r2_diag_lora(record,false,&token);}
  if(i%7!=0)r2_diag_poll();
  for(unsigned j=0;j<128;j++)emit(package[j]);
  for(unsigned j=0;j<16;j++){emit(previous[j]);emit(current[j]);}
  emit((uint32_t)phase);emit(pending_post);emit(totals.saved);emit(totals.coalesced);
 }
 r2_diag_finish(-1);r2_diag_poll();for(unsigned j=0;j<128;j++)emit(package[j]);
 assert(!ferror(stdout));return 0;
}
