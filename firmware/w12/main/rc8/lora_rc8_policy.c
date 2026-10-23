/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_rc8_policy.h"
#include <limits.h>
#include "precision_servo.h"
#include <string.h>
static void count(uint32_t *n){if(*n<UINT32_MAX)++*n;}
static bool add(uint64_t a,uint64_t b,uint64_t *out){if(b>UINT64_MAX-a)return false;*out=a+b;return true;}
bool rc8_is_transmitter(const struct rc8_enrollment *e){return e&&e->enrolled&&e->factory&&e->factory==e->transmitter_factory;}
struct rc8_requests rc8_requests_default(const struct rc8_enrollment *e){
 bool valid=e&&e->enrolled&&e->factory&&e->transmitter_factory;
 return (struct rc8_requests){.tx=valid&&rc8_is_transmitter(e),.rx=valid&&!rc8_is_transmitter(e)};
}
bool rc8_command_allowed(const struct rc8_enrollment *e,enum rc8_profile p,bool requested,enum rc8_command command){
 if(!e||!e->enrolled||!e->factory||!e->transmitter_factory||!requested||
 (p!=RC8_PROFILE_GM&&p!=RC8_PROFILE_RC))return false;
 if(command==RC8_CMD_TX)return p==RC8_PROFILE_GM&&rc8_is_transmitter(e);
 if(command==RC8_CMD_RX)return !rc8_is_transmitter(e);
 return false;
}
bool rc8_export_select(struct rc8_export *out,const struct rc8_export *own,
 const struct rc8_export *selected,bool gm,uint64_t factory,uint64_t boot){
 const struct rc8_export *s=gm?own:selected;
 if(!out)return false;
 /* Permit callers to select in place without clearing the selected input. */
 struct rc8_export saved={0};if(s)saved=*s;
 memset(out,0,sizeof *out);s=s?&saved:NULL;
 if(!s||!factory||!boot||!s->valid||!s->authenticated||
 s->scope.emitter!=factory||s->scope.emitter_boot!=boot||
 s->scope.kind!=(gm?RC8_OWN_GM:RC8_SELECTED_BLE)||
 (gm?s->scope.root!=factory:s->scope.root==factory))return false;
 *out=*s;return true;
}
bool rc8_export_close(struct rc8_export *s,uint64_t cut,uint64_t epoch){
 if(!s||!epoch||epoch!=s->capture_epoch)return false;
 if(cut<s->until_tick)s->until_tick=cut;
 return true;
}
static bool project(struct rc8_correction *c,const struct rc8_export *s,uint32_t sequence,
 uint64_t tick,uint64_t epoch){
 if(!c)return false;
 memset(c,0,sizeof *c);
 if(!s||!s->valid||!s->authenticated||!sequence||!s->map_generation||!epoch||epoch!=s->capture_epoch||
 !tick||tick<s->from_tick||tick>=s->until_tick||tick<s->anchor_tick||
 s->mode>RC8_MAP_DELIVERED)return false;
 uint64_t ticks=tick-s->anchor_tick,delta=0;
 if(s->mode==RC8_MAP_SOURCE_Q16){
  const uint64_t q=65536;
  if(ticks>24000000||s->rate_q16<15990000*q||s->rate_q16>16010000*q||s->rate_ppb>5000000||s->rate_ppb< -5000000)return false;
  uint64_t scaled=ticks*q,micro=(scaled%s->rate_q16)*1000000;
  delta=(scaled/s->rate_q16)*1000000000+(micro/s->rate_q16)*1000+(micro%s->rate_q16)*1000/s->rate_q16;
  int64_t adjust=(int64_t)delta*s->rate_ppb/1000000000;
  if(adjust>0)delta-=(uint64_t)adjust;else if(!add(delta,(uint64_t)-adjust,&delta))return false;
 }else if(s->mode==RC8_MAP_DELIVERED){
  if(s->anchor_ns>INT64_MAX)return false;
  r2_servo_map_t m={.delivery=s->anchor_tick,.anchor=(int64_t)s->anchor_ns,.phase=s->phase_ns,.rate=s->rate_ppb,.valid=true};
  int64_t result;
  if(!r2_servo_mapped(&m,tick,&result)||result<0||(uint64_t)result<s->anchor_ns)return false;
  delta=(uint64_t)result-s->anchor_ns;
 }else{
  if(s->rate_ppb>1000000||s->rate_ppb< -1000000||ticks>UINT64_MAX/125)return false;
  delta=ticks*125/2;
  uint64_t magnitude=s->rate_ppb<0?(uint64_t)-(int64_t)s->rate_ppb:(uint64_t)s->rate_ppb;
  uint64_t adjust=(delta/1000000000)*magnitude+(delta%1000000000)*magnitude/1000000000;
  if(s->rate_ppb<0)delta-=adjust;else if(!add(delta,adjust,&delta))return false;
 }
 uint64_t ns;if(!add(s->anchor_ns,delta,&ns)||ns<s->created_ns||ns>=s->expires_ns)return false;
 uint64_t age=(tick-s->from_tick)/16;
 if(age>UINT32_MAX-s->map_age_us)return false;
 *c=(struct rc8_correction){.sequence=sequence,.map_generation=s->map_generation,
 .event_ns=ns,.created_ns=s->created_ns,.expires_ns=s->expires_ns,
 .map_age_us=s->map_age_us+(uint32_t)age,.upstream_bound_ns=s->upstream_bound_ns};return true;
}
bool rc8_export_valid_at(const struct rc8_export *s,uint64_t tick,uint64_t epoch){
 struct rc8_correction ignored;return project(&ignored,s,1,tick,epoch);
}
bool rc8_export_event(struct rc8_correction *c,const struct rc8_export *s,uint32_t sequence,struct rc8_event event){
 if(event.kind!=RC8_EVENT_TX_DONE){if(c)memset(c,0,sizeof *c);return false;}
 return project(c,s,sequence,event.tick,event.capture_epoch);
}
bool rc8_tx_prepare(struct rc8_transmission *t,const struct rc8_enrollment *e,enum rc8_profile profile,
 bool requested,const struct rc8_export *map,const struct r2_group_config *g,uint32_t seq,uint32_t gen,uint64_t tick,uint64_t epoch){
 if(!t||t->pending||!map||!gen||!rc8_command_allowed(e,profile,requested,RC8_CMD_TX)||map->scope.emitter!=e->factory)return false;
 if(t->generation&&gen<=t->generation)return false;
 bool same=rc8_scope_equal(&t->map.scope,&map->scope);
 if(t->map.scope.emitter_boot==map->scope.emitter_boot){
  if(map->scope.stream<t->map.scope.stream||
   (map->scope.stream==t->map.scope.stream&&(!same||seq<=t->packet.sequence)))return false;
 }
 struct rc8_correction prepared;
 if(!project(&prepared,map,seq,tick,epoch))return false;
 struct rc8_packet packet={.sequence=seq,.root=map->scope.root,.source_epoch=map->scope.source_epoch,.scale=map->scope.scale,.kind=map->scope.kind};
 if(rc8_context_id(packet.context,&map->scope,g))return false;
 if(same&&t->previous_valid){packet.corrected=true;packet.correction=t->previous;}
 if(rc8_packet_validate(&packet))return false;
 struct rc8_correction previous=t->previous;
 *t=(struct rc8_transmission){.map=*map,.packet=packet,.previous=previous,.preparation_tick=tick,
  .generation=gen,.pending=true,.previous_valid=same&&t->previous_valid};
 return true;
}
bool rc8_tx_complete(struct rc8_correction *out,struct rc8_transmission *t,struct rc8_event event){
 if(!out)return false;
 memset(out,0,sizeof *out);
 if(!t||!t->pending||event.generation!=t->generation||event.kind!=RC8_EVENT_TX_DONE||event.capture_epoch!=t->map.capture_epoch)return false;
 t->pending=false;t->previous_valid=false;
 if(event.tick<t->preparation_tick||event.tick-t->preparation_tick>16000000||
  !rc8_export_event(out,&t->map,t->packet.sequence,event))return false;
 t->previous=*out;t->previous_valid=true;return true;
}
static bool interval(const struct rc8_bootstrap *b,uint64_t us,uint64_t *lo,uint64_t *hi){
 if(us<b->at_us||us>=b->until_us||us-b->at_us>RC8_ANCHOR_MAX_US)return false;
 uint64_t ns=(us-b->at_us)*1000,error=ns*b->rate_bound_ppb/1000000000;
 if(!add(b->lower_ns,ns,lo)||!add(b->upper_ns,ns,hi)||*lo<error||!add(*hi,error,hi))return false;
 *lo-=error;
 return *hi-*lo<=RC8_ANCHOR_WIDTH_NS;
}
bool rc8_receiver_init(struct rc8_receiver *s,const struct rc8_enrollment *e){
 if(!s)return false;
 memset(s,0,sizeof *s);
 if(!e||!e->enrolled||!e->factory||!e->transmitter_factory)return false;
 s->factory=e->factory;s->transmitter_factory=e->transmitter_factory;return true;
}
void rc8_receiver_switch(struct rc8_receiver *s,bool enabled){
 if(!s)return;
 uint32_t gen=s->generation;
 uint64_t factory=s->factory,transmitter=s->transmitter_factory;
 uint32_t accepted=s->accepted,rejected=s->rejected,replayed=s->replayed,overflow=s->overflow,unmatched=s->unmatched,miss=s->metadata_misses;
 memset(s,0,sizeof *s);s->factory=factory;s->transmitter_factory=transmitter;s->generation=gen;
 s->accepted=accepted;s->rejected=rejected;s->replayed=replayed;s->overflow=overflow;s->unmatched=unmatched;s->metadata_misses=miss;
 if(gen==UINT32_MAX)return;
 s->generation++;s->enabled=enabled&&factory&&transmitter&&factory!=transmitter;
}
bool rc8_receiver_bootstrap(struct rc8_receiver *s,const struct rc8_bootstrap *b,uint64_t now){
 if(!s||!s->enabled||!s->cached||!s->metadata_known||!b||rc8_scope_validate(&b->scope)||!rc8_scope_equal(&s->scope,&b->scope)||b->at_us!=now||b->until_us<=now||
 b->until_us-now>RC8_ANCHOR_MAX_US||b->upper_ns<b->lower_ns||b->upper_ns-b->lower_ns>100000000||
 b->rate_bound_ppb>100000||!b->min_sequence)return false;
 s->trust=*b;s->anchored=true;s->continuity=false;
 if(b->min_sequence-1>s->highest_sequence)s->highest_sequence=b->min_sequence-1;
 memset(s->history,0,sizeof s->history);memset(&s->sample,0,sizeof s->sample);memset(&s->fit,0,sizeof s->fit);s->queued_valid=false;
 return true;
}
bool rc8_receiver_freeze(struct rc8_receiver *s,const uint8_t *wire,size_t n,struct rc8_event event){
 if(!s||!s->enabled||!wire||(n!=RC8_BYTES&&n!=RC8_META_BYTES)||event.kind!=RC8_EVENT_RX_DONE||
 !event.tick||!event.capture_epoch||event.generation!=s->generation){if(s)count(&s->rejected);return false;}
 if(s->queued_valid){count(&s->overflow);return false;}
 memcpy(s->queued,wire,n);s->queued_size=(uint8_t)n;s->queued_event=event;s->queued_valid=true;return true;
}
static void reset_timing(struct rc8_receiver *s){
 s->anchored=false;s->continuity=false;
 memset(&s->trust,0,sizeof s->trust);memset(s->history,0,sizeof s->history);memset(&s->sample,0,sizeof s->sample);memset(&s->fit,0,sizeof s->fit);
}
static uint32_t root_u64(uint64_t value){
 uint64_t result=0,bit=UINT64_C(1)<<62;
 while(bit>value)bit>>=2;
 while(bit){if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
 return (uint32_t)result;
}
static void __attribute__((noinline)) link_fit(struct rc8_receiver *s,const struct rc8_event *rx,uint64_t ns){
 struct rc8_link_fit *f=&s->fit;
 if(f->n>=16||(s->sample.valid&&(rx->capture_epoch!=s->sample.rx.capture_epoch||rx->local_us<=s->sample.rx.local_us||rx->local_us-s->sample.rx.local_us>=RC8_HOLD_US)))memset(f,0,sizeof *f);
 if(!f->n){f->tick=rx->tick;f->ns=ns;f->n=1;return;}
 /* Explicit bounds keep every integer sum exact and far from overflow.
  * A discontinuity starts a new block without changing timing admission. */
 if(rx->tick<f->tick||ns<f->ns||rx->tick-f->tick>UINT64_C(1920000000))goto restart;
 uint64_t ticks=rx->tick-f->tick,elapsed=ns-f->ns,nominal=ticks*125/2;
 if((elapsed>nominal?elapsed-nominal:nominal-elapsed)>16000000)goto restart;
 int32_t x=(int32_t)(ticks/1600),y=elapsed>=nominal?(int32_t)(elapsed-nominal):-(int32_t)(nominal-elapsed);
 f->x+=x;f->y+=y;f->xx+=(int64_t)x*x;f->xy+=(int64_t)x*y;f->yy+=(int64_t)y*y;++f->n;
 if(f->n>=4){
  double n=f->n,xx=(double)f->xx-(double)f->x*f->x/n,xy=(double)f->xy-(double)f->x*f->y/n;
  double yy=(double)f->yy-(double)f->y*f->y/n;
  if(xx<=0)goto restart;
  double variance=(yy-xy*xy/xx)/n;
  f->rms_ns=variance>0?root_u64((uint64_t)variance):0;
 }
 return;
restart:
 memset(f,0,sizeof *f);f->tick=rx->tick;f->ns=ns;f->n=1;
}
static void adopt(struct rc8_receiver *s,const uint8_t *id){
 if(s->cached)memcpy(s->retired_context,s->context,RC8_CONTEXT_BYTES);
 memcpy(s->context,id,RC8_CONTEXT_BYTES);s->cached=true;s->metadata_known=false;s->metadata_until_us=0;
 s->diagnostic_sequence=0;s->withdrawal_sequence=0;s->highest_sequence=0;s->time_valid=false;
 memset(&s->quality,0,sizeof s->quality);reset_timing(s);
}
static bool metadata(struct rc8_receiver *s,const struct r2_group_config *g,struct rc8_event rx,uint64_t now,uint64_t origin_boot,uint32_t origin_epoch){
 struct rc8_companion p;uint8_t id[RC8_CONTEXT_BYTES];uint64_t until;
 if(rc8_companion_decode(&p,id,s->queued,s->queued_size,g)||p.scope.emitter!=s->transmitter_factory||
 (p.scope.root==s->factory&&(!origin_boot||!origin_epoch||p.scope.root_boot!=origin_boot||p.scope.source_epoch!=origin_epoch||!p.time_valid))||
 (s->cached&&rx.local_us<s->last_timing_us)||!add(rx.local_us,RC8_CONTEXT_US,&until)||now>=until){count(&s->rejected);return false;}
 if(p.scope.root==s->factory&&(!s->cached||!s->metadata_known||s->scope.root==s->factory||
    p.scope.emitter_boot!=s->scope.emitter_boot||p.scope.stream<=s->scope.stream||
    p.timing_sequence<s->highest_sequence)){
  /* A local origin fact never resolves an unknown foreign stream/boot or
   * authenticates a cold/replayed context. Only a monotonic same-emitter-boot
   * transition from a FULL foreign scope can retire its old constraint. */
  count(&s->rejected);return false;
 }
 bool same=s->cached&&!memcmp(id,s->context,RC8_CONTEXT_BYTES);
 if(!same&&s->cached){
  if(!memcmp(id,s->retired_context,RC8_CONTEXT_BYTES)){count(&s->replayed);return false;}
  if(s->metadata_known){
   if(p.scope.emitter_boot==s->scope.emitter_boot&&(p.scope.stream<=s->scope.stream||p.timing_sequence<s->highest_sequence)){count(&s->replayed);return false;}
   if(p.scope.emitter_boot!=s->scope.emitter_boot){
    if(p.scope.emitter_boot==s->retired_boot||(s->last_timing_us&&rx.local_us-s->last_timing_us<RC8_HOLD_US)){count(&s->replayed);return false;}
    s->retired_boot=s->scope.emitter_boot;
   }
  }else if(p.timing_sequence<s->highest_sequence&&s->last_timing_us&&rx.local_us-s->last_timing_us<RC8_HOLD_US){count(&s->replayed);return false;}
 }
 if(same&&p.sequence<=s->diagnostic_sequence){count(&s->replayed);return false;}
 if(same&&(p.scope.root!=s->scope.root||p.scope.source_epoch!=s->scope.source_epoch||
 p.scope.scale!=s->scope.scale||p.scope.kind!=s->scope.kind)){count(&s->rejected);return false;}
 if(!same)adopt(s,id);
 /* Filling unknown boots or refreshing quality is not a timing transition. */
 s->scope=p.scope;s->metadata_known=true;s->metadata_until_us=until;
 s->quality=p.quality;s->diagnostic_us=rx.local_us;s->diagnostic_sequence=p.sequence;
 s->snapshot_age_ms=p.snapshot_age_ms;s->diagnostic_events=p.events;s->time_valid=p.time_valid;
 if(!p.time_valid&&p.timing_sequence>=s->highest_sequence&&p.timing_sequence>=s->withdrawal_sequence){
  s->withdrawal_sequence=p.timing_sequence;reset_timing(s);
 }
 return false;
}
static bool candidate(struct rc8_receiver *s,const struct rc8_history *h,const struct rc8_correction *c,uint64_t now){
 if(!h->occupied||h->sequence!=c->sequence||now<h->rx.local_us||now-h->rx.local_us>RC8_CORRECTION_US)return false;
 uint64_t lo,hi;
 bool anchored=s->anchored&&interval(&s->trust,h->rx.local_us,&lo,&hi);
 if(anchored&&(c->event_ns>hi||(lo>c->event_ns&&lo-c->event_ns>RC8_MAX_PREP_NS)))return false;
 bool continuous=false;
 if(s->sample.valid){
  if(h->rx.local_us<=s->sample.rx.local_us||c->event_ns<=s->sample.tx.event_ns)return false;
  uint64_t elapsed=h->rx.local_us-s->sample.rx.local_us;
  if(elapsed<RC8_HOLD_US){
   uint64_t expected=elapsed*1000,actual=c->event_ns-s->sample.tx.event_ns;
   uint64_t error=actual>expected?actual-expected:expected-actual;
   if(error>RC8_MAX_PREP_NS+expected/10000)return false;
   continuous=s->continuity;
  }
 }
 s->continuity=anchored||continuous;
 return true;
}
bool rc8_receiver_process(struct rc8_receiver *s,const struct r2_group_config *g,uint64_t now){
 return rc8_receiver_process_origin(s,g,now,0,0);
}
bool rc8_receiver_process_origin(struct rc8_receiver *s,const struct r2_group_config *g,uint64_t now,uint64_t origin_boot,uint32_t origin_epoch){
 if(!s||!s->queued_valid)return false;
 struct rc8_event rx=s->queued_event;
 if(!s->enabled||now<rx.local_us||rx.generation!=s->generation){s->queued_valid=false;count(&s->rejected);return false;}
 if(s->queued_size==RC8_META_BYTES){bool result=metadata(s,g,rx,now,origin_boot,origin_epoch);s->queued_valid=false;return result;}
 struct rc8_packet p;int decoded=rc8_decode(&p,s->queued,s->queued_size,g);s->queued_valid=false;
 if(decoded){count(&s->rejected);return false;}
 if(p.root==s->factory||(p.kind==RC8_OWN_GM&&p.root!=s->transmitter_factory)||
 (p.kind==RC8_SELECTED_BLE&&p.root==s->transmitter_factory)){count(&s->rejected);return false;}
 if(rx.local_us<s->last_timing_us){s->anchored=false;s->continuity=false;count(&s->replayed);return false;}
 if(!s->cached||memcmp(p.context,s->context,RC8_CONTEXT_BYTES)){
  if(s->cached&&(!memcmp(p.context,s->retired_context,RC8_CONTEXT_BYTES)||
   (p.sequence<=s->highest_sequence&&rx.local_us-s->last_timing_us<RC8_HOLD_US))){count(&s->replayed);return false;}
  adopt(s,p.context);
  s->scope=(struct rc8_scope){.emitter=s->transmitter_factory,.root=p.root,.source_epoch=p.source_epoch,.scale=p.scale,.kind=p.kind};
 }else if(s->scope.root!=p.root||s->scope.source_epoch!=p.source_epoch||s->scope.scale!=p.scale||s->scope.kind!=p.kind){count(&s->rejected);return false;}
 if(p.sequence<=s->highest_sequence){count(&s->replayed);return false;}
 if(s->sample.valid&&rx.capture_epoch!=s->sample.rx.capture_epoch){
  s->anchored=false;s->continuity=false;memset(s->history,0,sizeof s->history);memset(&s->sample,0,sizeof s->sample);memset(&s->fit,0,sizeof s->fit);
 }
 s->highest_sequence=p.sequence;s->last_timing_us=rx.local_us;
 bool accepted=false;
 for(unsigned i=0;i<RC8_HISTORY;i++){
  struct rc8_history *h=&s->history[i];
  if(h->occupied&&(now<h->rx.local_us||now-h->rx.local_us>RC8_CORRECTION_US))h->occupied=false;
  if(p.corrected&&h->occupied&&h->sequence==p.correction.sequence){
   if(p.correction.sequence>s->withdrawal_sequence&&candidate(s,h,&p.correction,now)){
    link_fit(s,&h->rx,p.correction.event_ns);
    s->sample=(struct rc8_sample){.rx=h->rx,.tx=p.correction,.valid=true};accepted=true;count(&s->accepted);
   }
   h->occupied=false;
  }
 }
 if(p.corrected&&!accepted)count(&s->unmatched);
 for(unsigned i=0;i<RC8_HISTORY;i++)if(!s->history[i].occupied){
  s->history[i]=(struct rc8_history){.rx=rx,.sequence=p.sequence,.occupied=true};return accepted;
 }
 count(&s->overflow);return accepted;
}
enum rc8_input_state rc8_receiver_state(const struct rc8_receiver *s,uint64_t now){
 if(!s||!s->enabled)return RC8_OFF;
 if(!s->sample.valid)return RC8_ACQUIRING;
 if(now<s->sample.rx.local_us||now-s->sample.rx.local_us>=RC8_HOLD_US)return RC8_EXPIRED;
 return RC8_AVAILABLE;
}
enum rc8_assurance rc8_receiver_assurance(const struct rc8_receiver *s,uint64_t now){
 if(rc8_receiver_state(s,now)!=RC8_AVAILABLE)return RC8_FRESHNESS_UNKNOWN;
 uint64_t lo,hi;
 if(s->continuity&&s->anchored&&interval(&s->trust,now,&lo,&hi))return RC8_ANCHORED_INTERVAL;
 return s->continuity?RC8_STREAM_CONTINUITY:RC8_FRESHNESS_UNKNOWN;
}
enum rc8_authority rc8_receiver_authority(const struct rc8_receiver *s,uint64_t now){
 if(rc8_receiver_assurance(s,now)!=RC8_ANCHORED_INTERVAL)return RC8_AUTHORITY_UNKNOWN;
 uint64_t lo,hi;
 if(!interval(&s->trust,now,&lo,&hi))return RC8_AUTHORITY_UNKNOWN;
 if(hi<s->sample.tx.expires_ns)return RC8_ORIGINAL_ENVELOPE_CURRENT;
 if(lo>=s->sample.tx.expires_ns)return RC8_ORIGINAL_ENVELOPE_EXPIRED;
 return RC8_AUTHORITY_UNKNOWN;
}
bool rc8_receiver_read(const struct rc8_receiver *s,uint64_t now,struct rc8_sample *sample,struct rc8_scope *scope){
 if(!sample||!scope)return false;
 memset(sample,0,sizeof *sample);memset(scope,0,sizeof *scope);
 if(rc8_receiver_state(s,now)!=RC8_AVAILABLE)return false;
 *sample=s->sample;*scope=s->scope;return true;
}
enum rc8_selected rc8_select(bool ble,enum rc8_input_state state,enum rc8_assurance assurance,bool unverified){
 if(ble)return RC8_SELECT_BLE;
 return state==RC8_AVAILABLE&&(assurance==RC8_ANCHORED_INTERVAL||unverified)?RC8_SELECT_LORA:RC8_SELECT_NONE;
}
uint32_t rc8_output_bound(const struct rc8_receiver *s){(void)s;return RC8_UNKNOWN;}
void rc8_stop_begin(struct rc8_stop *s,enum rc8_phase phase,uint64_t now){
 if(!s||s->stopping)return;
 s->from=phase;s->stopping=true;s->started_us=now;
 if(!add(now,RC8_DRAIN_US,&s->deadline_us)){s->deadline_us=now;s->fault=true;}
}
enum rc8_stop_result rc8_stop_poll(struct rc8_stop *s,uint64_t now,bool dma,bool radio,bool fem,bool capture){
 if(!s||!s->stopping)return RC8_STOP_FAULT;
 if(s->fault)return RC8_STOP_FAULT;
 if(s->quiescent)return RC8_STOP_QUIESCENT;
 if(now<s->started_us||now>=s->deadline_us){s->fault=true;return RC8_STOP_FAULT;}
 if(dma&&radio&&fem&&capture){s->quiescent=true;return RC8_STOP_QUIESCENT;}
 return RC8_STOP_WAIT;
}
bool rc8_gesture_update(struct rc8_gesture *s,bool pressed,bool eligible,uint64_t now){
 if(!s)return false;
 if(pressed&&!s->pressed){s->started_us=now;s->eligible=s->claimed=eligible;s->consumed=false;}
 if(s->pressed&&(!eligible||now<s->started_us))s->eligible=false;
 uint64_t held=now>=s->started_us?now-s->started_us:0;
 /* One action at the threshold, even if the operator keeps holding. Release
  * never repeats it. A delayed poll/release cannot turn this claimed press
  * into the device's unrelated power-off gesture. */
 bool toggle=s->pressed&&s->eligible&&!s->consumed&&held>=1500000;
 if(toggle)s->consumed=true;
 s->pressed=pressed;if(!pressed)s->eligible=false;return toggle;
}
