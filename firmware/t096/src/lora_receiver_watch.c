/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_receiver_watch.h"
#include "holdover.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#if defined(R2_LORA_PASSIVE_SLOW) && defined(R2_LORA_FIXED500_EXPERIMENT)

static void inc(uint32_t *n){if(*n<UINT32_MAX)++*n;}
static uint32_t rms(double square,uint32_t n){
 if(!n)return UINT32_MAX;
 const double value=sqrt(square/n);if(!(value<UINT32_MAX))return UINT32_MAX;
 const uint32_t floor=(uint32_t)value;return floor+(value>floor);
}
uint32_t lp_receiver_watch_age(uint64_t now,uint64_t event){
 if(!event||now<event)return UINT32_MAX;
 const uint64_t ms=(now-event)/1000;return ms>UINT32_MAX?UINT32_MAX:(uint32_t)ms;
}
void lp_receiver_watch_fence(struct lp_receiver_watch *w,uint8_t reason){
 if(!w)return;
 w->admitted=false;w->reason=reason;w->rx.pending=w->rx.hint=w->rx.prediction=false;
 w->rx.have_pair=w->rx.trained=w->rx.display_valid=w->rx.display_acquiring=false;
 w->rx.fit_count=0;w->rx.model_accepted=0;w->rx.physical_deadline_us=0;
 w->predictions=w->recent_count=w->recent_head=0;w->error_us=0;
 /* Keep authenticated descriptive META/TZ and replay floors for this boot. */
}
bool lp_receiver_watch_meta(struct lp_receiver_watch *w,const struct lp_packet *p,const struct r2_group_config *g,uint64_t epoch,uint64_t event,uint64_t now){
 if(!w||!p||!g||p->kind!=LP_META)return false;
 uint8_t context[16];if(!epoch||lp_context(context,&p->scope,p->schedule,g))return false;
 const bool same=w->rx.cached&&!memcmp(context,w->rx.context,16);
 if(w->admitted&&w->admission_epoch!=epoch)lp_receiver_watch_fence(w,4);
 if(!lp_rx_meta(&w->rx,p,g,NULL,event,now))return false;
 if(!same){
  w->square=0;w->total_predictions=w->maximum=w->over1us=0;w->last_error=0;w->last_clipped=false;
  lp_receiver_watch_fence(w,2);
 }
 if(!p->time_valid){lp_receiver_watch_fence(w,3);return true;}
 w->admission_epoch=epoch;w->admitted=true;w->reason=0;return true;
}
bool lp_receiver_watch_sync(struct lp_receiver_watch *w,uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t event,uint64_t now){
 if(!w)return false;
 if(!w->admitted||epoch!=w->admission_epoch){w->rx.pending=false;return false;}
 return lp_rx_watch_sync(&w->rx,cursor,tick,epoch,event,now);
}
static void record(struct lp_receiver_watch *w,uint64_t actual,uint64_t predicted,uint64_t event){
 const uint64_t magnitude=actual>=predicted?actual-predicted:predicted-actual;
 const uint32_t value=magnitude>UINT32_MAX?UINT32_MAX:(uint32_t)magnitude;
 if(w->total_predictions==UINT32_MAX)return;
 w->recent[w->recent_head]=value;w->recent_head=(w->recent_head+1)%8;
 if(w->recent_count<8)++w->recent_count;
 inc(&w->predictions);inc(&w->total_predictions);w->square+=(double)value*value;
 if(value>w->maximum)w->maximum=value;
 if(value>=1000)inc(&w->over1us);
 const int32_t signed_value=value>INT32_MAX?INT32_MAX:(int32_t)value;
 w->last_error=actual>=predicted?signed_value:-signed_value;w->last_clipped=magnitude>INT32_MAX;w->error_us=event;
 double recent=0;for(unsigned i=0;i<w->recent_count;i++)recent+=(double)w->recent[i]*w->recent[i];
 w->recent_rms=rms(recent,w->recent_count);w->total_rms=rms(w->square,w->total_predictions);
}
bool lp_receiver_watch_follow(struct lp_receiver_watch *w,const struct lp_packet *p,uint64_t epoch,uint64_t now,bool delivered_known,uint64_t delivered_ns){
 if(!w||!p)return false;
 struct lp_rx *r=&w->rx;
 if(!w->admitted||epoch!=w->admission_epoch)return false;
 uint64_t predicted=delivered_ns;
 const bool known=delivered_known||(r->pending&&lp_rx_display_estimate(r,r->pending_tick,epoch,r->pending_us,&predicted));
 if(!lp_rx_watch_follow(r,p,epoch,now))return false;
 if(known)record(w,r->last_tx_ns,predicted,r->last_us);
 return true;
}
/* Early local rate learning has no training/precision permission. Its measured
 * pre-update errors, not a two-point fit's zero residual, supply the UI metric. */
