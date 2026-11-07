/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Production passive owner, with host-only observation of its view helper. */
static unsigned projection_calls;
#include "lora_ram_fixture.h"
static void poll_at_ms(uint64_t ms){now_us=ms*1000;r2_lora_console_poll(22,ms);}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);init(false);
 unsigned locks=lock_calls;
 if(mode==1){
  for(uint64_t ms=0;ms<=61000;ms++)poll_at_ms(ms);
  assert(projection_calls==61&&lock_calls-locks==61);
  assert(console_attempts==2&&console_notes==2);
  printf("PASS steady61001calls: snapshots=%u projections=%u notes=%u\n",lock_calls-locks,projection_calls,console_notes);
 }else if(mode==2){
  for(uint64_t ms=0;ms<1000;ms++)poll_at_ms(ms);
  assert(!projection_calls&&!console_attempts);poll_at_ms(1000);assert(console_notes==1);
  for(uint64_t ms=1001;ms<=2000;ms++)poll_at_ms(ms);
  assert(projection_calls==2&&console_notes==1);
  diagnostics.error=-EIO;
  for(uint64_t ms=2001;ms<3000;ms++)poll_at_ms(ms);
  assert(projection_calls==2&&console_notes==1);poll_at_ms(3000);assert(console_notes==2);
  puts("PASS first eligibility1s, changed note at next1Hz sample; no catchup");
 }else if(mode==3){
  console_busy=true;
  for(uint64_t ms=0;ms<=3999;ms++)poll_at_ms(ms);
  assert(projection_calls==3&&console_attempts==3&&!console_notes);
  console_busy=false;poll_at_ms(4000);assert(console_attempts==4&&console_notes==1);
  for(uint64_t ms=4001;ms<64000;ms++)poll_at_ms(ms);
  assert(console_attempts==4);poll_at_ms(64000);assert(console_notes==2);
  puts("PASS refused-note retries1Hz; success starts original60s note interval");
 }else if(mode==4){
  uint64_t start=UINT32_MAX-499ULL;poll_at_ms(start);assert(console_notes==1);
  diagnostics.error=-EIO;
  for(uint64_t ms=start+1;ms<start+1000;ms++)poll_at_ms(ms);
  assert(projection_calls==1&&console_notes==1);poll_at_ms(start+1000);
  assert(projection_calls==2&&console_notes==2);
  for(uint64_t ms=start+1001;ms<start+61000;ms++)poll_at_ms(ms);
  assert(console_notes==2);poll_at_ms(start+61000);assert(console_notes==3);
  puts("PASS unsigned49day wrap preserves1Hz and60s note gates");
 }else if(mode==5){
  poll_at_ms(1000);assert(projection_calls==1);
  now_us=1501000;r2_diag_scratch.lora.terminal=1;
  r2_lora_watchdog_poll();assert(atomic_get(&faulted));
  assert(projection_calls==1);poll_at_ms(1501);assert(projection_calls==1);
  assert(r2_lora_close_result()==-EIO);poll_at_ms(2000);assert(projection_calls==2);
  puts("PASS essential fault fencing runs independently between diagnostic samples");
 }else if(mode==6){
  for(uint64_t ms=0;ms<=61000;ms++)poll_at_ms(ms);
  assert(projection_calls>50000&&console_notes==2);
  printf("PASS baseline negative control: snapshots=%u projections=%u notes=%u\n",lock_calls-locks,projection_calls,console_notes);
 }else return 2;
 return 0;
}
