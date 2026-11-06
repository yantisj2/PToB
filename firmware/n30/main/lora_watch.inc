// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// N30-only authenticated observer and human watch. No live grant is extended.
// This translation-unit component shares the LoRa-only estimator, not BLE time.
struct watch_description {struct lp_scope scope;struct r2_tz tz;uint8_t context[16];uint64_t at;uint32_t serial;bool known;};
static struct watch_description watch_description;
static struct delivered watch_model;
static struct forecast watch_forecast;
static struct {uint64_t tick,ns,epoch,at,first_tick,error_at,pending_tick,pending_epoch,pending_us,pending_until;uint32_t highest,meta_cursor,pending_cursor,pairs,rejected,missing,unmatched,last_age_ms,predictions;uint8_t state,reason,prediction_mode;bool admitted,pending;} watch;
static void watch_fence(uint8_t reason){
 watch.admitted=watch.pending=false;watch.reason=reason;watch.state=5;watch.tick=watch.ns=watch.epoch=watch.at=watch.first_tick=0;
 watch.pairs=watch.predictions=0;watch.error_at=0;watch_forecast.recent_count=watch_forecast.recent_head=0;
 memset(&watch_model,0,sizeof watch_model);gm_clear(&thermal);
}
static bool watch_project(const struct delivered* m,uint64_t tick,uint64_t epoch,uint64_t* ns){
 if(!m->valid||m->epoch!=epoch||tick<m->tick)return false;
 const uint64_t cap=UINT64_C(300000000)*16;
 if(tick-m->tick<=cap)return project_math(m,tick,epoch,ns);
 if(m->tick>UINT64_MAX-cap)return false;
 uint64_t anchor;if(!project_math(m,m->tick+cap,epoch,&anchor))return false;
 // Unqualified long human HOLD: freeze the derivative at300s, deliberately
 // dropping residual acceleration. Original reference/error ages and covariance
 // remain untouched. This approximation supplies no timing authority.
 const double slow=120.0*(1.0-n30_lora_decay(300)),fast=30.0*(1.0-n30_lora_decay_fast(300));
 const double rate=m->rate*.001+m->accel*.000001*slow+m->fast_accel*.000001*fast;
 if(!isfinite(rate)||fabs(rate)>100000)return false;
 return r2_hold_lora_project(tick,m->tick+cap,anchor,(int32_t)llround(rate),0,ns);
}
static void watch_meta(const struct lp_packet* p,const uint8_t context[16],uint64_t at){
 const bool same=watch_description.known&&!memcmp(watch_description.context,context,16);
 if(!same){watch_fence(2);memset(&watch_forecast,0,sizeof watch_forecast);watch.pairs=watch.rejected=watch.missing=0;watch.error_at=0;}
 watch_description.scope=p->scope;watch_description.tz=p->tz;watch_description.at=at;watch_description.serial=p->serial;watch_description.known=true;
 memcpy(watch_description.context,context,16);
 if(!p->time_valid){watch_fence(3);return;}
 if(!watch.admitted){watch.highest=p->cursor;watch.pending=false;}
 watch.meta_cursor=p->cursor;watch.admitted=true;watch.reason=0;
}
static void watch_expire(uint64_t now){
 if(watch.pending&&now>=watch.pending_until){watch.pending=false;inc(&watch.unmatched);}
}
static bool watch_sync(uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t at,uint64_t now){
 watch_expire(now);
 if(!watch.admitted||!epoch||!tick||at>UINT64_MAX-LP_PENDING_US||now<at||now-at>LP_SYNC_PROCESS_US||cursor<watch.highest||
    ((uint64_t)cursor-watch.highest>32&&(cursor<watch.meta_cursor||(uint64_t)cursor-watch.meta_cursor>32))){watch.pending=false;inc(&watch.rejected);return false;}
 watch.pending=true;watch.pending_cursor=cursor;watch.pending_tick=tick;watch.pending_epoch=epoch;watch.pending_us=at;watch.pending_until=at+LP_PENDING_US;return true;
}
static bool watch_follow(const struct lp_packet* p,uint64_t epoch,uint64_t now){
 if(!watch.admitted||!watch.pending||p->cursor!=watch.pending_cursor||p->cursor<watch.meta_cursor||epoch!=watch.pending_epoch||now<watch.pending_us||now>=watch.pending_until){inc(&watch.rejected);watch.pending=false;return false;}
 const uint64_t tick=watch.pending_tick,at=watch.pending_us;watch.pending=false;
 uint64_t ns=p->utc_ns;
if(LP_FIXED_EVENT_DELAY_NS<0){
  if(ns<(uint64_t)-LP_FIXED_EVENT_DELAY_NS){inc(&watch.rejected);return false;}ns-=(uint64_t)-LP_FIXED_EVENT_DELAY_NS;
 }else{
  if(!ns||ns>UINT64_MAX-(uint64_t)LP_FIXED_EVENT_DELAY_NS){inc(&watch.rejected);return false;}ns+=(uint64_t)LP_FIXED_EVENT_DELAY_NS;
 }
 if(watch.tick&&(p->cursor<=watch.highest||epoch!=watch.epoch||tick<=watch.tick||ns<=watch.ns||at<watch.at)){inc(&watch.rejected);return false;}
 uint64_t predicted=0;const struct delivered* prior=&watch_model;bool have=false;
 if(model.valid&&project(&model,tick,epoch,&predicted)){prior=&model;have=true;
  const bool frozen=model.tick==held_model.tick&&model.ns==held_model.ns&&model.epoch==held_model.epoch&&model.physical==held_model.physical&&at>=hold_start&&at<hold_until;
  watch.prediction_mode=frozen?3:2;}
 else{have=watch_project(&watch_model,tick,epoch,&predicted);watch.prediction_mode=watch.at&&at-watch.at>=35000000?4:1;}
 if(have){record(&watch_forecast,predicted,ns);inc(&watch.predictions);watch.error_at=at;const uint64_t age=tick>=prior->physical?(tick-prior->physical)/16000:UINT64_MAX;watch.last_age_ms=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
 if(watch.tick){
  const uint64_t delta=tick-watch.tick,remote=ns-watch.ns;
  if(delta>UINT64_MAX/125||remote>INT64_MAX){watch_fence(6);inc(&watch.rejected);return false;}
  const uint64_t nominal=delta*125/2,error=remote>nominal?remote-nominal:nominal-remote;
  if(error>nominal/10000){watch_fence(6);inc(&watch.rejected);return false;}
  if(p->cursor-watch.highest>2){const uint32_t missing=(p->cursor-watch.highest)/2-1;watch.missing=missing>UINT32_MAX-watch.missing?UINT32_MAX:watch.missing+missing;}
 }
 if(!gm_observed_point(&thermal,tick,ns)){watch_fence(6);inc(&watch.rejected);return false;}
 if(!watch.first_tick)watch.first_tick=tick;
 watch.tick=tick;watch.ns=ns;watch.epoch=epoch;watch.at=at;watch.highest=p->cursor;inc(&watch.pairs);return true;
}
static void watch_commit(uint64_t tick,uint64_t epoch){
 if(!watch.admitted||!watch.tick||epoch!=watch.epoch||tick<watch.tick||watch_model.physical==watch.tick)return;
 uint64_t ns;if(!gm_observed_predict(&thermal,tick,&ns))return;
 const double dt=(double)(tick-thermal.tick)/16000000.0,u=n30_lora_decay(dt),uf=n30_lora_decay_fast(dt),g=120.0*(1.0-u),gf=30.0*(1.0-uf);
 watch_model=(struct delivered){.tick=tick,.ns=ns,.epoch=epoch,.physical=watch.tick,.physical_us=watch.at,
  .rate=llround((thermal.rate+thermal.accel*g+thermal.fast_accel*gf)*1000),.accel=llround(thermal.accel*u*1000000),.fast_accel=llround(thermal.fast_accel*uf*1000000),.valid=true};
}
static uint32_t watch_age(uint64_t now,uint64_t at){if(!at||now<at)return UINT32_MAX;const uint64_t age=(now-at)/1000;return age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
static void watch_snapshot(struct n30_lora_view* out,uint64_t sampled,uint64_t now){
 watch_expire(now);out->metadata_cached=watch_description.known;
 if(!receiver.cached&&watch_description.known){const struct lp_scope* s=&watch_description.scope;
  out->emitter=s->emitter;out->emitter_boot=s->boot;out->root=s->root;out->root_boot=s->root_boot;out->parent=s->parent;out->parent_boot=s->parent_boot;out->stream=s->stream;out->source_epoch=s->epoch;out->kind=s->kind;
  memcpy(out->context,watch_description.context,16);out->tz_id=watch_description.tz.id;out->tz_offset_min=watch_description.tz.offset_min;out->metadata_us=watch_description.at;out->diagnostic_sequence=watch_description.serial;out->snapshot_age_ms=watch_age(now,watch_description.at);
 }
 out->watch_pairs=watch.pairs;out->watch_rejected=watch.rejected;out->watch_missing=watch.missing;out->watch_unmatched=watch.unmatched;out->watch_age_ms=watch_age(now,watch.at);out->watch_error_age_ms=watch_age(now,watch.error_at);
 out->watch_prediction_n=watch.predictions;out->watch_total_prediction_n=watch_forecast.count;out->watch_rms_ns=recent_rms(&watch_forecast);out->watch_total_rms_ns=rms(&watch_forecast);out->watch_max_ns=watch_forecast.maximum;out->watch_over1us=watch_forecast.over;out->watch_last_ns=watch_forecast.last;out->watch_prediction_age_ms=watch.last_age_ms;out->watch_prediction_mode=watch.prediction_mode;
 const uint64_t span=watch.tick>=watch.first_tick?(watch.tick-watch.first_tick)/16000:0;out->watch_span_ms=span>UINT32_MAX?UINT32_MAX:(uint32_t)span;out->watch_reason=watch.reason;
 uint64_t ns=0;out->watch_valid=enabled&&watch.admitted&&watch_model.valid&&sampled<=now&&now-sampled<=250000&&current_us==sampled&&watch_project(&watch_model,current_tick,current_epoch,&ns);
 out->watch_sampled_us=sampled;out->watch_utc_us=out->watch_valid?ns/1000:0;out->watch_rate_ppb=(int32_t)llround(watch_model.rate*.001);
 out->watch_state=!out->watch_valid?watch.reason?5:0:out->watch_age_ms>=35000?3:!out->metadata_known?4:out->state==2?2:1;
}