static bool __attribute__((noinline)) early_commit(struct lp_rx *r,uint64_t tick,uint64_t epoch,uint64_t now){
 if(!r->fit_count||epoch!=r->last_epoch||tick<r->last_tick||now<r->last_us||now>=r->physical_deadline_us)return false;
 double rate=r->display_valid?r->display_rate_ppb:0,offset=0;
 double sx=0,sy=0,sxx=0,sxy=0;const unsigned n=r->fit_count;
 if(n>1){for(unsigned i=0;i<n;i++){
  const double x=(double)(r->fit[i].tick-r->fit[0].tick)/16.0;
  const double y=(double)(r->fit[i].tx_ns-r->fit[0].tx_ns)-x*1000.0;
  sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
 }
 const double denominator=n*sxx-sx*sx;if(!(denominator>0))return false;
 const double slope=(n*sxy-sx*sy)/denominator;rate=slope*1000000.0;
 const double last_x=(double)(r->last_tick-r->fit[0].tick)/16.0;
 offset=(sy-slope*sx)/n+slope*last_x-((double)(r->last_tx_ns-r->fit[0].tx_ns)-last_x*1000.0);}
 if(!isfinite(rate)||!isfinite(offset)||fabs(rate)>100000||fabs(offset)>1000000)return false;
 const int32_t ppb=(int32_t)llround(rate),correction=(int32_t)llround(offset);
 uint64_t phase=r->last_tx_ns,target,previous;
 if(correction<0){if(phase<(uint64_t)-(int64_t)correction)return false;phase-=(uint64_t)-(int64_t)correction;}
 else{if(phase>UINT64_MAX-(uint32_t)correction)return false;phase+=(uint32_t)correction;}
 if(!r2_hold_lora_project(tick,r->last_tick,phase,ppb,0,&target))return false;
 int32_t slew=0;
 if(r->display_valid&&r->display_epoch==epoch&&r2_hold_lora_project(tick,r->display_tick,r->display_ns,r->display_rate_ppb,r->display_phase_ns,&previous)){
  const uint64_t magnitude=target>=previous?target-previous:previous-target;
  if(magnitude<=1000000){slew=target>=previous?(int32_t)magnitude:-(int32_t)magnitude;target=previous;}
 }
 r->display_tick=tick;r->display_ns=target;r->display_rate_ppb=ppb;r->display_phase_ns=slew;
 r->display_epoch=epoch;r->display_us=r->last_us;r->display_valid=r->display_acquiring=true;
 r->model_accepted=r->accepted;return true;
}
bool lp_receiver_watch_time(struct lp_receiver_watch *w,uint64_t tick,uint64_t epoch,uint64_t now,uint64_t *ns){
 if(!w||!ns)return false;
 struct lp_rx *r=&w->rx;
 if(!w->admitted)return false;
 if(epoch!=w->admission_epoch||!tick||(r->last_tick&&(tick<r->last_tick||now<r->last_us))||(r->display_valid&&(tick<r->display_tick||now<r->display_us))){lp_receiver_watch_fence(w,4);return false;}
 if(r->pending&&now>=r->pending_deadline_us){r->pending=false;inc(&r->missing);}
 if(r->have_pair&&now>=r->physical_deadline_us)r->have_pair=r->trained=false;
 if(r->model_accepted!=r->accepted){
  if(r->trained)(void)lp_rx_discipline(r,tick,epoch,now);
  else (void)early_commit(r,tick,epoch,now);
 }
 return lp_rx_display_estimate(r,tick,epoch,now,ns);
}
uint32_t lp_receiver_watch_rms(const struct lp_receiver_watch *w){
 return w&&w->recent_count?w->recent_rms:UINT32_MAX;
}
uint32_t lp_receiver_watch_total_rms(const struct lp_receiver_watch *w){
 return w&&w->total_predictions?w->total_rms:UINT32_MAX;
}
#endif
