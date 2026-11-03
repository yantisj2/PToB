/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
/* Offline candidate. Phase/rate/drift start from packet data and zero priors,
 * never GNSS. Integer anchors stay outside this state. Float storage bounds
 * covariance memory; prediction and Joseph updates use double intermediates.
 * R/Q are declared engineering models, not calibrated error bounds. */
#define LP_DRIFT_MEASUREMENT_NS 4200u
#define LP_DRIFT_RATE_RW_PPB2_S 0.01
#define LP_DRIFT_DRIFT_RW_PPB2_S3 0.00000001
struct lp_clock_drift {
 double phase,rate,drift;
 float p00,p01,p02,p11,p12,p22;
};
_Static_assert(sizeof(struct lp_clock_drift)==48,"Sparse drift candidate storage");
static inline uint32_t lp_clock_drift_sd(double variance){
 if(!(variance>=0)||variance>UINT32_MAX*(double)UINT32_MAX)return UINT32_MAX;
 uint32_t lo=0,hi=UINT32_MAX;
 while(lo<hi){uint32_t mid=lo+(uint32_t)(((uint64_t)hi-lo+1)/2);
  if((double)mid*mid<=variance)lo=mid;else hi=mid-1;}
 return lo==UINT32_MAX?lo:lo+1;
}
static inline void lp_clock_drift_init(struct lp_clock_drift *s){
 *s=(struct lp_clock_drift){.p00=(float)LP_DRIFT_MEASUREMENT_NS*LP_DRIFT_MEASUREMENT_NS,.p11=1e10f,.p22=100.f};
}
static inline bool lp_clock_drift_step(struct lp_clock_drift *s,uint64_t ticks,uint64_t ns){
 if(!s||!ticks||ticks>UINT64_C(300000000)*16||ticks>UINT64_MAX/125||ns>INT64_MAX)return false;
 uint64_t nominal=ticks*125/2;if(nominal>INT64_MAX)return false;
 double t=(double)ticks/16000000.,t2=t*t,t3=t2*t,t4=t3*t;
 double qr=LP_DRIFT_RATE_RW_PPB2_S,qj=LP_DRIFT_DRIFT_RW_PPB2_S3;
 double a=s->p00+2*t*s->p01+t2*((double)s->p11+s->p02)+t3*s->p12+.25*t4*s->p22+qr*t3/3+qj*t4*t/20;
 double b=s->p01+t*((double)s->p11+s->p02)+1.5*t2*s->p12+.5*t3*s->p22+qr*t2/2+qj*t4/8;
 double c=s->p02+t*s->p12+.5*t2*s->p22+qj*t3/6;
 double d=s->p11+2*t*s->p12+t2*s->p22+qr*t+qj*t3/3;
 double e=s->p12+t*s->p22+qj*t2/2,f=s->p22+qj*t;
 double R=(double)LP_DRIFT_MEASUREMENT_NS*LP_DRIFT_MEASUREMENT_NS,S=a+R;
 double k0=a/S,k1=b/S,k2=c/S,one=1-k0;
 double residual=s->phase+t*s->rate+.5*t2*s->drift-(double)((int64_t)ns-(int64_t)nominal);
 struct lp_clock_drift next={.phase=one*residual,.rate=s->rate+t*s->drift-k1*residual,.drift=s->drift-k2*residual,
  .p00=(float)(one*one*a+k0*k0*R),.p01=(float)(one*(b-k1*a)+k0*k1*R),.p02=(float)(one*(c-k2*a)+k0*k2*R),
  .p11=(float)(d-2*k1*b+k1*k1*S),.p12=(float)(e-k1*c-k2*b+k1*k2*S),.p22=(float)(f-2*k2*c+k2*k2*S)};
 if(next.rate>100000&&next.rate<=100000.001)next.rate=100000;
 if(next.rate< -100000&&next.rate>=-100000.001)next.rate=-100000;
 if(!(next.phase>=-1000000&&next.phase<=1000000&&next.rate>=-100000&&next.rate<=100000&&next.drift>=-1000&&next.drift<=1000&&
  next.p00>=0&&next.p00<=1e18&&next.p11>=0&&next.p11<=1e10&&next.p22>=0&&next.p22<=101&&
  next.p01>=-1e14&&next.p01<=1e14&&next.p02>=-1e12&&next.p02<=1e12&&next.p12>=-1e12&&next.p12<=1e12))return false;
 *s=next;return true;
}
