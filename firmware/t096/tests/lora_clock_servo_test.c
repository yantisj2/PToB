/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_clock_servo.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
int main(void){
 struct lp_clock_servo s;uint64_t tick,ns,last_tick=0,last_ns=0;unsigned n=0;
 while(scanf("%"SCNu64" %"SCNu64,&tick,&ns)==2){
  if(!n)lp_clock_servo_init(&s);
  else assert(tick>last_tick&&ns>last_ns&&lp_clock_servo_step(&s,tick-last_tick,ns-last_ns));
  printf("%.17g %.17g %.17g %.17g %.17g\n",s.phase,s.rate,s.p00,s.p01,s.p11);
  last_tick=tick;last_ns=ns;++n;
 }
 assert(n);
 assert(!lp_clock_servo_step(&s,0,1));
 assert(!lp_clock_servo_step(&s,UINT64_MAX,1));
 assert(!lp_clock_servo_step(&s,1,UINT64_MAX));
 return 0;
}
