/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Bounded causal frequency selection and frozen-clock experiments. No hardware,
 * allocation, temperature input or source permission. RC5 rate convention:
 * positive ppb means local nominal ticks advance faster than remote time.
 * LoRa adapters must convert their opposite, correction-rate convention.
 */
#pragma once
#include "rc_boundary.h"
#include "precision_servo.h"
#include <limits.h>
#define R2_HOLD_POINTS 65u
#define R2_HOLD_MAX_TICKS (UINT64_C(120)*R2_CAPTURE_HZ)
struct r2_hold_point {uint32_t tick;int32_t residual;};
struct r2_hold_fit {int32_t rate;uint32_t guard,span_ms;uint16_t points,pairs;};
struct r2_hold_rate {
 struct r2_hold_point point[R2_HOLD_POINTS];uint8_t channel[R2_HOLD_POINTS];
 uint64_t origin_tick,origin_ns,last_tick,last_available,refreshed;
 struct r2_hold_fit short_fit,long_fit;unsigned count;
};
struct r2_hold_trial {
 struct rc5_map baseline;uint64_t start,last;
 uint32_t id,probe_ms,max_ns[2],cross_ms[2],witnesses,gaps;
 int32_t selected;bool active;
};
_Static_assert(sizeof(struct r2_hold_rate)<=768,"HOLD rate RAM budget");
_Static_assert(2*sizeof(struct r2_hold_trial)<=256,"HOLD shadow RAM budget");
static inline uint64_t r2_hold_abs(int64_t n){return n<0?(uint64_t)(-(n+1))+1:(uint64_t)n;}
static inline uint64_t r2_hold_point_tick(const struct r2_hold_rate *s,unsigned i){
 return s->last_tick-(uint32_t)((uint32_t)s->last_tick-s->point[i].tick);
}
static inline bool r2_hold_observe(struct r2_hold_rate *s,uint64_t tick,uint64_t ns,uint64_t available,uint8_t channel){
 if(!s||!tick||ns>INT64_MAX||available<tick||available-tick>R2_CAPTURE_HZ/4||channel>36||
    (s->last_tick&&(tick<=s->last_tick||available<s->last_available)))return false;
 /*450ms avoids selecting only every third250ms event on a slow oscillator;
  *65 points still cover>=28.8s, above the32s fit's85% span gate. */
 if(s->last_tick&&tick-s->last_tick<R2_CAPTURE_HZ*45/100)return false;
 if(!s->count){s->origin_tick=tick;s->origin_ns=ns;}
 if(tick<s->origin_tick||tick-s->origin_tick>(uint64_t)INT64_MAX/125||ns<s->origin_ns)return false;
 int64_t residual=(int64_t)((tick-s->origin_tick)*125/2)-(int64_t)(ns-s->origin_ns);
 if(residual<INT32_MIN||residual>INT32_MAX){*s=(struct r2_hold_rate){0};return false;}
 while(s->count&&(tick-r2_hold_point_tick(s,0)>32*R2_CAPTURE_HZ||s->count==R2_HOLD_POINTS)){
  --s->count;memmove(s->point,s->point+1,s->count*sizeof s->point[0]);memmove(s->channel,s->channel+1,s->count);
 }
 if(tick-s->origin_tick>32*R2_CAPTURE_HZ){
  for(unsigned i=0;i<s->count;i++){
   int64_t y=(int64_t)s->point[i].residual-residual;
   if(y<INT32_MIN||y>INT32_MAX){*s=(struct r2_hold_rate){0};return false;}
   s->point[i].residual=(int32_t)y;
  }
  s->origin_tick=tick;s->origin_ns=ns;residual=0;
 }
 s->point[s->count]=(struct r2_hold_point){(uint32_t)tick,(int32_t)residual};
 s->channel[s->count++]=channel;s->last_tick=tick;s->last_available=available;return true;
}
static inline struct r2_hold_fit r2_hold_fit_window(const struct r2_hold_rate *s,uint64_t at,unsigned seconds){
 struct r2_hold_fit f={.guard=UINT32_MAX};
 unsigned first=0;while(first<s->count&&(at<r2_hold_point_tick(s,first)||at-r2_hold_point_tick(s,first)>seconds*R2_CAPTURE_HZ))++first;
 if(first==s->count||at<s->last_available)return f;
 uint64_t origin=r2_hold_point_tick(s,first);int32_t base=s->point[first].residual;
 f.points=(uint16_t)(s->count-first);f.span_ms=(uint32_t)((s->last_tick-origin)/16000);
 if(f.points<32||f.span_ms<seconds*850)return f;
 for(unsigned i=first;i<s->count;i++)if(r2_hold_abs((int64_t)s->point[i].residual-base)>16000000)return f;
 int64_t xx=0,xy=0;
 /* Two bounded passes per channel avoid W12's large per-channel work arrays.
  * x is remote microseconds, y local-minus-remote ns. All centering is causal. */
 for(unsigned ch=0;ch<37;ch++){
  int64_t sx=0,sy=0;unsigned n=0;
  for(unsigned i=first;i<s->count;i++)if(s->channel[i]==ch){
   int64_t y=(int64_t)s->point[i].residual-base;
   int64_t x=((int64_t)((r2_hold_point_tick(s,i)-origin)*125/2)-y)/1000;
   sx+=x;sy+=y;++n;
  }
  if(n<2)continue;
  f.pairs+=(uint16_t)(n-1);sx/=n;sy/=n;
  for(unsigned i=first;i<s->count;i++)if(s->channel[i]==ch){
   int64_t y=(int64_t)s->point[i].residual-base;
   int64_t x=((int64_t)((r2_hold_point_tick(s,i)-origin)*125/2)-y)/1000-sx;y-=sy;
   xx+=x*x;xy+=x*y;
  }
 }
 if(f.pairs<8||xx<=0)return f;
 int64_t rate=r2_servo_scaled_ratio(xy,1000000,(uint64_t)xx);
 if(rate< -300000||rate>300000)return f;
 f.rate=(int32_t)rate;uint64_t squares=0;
 for(unsigned ch=0;ch<37;ch++){
  int64_t sx=0,sy=0;unsigned n=0;
  for(unsigned i=first;i<s->count;i++)if(s->channel[i]==ch){
   int64_t y=(int64_t)s->point[i].residual-base;
   sx+=((int64_t)((r2_hold_point_tick(s,i)-origin)*125/2)-y)/1000;sy+=y;++n;
  }
  if(n<2)continue;
  sx/=n;sy/=n;
  for(unsigned i=first;i<s->count;i++)if(s->channel[i]==ch){
   int64_t y=(int64_t)s->point[i].residual-base;
   int64_t x=((int64_t)((r2_hold_point_tick(s,i)-origin)*125/2)-y)/1000-sx;
   int64_t e=y-sy-x*rate/1000000;uint64_t a=r2_hold_abs(e);
   if(a>1000000||squares>UINT64_MAX-a*a)return (struct r2_hold_fit){.guard=UINT32_MAX};
   squares+=a*a;
  }
 }
 uint64_t rms=(uint64_t)r2_servo_rms_floor(squares,f.pairs-1)+1;
 uint64_t root=r2_servo_rms_floor((uint64_t)xx,1);
 if(!root)return (struct r2_hold_fit){.guard=UINT32_MAX};
 uint64_t guard=(3*rms*1000000+root-1)/root,quant=(62500+f.span_ms-1)/f.span_ms;
 if(guard<quant)guard=quant;
 f.guard=guard<UINT32_MAX?(uint32_t)guard:UINT32_MAX;return f;
}
static inline void r2_hold_refresh(struct r2_hold_rate *s,uint64_t at){
 if(!s->last_available||at<s->last_available||(s->refreshed&&at-s->refreshed<R2_CAPTURE_HZ))return;
 s->short_fit=r2_hold_fit_window(s,at,16);s->long_fit=r2_hold_fit_window(s,at,32);s->refreshed=at;
}
static inline int32_t r2_hold_choose(const struct r2_hold_rate *s,uint64_t at,int32_t fast,uint32_t fast_guard,bool *selected){
 *selected=false;
 if(!s->refreshed||at<s->refreshed||at-s->refreshed>2*R2_CAPTURE_HZ||at<s->last_tick||
    at-s->last_tick>2*R2_CAPTURE_HZ||s->short_fit.guard>10||s->long_fit.guard>10||
    s->long_fit.guard>=fast_guard||r2_hold_abs((int64_t)s->short_fit.rate-s->long_fit.rate)>5)return fast;
 int32_t a=fast,b=s->short_fit.rate,c=s->long_fit.rate,t;
 if(a>b){t=a;a=b;b=t;}if(b>c){t=b;b=c;c=t;}if(a>b){t=a;a=b;b=t;}
 int64_t delta=(int64_t)b-fast;if(r2_hold_abs(delta)>15)return fast;
 if(delta>5)delta=5;
 if(delta< -5)delta=-5;
 *selected=true;return fast+(int32_t)delta;
}
/* Reanchor an exact RC5 map at a causal entry. Shadow and production call the
 * same function. The unconsumed phase correction remains explicit. */
