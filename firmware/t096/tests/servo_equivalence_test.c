/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "precision_servo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
bool cached_observe(r2_servo_clock_t *,const r2_servo_sample_t *,bool,bool);
bool uncached_observe(r2_servo_clock_t *,const r2_servo_sample_t *,bool,bool);
extern unsigned cached_fits,uncached_fits;
int main(void){
 r2_servo_clock_t cached={0},uncached={0};uint64_t local=32000000,remote=16000000;uint32_t session=1,sequence=0;
 for(unsigned i=0;i<20000;i++){
  if(i&&i%5000==0){++session;sequence=0;}
  uint64_t delta=i&&i%2000==0?32000000:1000000;
  local+=delta;remote+=delta;
  r2_servo_sample_t sample={.session=session,.sequence=++sequence,.local=local,
   .remote=remote+(i%103==0?400:0),.available=local+80000+(i%7)*16000,.peer_pps_below200=i%17!=0};
  bool servo=i%131!=0,permission=i%137!=0;
  assert(cached_observe(&cached,&sample,servo,permission)==uncached_observe(&uncached,&sample,servo,permission));
  assert(!memcmp(&cached,&uncached,sizeof cached));
  if(i%971==0){
   assert(!cached_observe(&cached,&sample,true,true)&&!uncached_observe(&uncached,&sample,true,true));
   assert(!memcmp(&cached,&uncached,sizeof cached));
  }
 }
 assert(cached_fits*4<uncached_fits);
 printf("PASS20000 observations plus duplicates: bit-identical complete servo state, cached_fits=%u uncached_fits=%u\n",cached_fits,uncached_fits);
}
