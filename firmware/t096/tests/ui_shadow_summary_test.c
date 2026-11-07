/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
/* Exact observer/session integration, fake immutable source records. */
struct rc_shadow_record;
static void record_filter(struct rc_shadow_record *);
#define R2_UI_SHADOW_RECORD record_filter
#define main state_main
#include "ui_critical_state_test.c"
#undef main
static unsigned population_change;
static void record_filter(struct rc_shadow_record *r){
 r->map.normal=true;
 if(r->sequence%11==0)r->phase_ns=500000000;
 if(r->sequence%17==0){r->valid=false;r->reason=RC_SHADOW_EXPIRED;}
 if(r->sequence%19==0)r->reference_fix=false;
 if(r->sequence%23==0)r->phase_ns=INT64_MIN;
 if(r->sequence%29==0)r->reference_seen=false;
 if(r->sequence>=30)switch(population_change){
  case 1:r->map.parent_epoch++;break;case 2:r->map.train++;break;
  case 3:r->map.local_hold=true;break;case 4:r->map.lora_delay_ns=123456789;break;
  case 5:r->map.lora=true;break;
 }
}
static void begin(void){
 reset();atomic_set(&ui_critical_dirty,0);priority=9;atomic_set(&ui_worker_phase,UI_UNSAFE);
 memset(&shadow_session,0,sizeof shadow_session);memset(&shadow_scope,0,sizeof shadow_scope);
 memset(&ui_shadow_observer,0,sizeof ui_shadow_observer);shadow_due=0;mode=0;witness_enabled=false;
 status.page=status.drawn_page=R2_UI_SHADOW_PAGE;status.ready=true;
}
static void tick(uint64_t now){
 fake_now=now;model.sampled_ms=now;model.ble_valid_until_us=(now+1000)*1000;
 model.clock.pps_present=model.clock.fix_active=true;model.clock.pps_age_us=(int64_t)(now%1000)*1000;
 struct r2_ui_status s=status;
 shadow_tick(&model,&s,now,true,1,0,7);
 /* Calls have no writable clock/model alias or discipline/export feedback. */
 assert(model.sampled_ms==now&&model.ble_valid_until_us==(now+1000)*1000);
 assert(ui_shadow_observer.published.session==status.shadow.session);
 assert(ui_shadow_observer.published.samples==status.shadow.samples);
}
int main(void){
 state_main();begin();population_change=0;
 for(uint64_t now=1000;now<=29000;now+=250)tick(now);
 assert(shadow_session.active&&status.shadow.valid&&status.shadow.samples);
 struct r2_shadow_session independent={.entered=true};
 const struct r2_ui_shadow_window w=ui_shadow_observer.published;
 assert(w.session==1&&w.context==1&&w.hint==0&&w.control==7&&w.start_sequence<w.cursor);
 assert(r2_shadow_session_begin(&independent,0,w.start_tick,w.epoch,w.start_sequence));
 uint32_t cursor=w.start_sequence;
 while(cursor<w.cursor){
  struct rc_shadow_record r;struct rc_shadow_status st;assert(r2_shadow_read(cursor,&r,&st));cursor=r.sequence;
  bool usable=r.valid&&r.reason==RC_SHADOW_OK&&r.reference_seen&&r.reference_fix&&r.reference_pps;
  r2_shadow_session_sample(&independent,r.sequence,r.edge.tick,r.edge.epoch,usable,r.phase_ns);
 }
 struct r2_shadow_session_view v;r2_shadow_session_view(&independent,&v);
 assert(v.samples==status.shadow.samples&&v.skipped==status.shadow.skipped&&v.missing==status.shadow.missing);
 assert(v.mean_tenth_ns==status.shadow.mean_tenth_ns&&v.rms_ns==status.shadow.rms_ns&&
  v.max_ns==status.shadow.max_ns&&v.p95_ns==status.shadow.p95_ns&&v.p99_ns==status.shadow.p99_ns);
 assert(v.max_ns==500000000&&v.skipped&&v.approximate);
 /* Private session turnover never publishes a partially changed window. */
 ui_shadow_observer.context=2;ui_shadow_observer.hint=16;ui_shadow_observer.control=8;
 ui_shadow_observer.start_sequence=123;
 assert(!memcmp(&w,&ui_shadow_observer.published,sizeof w));
 unsigned key=ui_take();shadow_publish_locked(&status.shadow);ui_drop(key);
 assert(ui_shadow_observer.published.context==2&&ui_shadow_observer.published.start_sequence==123);
 for(unsigned change=1;change<=5;change++){
  begin();population_change=change;
  for(uint64_t now=1000;now<=31000;now+=250)tick(now);
  assert(!shadow_session.active&&shadow_session.session==1);
  bool restarted=false;
  for(uint64_t now=31250;now<=60000;now+=250){tick(now);if(shadow_session.active){restarted=true;break;}}
  assert(restarted&&shadow_session.session==2&&shadow_session.start_tick>=UINT64_C(30)*16000000);
 }
 puts("PASS actual UI warmup/cursor/publication, exact independently reconstructed mean/RMS/tails/exclusions and5 isolated source-population turnovers");
}