static inline bool r2_hold_freeze(const struct rc5_map *in,uint64_t at,int32_t rate,struct rc5_map *out){
 uint64_t ns,elapsed;if(!in||rate< -300000||rate>300000||!rc5_map_project(in,at,&ns,&elapsed)||ns>INT64_MAX)return false;
 *out=*in;out->effective=out->anchor_tick=at;out->anchor_ns=ns;out->ppb=rate;
 out->phase_ns=elapsed>=1000000000?0:(int32_t)((int64_t)in->phase_ns*(int64_t)(1000000000-elapsed)/1000000000);
 out->bound=out->rate_bound=out->capture_bound=RC5_UNKNOWN;return true;
}
/* LoRa expresses the correction multiplier instead of local frequency error.
 * Preserve its independently authenticated phase; this adapter adds no lease. */
static inline bool r2_hold_from_lora(uint64_t tick,uint64_t ns,int32_t correction_ppb,struct rc5_map *out){
 if(!out||!tick||ns>INT64_MAX||correction_ppb< -100000||correction_ppb>100000)return false;
 int64_t inverse=-(int64_t)correction_ppb*INT64_C(1000000000)/(INT64_C(1000000000)+correction_ppb);
 *out=(struct rc5_map){.effective=tick,.anchor_tick=tick,.anchor_ns=ns,.ppb=(int32_t)inverse,
  .bound=RC5_UNKNOWN,.rate_bound=RC5_UNKNOWN,.capture_bound=RC5_UNKNOWN};return true;
}
static inline bool r2_hold_trial_value(const struct r2_hold_trial *t,uint64_t tick,unsigned candidate,uint64_t *ns){
 struct rc5_map m=t->baseline;uint64_t elapsed;
 if(candidate&&!r2_hold_freeze(&t->baseline,t->start,t->selected,&m))return false;
 /* The final real witness may overshoot a120s probe by up to its1s expiry. */
 return tick>=t->start&&tick-t->start<=R2_HOLD_MAX_TICKS+R2_CAPTURE_HZ&&rc5_map_project(&m,tick,ns,&elapsed);
}
static inline bool r2_hold_trial_score(struct r2_hold_trial *t,uint64_t tick,uint64_t remote){
 if(!t->active||tick<t->start||(t->last&&tick<=t->last))return false;
 if(tick-(t->last?t->last:t->start)>R2_CAPTURE_HZ/4)++t->gaps;
 uint32_t age=(uint32_t)((tick-t->start)/16000);
 for(unsigned k=0;k<2;k++){
  uint64_t ns;if(!r2_hold_trial_value(t,tick,k,&ns))return false;
  uint64_t e=ns>remote?ns-remote:remote-ns;uint32_t bounded=e<UINT32_MAX?(uint32_t)e:UINT32_MAX;
  if(bounded>t->max_ns[k])t->max_ns[k]=bounded;
  if(e>=1000&&!t->cross_ms[k])t->cross_ms[k]=age+1;
 }
 t->last=tick;++t->witnesses;return true;
}
