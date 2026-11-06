// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Passive wire3/schedule8 receiver. No BLE model, transmitter or clock authority.
#include "lora_core.h"
#include "lora_passive_core.h"
#include "lora_thermal.h"
#include "holdover.h"
#include <math.h>
#include <string.h>
static struct lp_rx receiver;
static struct lp_replay replay;
static struct r2_group_config group;
static uint64_t transmitter,current_tick,current_epoch,current_us,meta_us;
static uint32_t generation,metadata,rejected,replayed;
static bool enabled,time_valid;
static struct gm_clock thermal;
static uint64_t qualified_first_tick;static uint32_t qualified_points;
// The raw shared linear fitter remains unmodified and visible. This N30-only
// delivered model follows the independent XTAL's warmup acceleration. Neither
// covariance nor prediction agreement is an RF/UTC uncertainty bound.
// Validated |rate|<=100000ppb, |accel|<=1000ppb/s and projection<=120s
// keep both fixed-point coefficients inside int32 (rate derivative<220M).
struct delivered {uint64_t tick,ns,epoch,physical,physical_us;int32_t rate,accel,fast_accel,phase;bool valid;};
static struct delivered model,held_model,history[4];
struct forecast {double square;uint32_t recent[8],recent_count,recent_head,count,maximum,over;int32_t last;};
static struct forecast prediction,frozen60,hold60;
static uint64_t hold_id,hold_start,hold_until,hold_grant;static uint32_t hold_runs;
static uint8_t hold_state,hold_abort; //0none/1active/2complete/3aborted; reason1model/2context/3source/4capture/5OFF/6expiry/7radiofault
static void inc(uint32_t *n){if(*n<UINT32_MAX)++*n;}
static void clear_model(void){
 qualified_first_tick=0;qualified_points=0;memset(&model,0,sizeof model);memset(history,0,sizeof history);
 if(hold_state==1){hold_state=3;hold_abort=1;}
 prediction.recent_count=prediction.recent_head=frozen60.recent_count=frozen60.recent_head=0;
}
static void watch_fence(uint8_t reason);
void n30_rc8_radio_fault(void){watch_fence(7);if(hold_state==1){clear_model();hold_abort=7;}}
static void clear_context(void){clear_model();memset(&prediction,0,sizeof prediction);memset(&frozen60,0,sizeof frozen60);}
static void expire(uint64_t now){
 if(hold_state==1&&now>=hold_until)hold_state=2;
 lp_rx_expire(&receiver,now);
 if(!receiver.have_pair){clear_model();if(hold_state==3&&hold_abort==1)hold_abort=6;}
}
static bool project_math(const struct delivered* m,uint64_t tick,uint64_t epoch,uint64_t* ns){
 if(!m->valid||m->epoch!=epoch||tick<m->tick||tick<m->physical)return false;
 const uint64_t ticks=tick-m->tick,nominal=ticks*125/2;const double dt=(double)ticks/16000000.0;
 const double h=120.0*(dt-120.0*(1.0-n30_lora_decay(dt)));
 const double hf=30.0*(dt-30.0*(1.0-n30_lora_decay_fast(dt)));
 const double correction=(double)m->rate*.001*dt+(double)m->accel*.000001*h+(double)m->fast_accel*.000001*hf+(double)m->phase*(dt<1.0?dt:1.0);
 if(!isfinite(correction)||fabs(correction)>1e9)return false;
 const int64_t change=llround(correction);uint64_t elapsed;
 if(change<0){if(nominal<(uint64_t)-change)return false;elapsed=nominal-(uint64_t)-change;}
 else{if(nominal>UINT64_MAX-(uint64_t)change)return false;elapsed=nominal+(uint64_t)change;}
 if(m->ns>UINT64_MAX-elapsed)return false;
 *ns=m->ns+elapsed;return true;
}
static bool project(const struct delivered* m,uint64_t tick,uint64_t epoch,uint64_t* ns){
 if(tick<m->physical||tick-m->physical>LP_HOLD_US*16)return false;
 return project_math(m,tick,epoch,ns);
}
static void record(struct forecast* f,uint64_t predicted,uint64_t actual){
 const uint64_t magnitude=predicted>=actual?predicted-actual:actual-predicted;
 // Saturated errors remain failures; the original physical pair is not filtered.
 const uint32_t value=magnitude>UINT32_MAX?UINT32_MAX:(uint32_t)magnitude;
 if(f->count==UINT32_MAX)return;
 f->recent[f->recent_head]=value;f->recent_head=(f->recent_head+1)%8;
 if(f->recent_count<8)++f->recent_count;
 ++f->count;f->square+=(double)value*value;if(value>f->maximum)f->maximum=value;
 if(value>=1000)inc(&f->over);
 const int32_t signed_value=value>INT32_MAX?INT32_MAX:(int32_t)value;
 f->last=predicted>=actual?signed_value:-signed_value;
}
static uint32_t round_rms(double square,uint32_t n){
 if(!n)return UINT32_MAX;
 const double value=sqrt(square/n);if(!(value<UINT32_MAX))return UINT32_MAX;
 const uint32_t integer=(uint32_t)value;return integer+(value>integer);
}
static uint32_t rms(const struct forecast* f){return round_rms(f->square,f->count);}
static uint32_t recent_rms(const struct forecast* f){unsigned n=f->recent_count;double square=0;for(unsigned i=0;i<n;i++)square+=(double)f->recent[i]*f->recent[i];return round_rms(square,n);}
#include "lora_watch.inc"
static void physical_point(void){
 const uint64_t tick=receiver.last_tick,epoch=receiver.last_epoch;
 if(thermal.points&&(tick<=thermal.tick||tick-thermal.tick>LP_HOLD_US*16||epoch!=current_epoch))clear_model();
 bool got30=false,got60=false;
 for(unsigned i=0;i<4;i++){
  const struct delivered* old=&history[i];uint64_t ns;
  if(!old->valid||tick<old->physical)continue;
  const uint64_t age=tick-old->physical;
  if(!got30&&age>=UINT64_C(25000000)*16&&age<=UINT64_C(40000000)*16&&project(old,tick,epoch,&ns)){record(&prediction,ns,receiver.last_tx_ns);got30=true;}
  if(!got60&&age>=UINT64_C(50000000)*16&&age<=UINT64_C(75000000)*16&&project(old,tick,epoch,&ns)){record(&frozen60,ns,receiver.last_tx_ns);got60=true;}
 }
 if((hold_state==1||hold_state==2)&&receiver.last_us>=hold_start&&receiver.last_us<hold_until&&
    tick>=held_model.physical&&tick-held_model.physical>=UINT64_C(50000000)*16&&
    tick-held_model.physical<=UINT64_C(75000000)*16){uint64_t ns;if(project(&held_model,tick,epoch,&ns))record(&hold60,ns,receiver.last_tx_ns);}
 if(!qualified_first_tick)qualified_first_tick=tick;
 inc(&qualified_points);
}
static bool discipline(uint64_t tick,uint64_t epoch,uint64_t now){
 // Shared estimator is an additional eligibility fence, never replaced by a
 // cached filter. Original META/capture/physical120s/training checks still apply.
 uint64_t original,target,previous;
 if(!lp_rx_estimate(&receiver,tick,epoch,now,&original))return false;
 if(hold_state==1&&now<hold_until)return model.valid;
 if(model.valid&&history[0].physical==receiver.last_tick)return true;
 if(qualified_points<4||receiver.last_tick<qualified_first_tick||receiver.last_tick-qualified_first_tick<UINT64_C(180000000)*16||thermal.tick!=receiver.last_tick||!gm_predict(&thermal,tick,&target))return false;
 int32_t slew=0;
 if(project(&model,tick,epoch,&previous)){
  const uint64_t magnitude=target>=previous?target-previous:previous-target;
  if(magnitude<=1000000){slew=target>=previous?(int32_t)magnitude:-(int32_t)magnitude;target=previous;}
 }
 const double dt=(double)(tick-thermal.tick)/16000000.0,u=n30_lora_decay(dt),g=120.0*(1.0-u),uf=n30_lora_decay_fast(dt),gf=30.0*(1.0-uf);
 model=(struct delivered){.tick=tick,.ns=target,.epoch=epoch,.physical=receiver.last_tick,.physical_us=receiver.last_us,
  .rate=llround((thermal.rate+thermal.accel*g+thermal.fast_accel*gf)*1000.0),.accel=llround(thermal.accel*u*1000000.0),.fast_accel=llround(thermal.fast_accel*uf*1000000.0),.phase=slew,.valid=true};
 memmove(history+1,history,3*sizeof history[0]);history[0]=model;return true;
}
bool n30_rc8_hold60(uint64_t id,uint64_t now){
 uint64_t ignored;
 if(!id||!enabled||!time_valid||!model.valid||!model.physical_us||now<model.physical_us||
    now-model.physical_us>UINT64_C(10000000)||now<current_us||now-current_us>250000||
    hold_state==1||hold_runs==UINT32_MAX||
    now>UINT64_MAX-UINT64_C(60000000)||receiver.physical_deadline_us<=now+UINT64_C(60000000)||
    receiver.observed_deadline_us<=now+UINT64_C(60000000)||
    model.physical!=receiver.last_tick||!lp_rx_estimate(&receiver,current_tick,current_epoch,now,&ignored))return false;
 held_model=model;hold_id=id;hold_start=now;hold_until=now+UINT64_C(60000000);hold_grant=receiver.physical_deadline_us<receiver.observed_deadline_us?receiver.physical_deadline_us:receiver.observed_deadline_us;
 if(hold_grant>model.physical_us+LP_HOLD_US)hold_grant=model.physical_us+LP_HOLD_US;
 hold_state=1;hold_abort=0;++hold_runs;return true;
}
bool n30_rc8_init(uint64_t factory,uint64_t source,uint64_t id,uint32_t epoch,const uint8_t key[32]){
 if(!key||!factory||!source||factory==source)return false;
 group=(struct r2_group_config){.id=id,.epoch=epoch};memcpy(group.psk,key,32);
 if(!r2_group_valid(&group))return false;
 lp_rx_clear(&receiver);clear_context();gm_clear(&thermal);memset(&watch,0,sizeof watch);memset(&watch_description,0,sizeof watch_description);memset(&watch_model,0,sizeof watch_model);memset(&watch_forecast,0,sizeof watch_forecast);memset(&hold60,0,sizeof hold60);hold_id=hold_start=hold_until=hold_grant=0;hold_state=hold_abort=0;hold_runs=0;memset(&replay,0,sizeof replay);transmitter=source;
 generation=metadata=rejected=replayed=0;current_tick=current_epoch=current_us=meta_us=0;enabled=time_valid=false;return true;
}
uint32_t n30_rc8_switch(bool on){
 if(generation==UINT32_MAX){enabled=false;return 0;}
 ++generation;enabled=on;watch_fence(5);lp_rx_clear(&receiver);clear_context();if(!on&&hold_state==3&&hold_abort==1)hold_abort=5;time_valid=false;current_tick=current_epoch=current_us=meta_us=0;return generation;
}
void n30_rc8_clock_sample(uint64_t tick,uint64_t epoch,uint64_t sampled,uint64_t now){
 if(!enabled)return;
 expire(now);
 if(!tick||!epoch||sampled>now||now-sampled>250000||(current_epoch==epoch&&(tick<current_tick||sampled<current_us))){watch_fence(4);lp_rx_clear(&receiver);clear_context();if(hold_state==3&&hold_abort==1)hold_abort=4;time_valid=false;return;}
 if(current_epoch&&epoch!=current_epoch){watch_fence(4);clear_context();if(hold_state==3&&hold_abort==1)hold_abort=4;}
 current_tick=tick;current_epoch=epoch;current_us=sampled;watch_commit(tick,epoch);
 (void)lp_rx_discipline(&receiver,tick,epoch,now);
 (void)discipline(tick,epoch,now);
}
void n30_rc8_clock(uint64_t tick,uint64_t epoch,uint64_t now){n30_rc8_clock_sample(tick,epoch,now,now);}
bool n30_rc8_receive(const uint8_t* wire,size_t size,uint64_t tick,uint64_t epoch,uint64_t local,uint64_t now,uint32_t gen){
 if(!enabled||gen!=generation||!wire||!epoch||!tick||now<local){inc(&rejected);return false;}
 expire(now);struct lp_packet packet;
 if(lp_decode(&packet,wire,size,receiver.cached?receiver.context:NULL,&group)||packet.kind==LP_DISC){inc(&rejected);return false;}
 if(packet.kind==LP_META){
  if(packet.scope.emitter!=transmitter||!lp_replay_admit(&replay,&packet)){inc(&replayed);return false;}
  // No independent UTC age/source-error interval exists on this no-GNSS target.
  // Authenticated observation leases are not calibrated age/accuracy bounds.
  uint8_t old_context[16];memcpy(old_context,receiver.context,16);
  if(!lp_rx_meta(&receiver,&packet,&group,NULL,local,now)){inc(&rejected);return false;}
  if(memcmp(old_context,receiver.context,16)||!packet.time_valid){clear_context();if(hold_state==3&&hold_abort==1)hold_abort=packet.time_valid?2:3;}
  watch_meta(&packet,receiver.context,local);lp_replay_commit(&replay,&packet);time_valid=packet.time_valid;meta_us=local;inc(&metadata);return false;
 }
 if(!receiver.cached||!time_valid){inc(&rejected);return false;}
 if(packet.kind==LP_SYNC){
  (void)watch_sync(packet.cursor,tick,epoch,local,now);const bool ok=lp_rx_sync(&receiver,packet.cursor,tick,epoch,local,now);
  if(!ok)inc(&rejected);
  return false;
 }
 const bool ok=lp_rx_follow(&receiver,&packet,NULL,epoch,now);
 if(!ok)inc(&rejected);
 else physical_point();
 const bool observed=watch_follow(&packet,epoch,now);if(ok&&!observed)clear_model();
 return ok;
}
void n30_rc8_snapshot_sample(struct n30_lora_view* out,uint64_t sampled,uint64_t now){
 if(!out)return;
 expire(now);memset(out,0,sizeof *out);const struct lp_rx* r=&receiver;
 out->tz_id=r->tz.id;out->tz_offset_min=r->tz.offset_min;
 out->sampled_us=sampled;out->generation=generation;out->state=enabled?1:0;
 out->emitter=r->scope.emitter;out->emitter_boot=r->scope.boot;out->root=r->scope.root;out->root_boot=r->scope.root_boot;out->parent=r->scope.parent;out->parent_boot=r->scope.parent_boot;out->stream=r->scope.stream;out->source_epoch=r->scope.epoch;out->kind=r->scope.kind;out->scale=1;
 memcpy(out->context,r->context,16);out->sequence=r->highest;out->corrected_sequence=r->highest;out->accepted=r->accepted;out->rejected=rejected;out->replayed=replayed;out->unmatched=r->missing;out->metadata=metadata;
 out->metadata_known=r->cached&&now<r->observed_deadline_us;out->time_valid=time_valid&&out->metadata_known;
 out->metadata_us=meta_us;out->diagnostic_sequence=r->serial;out->snapshot_age_ms=meta_us&&now>=meta_us?(uint32_t)((now-meta_us)/1000):UINT32_MAX;
 out->parent_bound_ns=out->residual_ns=out->correction_age_ms=out->map_life_ms=UINT32_MAX;
 out->age_ms=UINT32_MAX;out->link_rms_ns=UINT32_MAX;
 out->raw_trained=r->trained;out->trained=r->trained&&gm_ready(&thermal);out->slow_fit=r->hold_selected;
 out->model_valid=model.valid&&r->display_valid&&!r->display_acquiring&&r->model_accepted==r->accepted;
 out->model_kind=1;out->model_tick16=model.tick;out->model_ns=model.ns;out->model_epoch=model.epoch;
 out->model_rate_milli_ppb=model.rate;out->model_accel_micro_ppb_s=(int64_t)model.accel+model.fast_accel;out->model_fast_accel_micro_ppb_s=model.fast_accel;out->model_slow_accel_micro_ppb_s=model.accel;
 out->model_rate_ppb=(int32_t)llround((double)model.rate/1000.0);out->model_phase_ns=model.phase;
 out->phase_ns=model.phase;
 out->model_samples=thermal.points;const uint64_t span=thermal.points?(thermal.tick-thermal.first_tick)/16000:0;out->model_span_ms=span>UINT32_MAX?UINT32_MAX:(uint32_t)span;
 out->prediction_n=prediction.count;out->prediction_rms_ns=rms(&prediction);out->prediction_recent_rms_ns=recent_rms(&prediction);out->prediction_max_ns=prediction.maximum;out->prediction_over1us=prediction.over;out->prediction_last_ns=prediction.last;
 out->frozen60_n=frozen60.count;out->frozen60_rms_ns=rms(&frozen60);out->frozen60_max_ns=frozen60.maximum;out->frozen60_over1us=frozen60.over;
 out->reference_age_ms=out->model_valid&&now>=model.physical_us&&(now-model.physical_us)/1000<=UINT32_MAX?(uint32_t)((now-model.physical_us)/1000):UINT32_MAX;
 out->hold_id=hold_id;out->hold_start_us=hold_start;out->hold_until_us=hold_until;out->hold_grant_until_us=hold_grant;out->hold_active=hold_state==1&&now>=hold_start&&now<hold_until;out->hold_state=hold_state;out->hold_abort=hold_abort;
 out->hold_runs=hold_runs;out->hold60_n=hold60.count;out->hold60_rms_ns=rms(&hold60);out->hold60_max_ns=hold60.maximum;out->hold60_over1us=hold60.over;
 if(r->last_tick){
  out->edge_us=r->last_us;out->edge_tick=r->last_tick;out->capture_epoch=r->last_epoch;out->event_ns=r->last_tx_ns;
  out->age_ms=now>=r->last_us&&(now-r->last_us)/1000<=UINT32_MAX?(uint32_t)((now-r->last_us)/1000):UINT32_MAX;
  out->link_samples=r->fit_count;out->link_rms_ns=r->trained?r->rms_ns:UINT32_MAX;out->rate_ppb=out->model_rate_ppb;
  if(r->fit_count>1)out->fit_span_ms=(uint32_t)((r->fit[r->fit_count-1].tick-r->fit[0].tick)/16000);
  uint64_t ns;
  uint64_t guard;
  if(enabled&&sampled<=now&&now-sampled<=250000&&current_us==sampled&&lp_rx_estimate(r,current_tick,current_epoch,now,&guard)&&project(&model,current_tick,current_epoch,&ns)){
   out->state=2;out->utc_us=ns/1000;out->until_us=r->physical_deadline_us<r->observed_deadline_us?r->physical_deadline_us:r->observed_deadline_us;
   // lp_rx_sync admitted physical_us only with space for LP_HOLD_US.
   if(out->until_us>model.physical_us+LP_HOLD_US)out->until_us=model.physical_us+LP_HOLD_US;
   if(out->hold_active&&out->until_us>hold_grant)out->until_us=hold_grant;
  }else if(!r->have_pair)out->state=3;
 }
 watch_snapshot(out,sampled,now);
 // assurance/authority remain zero: no independently bounded admission or relay.
}
void n30_rc8_snapshot(struct n30_lora_view* out,uint64_t now){n30_rc8_snapshot_sample(out,now,now);}
