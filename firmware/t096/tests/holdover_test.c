/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "holdover.h"
#include <assert.h>
#include <stdio.h>
static uint64_t remote_at(uint64_t tick,int32_t rate){
 uint64_t n=tick*125/2,d=(uint64_t)(1000000000+(int64_t)rate);
 return n/d*1000000000+n%d*1000000000/d;
}
static void frequency(int32_t truth,uint64_t offset){
 struct r2_hold_rate s={0};
 for(unsigned i=0;i<=160;i++){
  uint64_t t=offset+R2_CAPTURE_HZ+(uint64_t)i*R2_CAPTURE_HZ/2;
  uint8_t ch=i%9;int64_t bias=((int)ch-4)*120;
  assert(r2_hold_observe(&s,t,(uint64_t)((int64_t)remote_at(t,truth)+bias),t+16000,ch));
  r2_hold_refresh(&s,t+16000);
  assert(s.count<=65);
 }
 printf("frequency truth=%d rates=%d/%d guards=%u/%u\n",truth,s.short_fit.rate,s.long_fit.rate,s.short_fit.guard,s.long_fit.guard);fflush(stdout);
 assert(s.short_fit.guard<=10&&s.long_fit.guard<=10);
 assert(r2_hold_abs((int64_t)s.short_fit.rate-truth)<=1);
 assert(r2_hold_abs((int64_t)s.long_fit.rate-truth)<=1);
 bool selected=false;int32_t rate=r2_hold_choose(&s,s.last_available,truth+10,100,&selected);
 assert(selected&&r2_hold_abs((int64_t)rate-truth)<=1);
 assert(r2_hold_choose(&s,s.last_available,truth+10,5,&selected)==truth+5&&selected);
 assert(r2_hold_choose(&s,s.last_available,truth+2000,100,&selected)==truth+2000&&!selected);
 assert(r2_hold_choose(&s,s.last_available+3*R2_CAPTURE_HZ,truth+10,100,&selected)==truth+10&&!selected);
 assert(!r2_hold_observe(&s,s.last_tick,s.origin_ns,s.last_available,0));
 assert(!r2_hold_observe(&s,s.last_tick+R2_CAPTURE_HZ,s.origin_ns,s.last_tick,0));
 assert(!r2_hold_observe(&s,s.last_tick+R2_CAPTURE_HZ,s.origin_ns,s.last_available+R2_CAPTURE_HZ,37));
 printf("frequency truth=%d offset=%llu rates=%d/%d guards=%u/%u RAM=%zu\n",truth,(unsigned long long)offset,s.short_fit.rate,s.long_fit.rate,s.short_fit.guard,s.long_fit.guard,sizeof s);
}
int main(void){
 frequency(0,0);frequency(12345,0);frequency(-12345,0);frequency(299000,UINT64_C(4300000000));
 frequency(-299000,UINT64_C(4300000000));
 struct r2_hold_rate cadence={0};unsigned admitted=0;
 for(unsigned i=1;i<=400;i++){
  uint64_t tick=R2_CAPTURE_HZ+(uint64_t)i*3999996;
  uint64_t ns=UINT64_C(1000000000)+(uint64_t)i*250000000+(i%9)*120;
  if(i%17)(void)r2_hold_observe(&cadence,tick,ns,tick+16000,i%9);
  r2_hold_refresh(&cadence,tick+16000);
  if(i>140){bool yes;int32_t chosen=r2_hold_choose(&cadence,tick+16000,-950,100,&yes);if(yes){admitted++;assert(r2_hold_abs((int64_t)chosen+1000)<=1);}}
 }
 assert(admitted>200&&cadence.count<=65);
 struct r2_hold_rate bad={0};bool selected=true;
 assert(r2_hold_choose(&bad,1,42,100,&selected)==42&&!selected);
 assert(r2_hold_observe(&bad,16000000,1000000000,16000001,0));
 assert(!r2_hold_observe(&bad,UINT64_MAX,INT64_MAX,UINT64_MAX,0));
 struct rc5_map map={.effective=16000000,.anchor_tick=16000000,.anchor_ns=1000000000,.ppb=20,.phase_ns=-200,.generation=1};
 struct rc5_map frozen;uint64_t a,b,e;
 assert(r2_hold_freeze(&map,24000000,15,&frozen));
 assert(rc5_map_project(&map,24000000,&a,&e)&&rc5_map_project(&frozen,24000000,&b,&e)&&a==b);
 struct r2_hold_trial t={.baseline={map.anchor_ns,map.ppb,map.phase_ns},.start=16000000,.selected=0,.probe_ms=120000,.active=true};
 for(unsigned i=1;i<=1920;i++){
  uint64_t tick=16000000+(uint64_t)i*1000000;
  assert(r2_hold_trial_score(&t,tick,tick*125/2));
 }
 assert(t.max_ns[1]<1000&&t.max_ns[0]>=1000&&t.cross_ms[0]&&!t.cross_ms[1]);
 uint32_t retained=t.max_ns[0];assert(!r2_hold_trial_score(&t,t.last,0));assert(t.max_ns[0]==retained);
 t=(struct r2_hold_trial){.baseline={.anchor_ns=1000000000},.start=1,.probe_ms=2000,.active=true};
 assert(r2_hold_trial_score(&t,1600001,1100001500));assert(t.max_ns[0]>=1000);
 assert(r2_hold_trial_score(&t,3200001,1200000000));assert(t.max_ns[0]>=1000&&t.cross_ms[0]);
 assert(r2_hold_trial_score(&t,16000001,2000000000));assert(t.gaps==1);
 assert(!r2_hold_trial_value(&t,1+R2_HOLD_MAX_TICKS+R2_CAPTURE_HZ+1,0,&a));
 for(int32_t correction=-100000;correction<=100000;correction+=10000){
  assert(r2_hold_from_lora(16000000,1000000000,correction,&frozen));
  assert(rc5_map_project(&frozen,16000000+120*R2_CAPTURE_HZ,&a,&e));
  int64_t expect=INT64_C(121000000000)+INT64_C(120)*correction;
  assert(r2_hold_abs((int64_t)a-expect)<=121);
 }
 assert(!r2_hold_from_lora(1,1,100001,&frozen));
 puts("HOLD causal rates, channel bias, wrap, limits, frozen isolation and prefix tails PASS");
}
