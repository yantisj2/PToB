/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_clock_drift.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
int main(void){
 struct lp_clock_drift s;uint64_t tick,ns,last_tick=0,last_ns=0;unsigned n=0;
 while(scanf("%"SCNu64" %"SCNu64,&tick,&ns)==2){
  if(!n)lp_clock_drift_init(&s);
  else assert(tick>last_tick&&ns>last_ns&&lp_clock_drift_step(&s,tick-last_tick,ns-last_ns));
  printf("%.17g %.17g %.17g %.9g %.9g %.9g %.9g %.9g %.9g\n",s.phase,s.rate,s.drift,s.p00,s.p01,s.p02,s.p11,s.p12,s.p22);
  last_tick=tick;last_ns=ns;++n;
 }
 assert(n&&!lp_clock_drift_step(&s,0,1)&&!lp_clock_drift_step(&s,UINT64_MAX,1)&&!lp_clock_drift_step(&s,1,UINT64_MAX));
}
