/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
/* Engineering noise assumptions, not calibrated uncertainty or source bounds.
 * State is phase relative to the most recent sampled physical pair, and the
 * correction multiplier in ns/s (ppb). UTC and counter anchors stay integer.
 * No PPS, BLE observation, allocation or transport permission enters here. */
#define LP_SERVO_MEASUREMENT_NS 4200u
#define LP_SERVO_RATE_WANDER_PPB2_S 0.01
#ifdef R2_LORA_PASSIVE_SLOW
#define LP_CLOCK_FILTER_NAME "RECURSIVE_PHASE_RATE_V1"
#else
#define LP_CLOCK_FILTER_NAME "LEGACY_RAW_ANCHOR"
#endif
struct lp_clock_servo {double phase,rate,p00,p01,p11;};
_Static_assert(sizeof(struct lp_clock_servo)==40,"Sparse servo state budget");
static inline void lp_clock_servo_init(struct lp_clock_servo *s){
 *s=(struct lp_clock_servo){.p00=(double)LP_SERVO_MEASUREMENT_NS*LP_SERVO_MEASUREMENT_NS,.p11=1e10};
}
static inline bool lp_clock_servo_step(struct lp_clock_servo *s,uint64_t ticks,uint64_t ns){
 if(!s||!ticks||ticks>UINT64_C(300000000)*16||ticks>UINT64_MAX/125||ns>INT64_MAX)return false;
 uint64_t nominal=ticks*125/2;
 if(nominal>INT64_MAX)return false;
 double dt=(double)ticks/16000000.0,q=LP_SERVO_RATE_WANDER_PPB2_S;
 double measured=(double)((int64_t)ns-(int64_t)nominal);
 double phase=s->phase+dt*s->rate-measured;
 double a=s->p00+2*dt*s->p01+dt*dt*s->p11+q*dt*dt*dt/3;
 double b=s->p01+dt*s->p11+q*dt*dt/2,c=s->p11+q*dt;
 double noise=(double)LP_SERVO_MEASUREMENT_NS*LP_SERVO_MEASUREMENT_NS;
 double total=a+noise,k0=a/total,k1=b/total,one=1-k0;
 /* Joseph form preserves covariance under the broad initial rate prior. */
 struct lp_clock_servo next={.phase=one*phase,.rate=s->rate-k1*phase,
  .p00=one*one*a+k0*k0*noise,
  .p01=one*(b-k1*a)+k0*k1*noise,
  .p11=c-2*k1*b+k1*k1*total};
 /* Resolve sub-millippb rounding at the inclusive100ppm domain edge before
  * checking the same physical rate limit; this cannot admit a larger model. */
 if(next.rate>100000&&next.rate<=100000.001)next.rate=100000;
 if(next.rate< -100000&&next.rate>=-100000.001)next.rate=-100000;
 if(!(next.phase>=-1000000&&next.phase<=1000000&&next.rate>=-100000&&next.rate<=100000&&
      next.p00>=0&&next.p00<=1e18&&next.p11>=0&&next.p11<=1e10&&
      next.p01>=-1e14&&next.p01<=1e14))return false;
 *s=next;return true;
}
static inline uint32_t lp_clock_servo_sd(double variance){
 if(!(variance>=0)||variance>UINT32_MAX*(double)UINT32_MAX)return UINT32_MAX;
 uint32_t lo=0,hi=UINT32_MAX;
 while(lo<hi){uint32_t mid=lo+(uint32_t)(((uint64_t)hi-lo+1)/2);
  if((double)mid*mid<=variance)lo=mid;else hi=mid-1;}
 return lo==UINT32_MAX?lo:lo+1;
}
