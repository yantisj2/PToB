/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual coalescer/worker with deterministic main/owner scheduling. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
static void fixture_render(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,uint64_t);

static void fixture_holds(struct r2_ui_holds *,const struct r2_ui_model *,uint64_t);
#define r2_ui_render fixture_render
#define r2_ui_holds_update fixture_holds
#include "../src/ui_service.c"
#undef r2_ui_render
#undef r2_ui_holds_update
struct hint_service {
 uint64_t profile_incarnation,identities[4],root_boot;
 uint32_t source_epoch,local_source_epoch;unsigned root;struct {uint32_t train;}precision;
 bool profile_rc,profile_pending,serving,active,closing,follower_clock_valid,source_eligible;int error;
};
struct hint_clock {struct {bool source_available,valid,fix_active,pps_present;uint32_t source_epoch;}view;};
#include "main_hint_under_test.h"
static unsigned scene,builds,render_calls,dropped,builds_during_busy;
static uint64_t produced[1024],main_now,precommit_at,busy_until;
static uint64_t owner_running_until;
static bool injected_tap;
static unsigned history_race;static uint32_t held_before;
static bool precommit_invalidated,history_checked,aba_checked;
static unsigned deferred_context;
static bool atomic_commit_armed,atomic_commit_done,atomic_commit_checked,history_atomic_armed,history_atomic_done,history_atomic_checked;
static bool render_invalidated,spi_invalidated,busy_expiry_seen,learned_survived,spi_checked;
static struct r2_ui_context ctx={.incarnation=1,.root=4,.boot=73,.source_epoch=1,.train=1,.flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE};
static void produce(uint64_t now){
 struct r2_ui_model m=sync_model(now);
 m.sampled_ms=now;m.ble_active=m.scope_active=m.authenticated_gm=m.ble_time_selected=true;
 m.clock.valid=true;m.clock.utc_us=1792340000000000LL+(int64_t)now*1000;
 m.ble_valid_until_us=(now+(scene==7?1500:500))*1000;m.role=R2_UI_FOLLOWER;
 m.precision.map_valid=m.precision.authorized=m.precision.locked=true;
 m.clock.pps_present=true;m.clock.pps_age_us=0;
 if(scene==6||scene==9){m.profile_rc=false;m.boundary_clock=false;m.local_hot_standby=true;}
#ifdef R2_TZ_WIRE
 m.tz=r2_tz_unknown();
#endif
 r2_ui_publish(&m);assert(builds<1024);produced[builds++]=now;
}
static void ui_schedule_poll(void){
 if(atomic_commit_done&&!atomic_commit_checked){
  struct r2_ui_status observed;r2_ui_snapshot(&observed);
  assert(status.drawn_ble); /* Main invalidated after the last in-lock check. */
  assert(!observed.drawn_ble&&!observed.analog&&!observed.drawn_time);
  atomic_commit_checked=true;
 }
 if(history_atomic_done&&!history_atomic_checked){
  struct r2_ui_status observed;r2_ui_snapshot(&observed);
  assert(status.holds.hot_since!=held_before);
  assert(!observed.holds.hot_since&&!observed.sync_display.at_ms);
  history_atomic_checked=true;
 }
 if((spi_invalidated||precommit_invalidated)&&!spi_checked){
  struct r2_ui_status observed;r2_ui_snapshot(&observed);
  assert(!observed.drawn_ble&&!observed.analog&&!observed.drawn_scope);spi_checked=true;
 }
 /* Main wakes every1ms. A normal2ms GM period is queued for one of those
  * ticks, then completes in its idle window; no forced projection on busy. */
 for(;main_now<=fake_now;main_now++){
  if(scene==10&&main_now>=1000&&main_now<3000){
   ctx.root=(main_now&1u)?2:4;ctx.boot=(main_now&1u)?438:73;r2_ui_context_update(&ctx);
   r2_ui_source_hint(R2_UI_HINT_KNOWN|((main_now&1u)?R2_UI_HINT_BLE_VALID:0));
  }
  bool busy=(scene>=11?main_now<owner_running_until:(main_now%2)==0)||(scene==2&&main_now>=2000&&main_now<4000)||(scene==7&&main_now<busy_until);
  (void)busy;
  if(scene==2&&main_now==2101){
#ifdef R2_TZ_WIRE
   r2_ui_learn_tz((struct r2_tz){2,-300},R2_UI_TZ_NETWORK);
#endif
  }
  if(main_now==fake_now&&r2_ui_logic_due(main_now,(main_now+500)*1000,true)){
   if(scene==2&&main_now>=2000&&main_now<4000)++builds_during_busy;
   produce(main_now);
  }
 }
 if(scene==2&&fake_now>=3100&&fake_now<3900){
  assert(model.sampled_ms>=fake_now-20&&r2_ui_ble_current(&model,fake_now));
  busy_expiry_seen=true;
 }
 if(scene==2&&fake_now>=4100){
#ifdef R2_TZ_WIRE
  assert(model.display_tz.id==2&&model.display_tz.offset_min==-300);learned_survived=true;
#endif
 }
 if(scene==12&&!injected_tap&&fake_now>=1000&&status.ready){
  assert(!r2_ui_press(1));injected_tap=true;
 }
 if(fake_now>=10000)r2_ui_stop();
}
static void ui_schedule_yield(void){
 if(scene>=11){
  /* Actual observed GM item wall bound was27.832ms. Model one28ms
   * peer item at each explicit yield, with no timeslice/idle0 assumption. */
  owner_running_until=fake_now+28;input_main_advance(28);
  ui_schedule_poll();
 }
}
static void ui_schedule_frame(void){
 if(scene==4&&frame_calls==4){
  /* Source changes while public synchronous SPI owns its existing frame.
   * The caller must retire it and suppress an old-generation state commit. */
  ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);spi_invalidated=true;
 }
 if((scene==5||scene==8)&&frame_calls==4)precommit_at=fake_now+26;
}
static void ui_schedule_lock(void){
 if(precommit_at&&fake_now>=precommit_at){
  precommit_at=0;
  if(scene==8)atomic_commit_armed=true;
  else {ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);precommit_invalidated=true;}
 }
 if(history_race==1)history_race=2;
 else if(history_race==2){
  assert(status.holds.hot_since==held_before);history_race=3;history_checked=true;
 }
}
static void ui_schedule_atomic(atomic_t *address,int value){
 (void)value;
 if(address!=&source_hint)return;
 if(atomic_commit_armed){
  atomic_commit_armed=false;deferred_context=1;
 }
 if(history_atomic_armed){
  history_atomic_armed=false;deferred_context=2;
 }
}
static void ui_schedule_unlock(void){
 /* Context publication now shares the mutex: a preempting main writer
  * cannot recursively enter it, and resumes immediately after this release. */
 unsigned pending=deferred_context;if(!pending)return;deferred_context=0;
 ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);
 if(pending==1)atomic_commit_done=true;
 else {history_atomic_done=true;struct r2_ui_status observed;r2_ui_snapshot(&observed);
  assert(!observed.holds.hot_since&&!observed.sync_display.at_ms);history_atomic_checked=true;}
}
static void fixture_holds(struct r2_ui_holds *h,const struct r2_ui_model *m,uint64_t now){
 if((scene==6||scene==9)&&!history_race&&!history_atomic_done&&now>=1200)h->hot_since=0;
 r2_ui_holds_update(h,m,now);
 if((scene==6||scene==9)&&!history_race&&!history_atomic_done&&now>=1200){
  held_before=status.holds.hot_since;assert(h->hot_since!=held_before);
  if(scene==9)history_atomic_armed=true;
  else {ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);history_race=1;}
 }
}
static void fixture_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){
 r2_ui_render(fb,m,s,now);++render_calls;
 if(scene==3&&!render_invalidated&&render_calls==4){
  int before=frame_calls;
  /* Actual WITHDRAW/root change at the same roster index. */
  ctx.flags|=R2_UI_CONTEXT_SWITCH;r2_ui_context_update(&ctx);
  ctx.root=2;ctx.boot=438;ctx.flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE;r2_ui_context_update(&ctx);
  assert(!frame_current(logic_revision,model_context,s->page,control_serial,(unsigned)atomic_get(&source_hint)));
  assert(frame_calls==before);render_invalidated=true;++dropped;
 }
 if(scene==7&&!aba_checked&&render_calls==4){
  uint32_t before=(uint32_t)atomic_get(&source_hint);
  uint64_t loss=fake_now+1;fake_now=loss;r2_ui_source_hint(R2_UI_HINT_KNOWN);
  fake_now+=700;r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
  uint32_t after=(uint32_t)atomic_get(&source_hint);assert((before&15u)==(after&15u)&&before!=after);
  assert(model.ble_valid_until_us>fake_now*1000); /* Old lease is still future. */
  struct r2_ui_model old=model;model_fence(&old,model_context,model_hint);
  assert(!r2_ui_ble_current(&old,fake_now));
  struct r2_ui_sync_display c={.utc_us=100000,.at_ms=loss-1};
  sync_update(&c,&old,fake_now,false,after);assert(!c.at_ms); /* Already>500ms. */
  assert(!frame_current(logic_revision,model_context,s->page,control_serial,before));
  busy_until=fake_now+500;aba_checked=true;
 }
}
int main(int argc,char **argv){
 assert(argc==2);scene=(unsigned)atoi(argv[1]);mode=99;
 assert(!r2_ui_close_result()&&!r2_ui_poweroff_requested());
 for(unsigned bit=0;bit<7;bit++){
  bool idle=bit!=0,switching=bit==1,closing=bit==2,relay=bit==5,takeover=bit==6;
  assert(!r2_ui_work_allowed(idle,switching,closing,bit==3?2:0,bit==4,relay,takeover));
 }
 assert(r2_ui_work_allowed(true,false,false,0,0,false,false));
 assert(r2_ui_work_allowed(true,false,false,1,0,false,false));
 assert(!r2_ui_work_allowed(true,false,false,2,0,false,false));
 /* Exercise the actual extracted main hint expressions: nonselected local
  * GNSS churn cannot invalidate a healthy selected BLE root; roster index alone
  * cannot hide replacement identity/boot. */
 struct hint_service hs={.profile_incarnation=1,.identities={0,4},.root=1,.root_boot=73,
  .source_epoch=1,.precision={.train=1},.profile_rc=true,.active=true,.follower_clock_valid=true};
 struct hint_clock hc={0};main_hint_fixture(hs,hc);
 uint32_t selected=(uint32_t)atomic_get(&context_revision),selected_hint=(uint32_t)atomic_get(&source_hint);
 for(unsigned i=0;i<100;i++){hc.view.source_epoch=i;main_hint_fixture(hs,hc);
  assert((uint32_t)atomic_get(&context_revision)==selected&&(uint32_t)atomic_get(&source_hint)==selected_hint);}
 hs.identities[1]=2;hs.root_boot=438;main_hint_fixture(hs,hc);
 assert((uint32_t)atomic_get(&context_revision)!=selected);
 /* Reset only this synthetic test's UI bookkeeping before cold fixtures. */
 atomic_set(&context_revision,1);atomic_set(&source_hint,0);context_seen=false;
 atomic_set(&source_loss_revision,0);atomic_set(&source_loss_ms,0);
 /* Deferred visual models cannot leave witness selection on retired GM. */
 struct r2_ui_model previous_model=model;struct r2_ui_status previous_status=status;
 model.profile_rc=false;model.scope_active=true;model.role=R2_UI_GM;model_context=0;
 status.page=R2_UI_PRECISION_PAGE;ctx.flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE;
 r2_ui_context_update(&ctx);assert(r2_ui_shadow_requested());
 ctx.flags=R2_UI_CONTEXT_LIVE|R2_UI_CONTEXT_SERVING;r2_ui_context_update(&ctx);assert(!r2_ui_shadow_requested());
 ctx.flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE;
 model=previous_model;status=previous_status;model_context=1;
 atomic_set(&context_revision,1);context_seen=false;
 /* Healthy physical GM04 has legacy association/source_available=false.
  * Only the established physical qualification may withdraw this model. */
 assert(r2_ui_local_source_current(false,true,true,true));
 assert(!r2_ui_local_source_current(false,false,false,false));
 struct r2_ui_model gm={.role=R2_UI_GM,.clock={.valid=true,.source_available=false},.gm_source_fresh=true};
 r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_LOCAL|R2_UI_HINT_LOCAL_VALID);
 uint32_t good=(uint32_t)atomic_get(&source_hint);
 model_fence(&gm,1,good);assert(gm.clock.valid&&gm.gm_source_fresh);
 fake_now=50;r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_LOCAL);
 model_fence(&gm,1,good);assert(!gm.clock.valid&&!gm.gm_source_fresh);
 atomic_set(&source_hint,0);atomic_set(&source_loss_revision,0);fake_now=100;
 r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 uint32_t old_hint=(uint32_t)atomic_get(&source_hint);
 struct r2_ui_model mask=sync_model(100);mask.ble_valid_until_us=999000;mask.clock.valid=true;
 fake_now=101;r2_ui_source_hint(R2_UI_HINT_KNOWN);model_fence(&mask,1,old_hint);
 assert(!r2_ui_ble_current(&mask,101)&&mask.ble_active);
 struct r2_ui_sync_display grace={.utc_us=123456,.at_ms=100};
 uint32_t lost=(uint32_t)atomic_get(&source_hint);
 sync_update(&grace,&mask,300,false,lost);assert(grace.loss_ms==101);
 fake_now=400;r2_ui_source_hint(R2_UI_HINT_KNOWN);assert((uint32_t)atomic_get(&source_hint)==lost);
 sync_update(&grace,&mask,600,false,lost);assert(grace.loss_ms==101);
 sync_update(&grace,&mask,601,false,lost);assert(!grace.at_ms);
 fake_now=700;r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 model_fence(&mask,1,old_hint);assert(!r2_ui_ble_current(&mask,702));
 fake_now=800;r2_ui_source_hint(R2_UI_HINT_KNOWN); /* No new owner projection. */
 assert((uint32_t)atomic_get(&source_loss_ms)==101);
 struct r2_ui_sync_display starved={.utc_us=123456,.at_ms=100};
 sync_update(&starved,&mask,1000,false,(uint32_t)atomic_get(&source_hint));assert(!starved.at_ms);
 /* Fresh RC owner local-HOLD is display-only and keeps its own deadline.
  * Source hint cannot revive a cached pre-withdraw copy or grant analog. */
 struct r2_ui_model saved_model=model;
 uint64_t saved_serial=model_serial;uint32_t saved_context=model_context,saved_hint=model_hint;
 fake_now=800;uint32_t hold_hint=(uint32_t)atomic_get(&source_hint);
 struct r2_ui_model hold=sync_model(800);
 hold.ble_local_holdover=true;hold.ble_valid_until_us=1300000;
 hold.precision.map_valid=hold.precision.authorized=true;
 r2_ui_publish(&hold);struct r2_ui_model held=model;
 model_fence(&held,model_context,model_hint);assert(r2_ui_ble_current(&held,1299));
 assert(r2_ui_analog(&held,1299)); /* Existing helper, not forced by hint. */
 fake_now=1300;model_fence(&held,model_context,model_hint);assert(!r2_ui_ble_current(&held,1300));
 fake_now=900;hold.sampled_ms=900;hold.ble_valid_until_us=1400000;
 r2_ui_publish(&hold);held=model;uint32_t held_context=model_context,held_hint=model_hint;
 r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 r2_ui_source_hint(R2_UI_HINT_KNOWN); /* Same flags, new revision: old lease still future. */
 model_fence(&held,held_context,held_hint);assert(!r2_ui_ble_current(&held,901));
 hold.sampled_ms=1000;hold.ble_valid_until_us=1500000;hold.precision.map_valid=false;
 fake_now=1000;r2_ui_publish(&hold);held=model;hold_hint=model_hint;
 model_fence(&held,model_context,model_hint);
 assert(r2_ui_ble_current(&held,1001)&&!r2_ui_analog(&held,1001));
 assert(!strcmp(r2_ui_servo_state(&held,1001),"ACQUIRING"));
 atomic_set(&source_loss_ms,900);atomic_set(&source_loss_revision,(int32_t)hold_hint);
 struct r2_ui_sync_display old_grace={.utc_us=123456,.at_ms=899};
 sync_update(&old_grace,&held,1001,false,hold_hint);assert(old_grace.loss_ms==900);
 sync_update(&old_grace,&held,1400,false,hold_hint);assert(!old_grace.at_ms);
 held=hold;model_fence(&held,0,hold_hint);assert(!r2_ui_ble_current(&held,1001)); /* Root/context change. */
 held=hold;held.profile_rc=false;model_fence(&held,(uint32_t)atomic_get(&context_revision),hold_hint);
 assert(!r2_ui_ble_current(&held,1001)); /* No GM exemption. */
 model=saved_model;model_serial=saved_serial;model_context=saved_context;model_hint=saved_hint;
#ifdef R2_LORA
 mask.lora=(struct r2_lora_view){.requested=true,.active=true,.context_known=true,
  .choice=RC8_SELECT_LORA,.state=RC8_EXPIRED,.display_held=true,.time_valid=true,
  .utc_us=123456,.display_until_us=UINT64_MAX,.until_us=UINT64_MAX,.sampled_us=100000};
 mask.ble_rx_supported=mask.ble_rx_requested=true;
 strcpy(mask.root_name,"TC-04");strcpy(mask.upstream_name,"TC-04");
 r2_ui_context_update(&ctx);
 /* Hard-fenced live BLE still owns acquisition screen over unqualified HOLD. */
 model_fence(&mask,0,old_hint);
 assert(mask.lora.choice==RC8_SELECT_LORA&&mask.lora.time_valid);
 assert(mask.ble_active&&mask.sync_attempt&&!mask.root_name[0]&&!mask.upstream_name[0]);
 assert(!r2_ui_lora_primary(&mask,700));
 uint8_t actual[R2_UI_FRAME_BYTES],expected[R2_UI_FRAME_BYTES];struct r2_ui_status st={0};
 r2_ui_render(actual,&mask,&st,700);
 struct r2_ui_model ble_only=mask;ble_only.lora=(struct r2_lora_view){0};
 r2_ui_render(expected,&ble_only,&st,700);assert(!memcmp(actual,expected,sizeof actual));
 /* Known independent emitter label survives while retired BLE label did not. */
 mask.upstream_is_lora=true;strcpy(mask.upstream_name,"TC-03");
 model_fence(&mask,0,old_hint);assert(!strcmp(mask.upstream_name,"TC-03")&&!mask.root_name[0]);
 struct r2_ui_model tx={.sampled_ms=700,.platform_ready=true,.unit="TC-03",
  .scope_active=true,.role=R2_UI_GM,.gm_source_fresh=true,.clock={.valid=true,.source_available=true,.utc_us=123456000},
  .lora={.requested=true,.active=true,.transmitter=true,.time_valid=true,.state=RC8_AVAILABLE}};
 r2_ui_render(actual,&tx,&st,700);unsigned blue=0,amber=0;
 for(unsigned i=0;i<sizeof actual;i++){blue+=actual[i]==R2_UI_COLOR_BLUE;amber+=actual[i]==R2_UI_COLOR_AMBER;}
 assert(blue);
 transmitter_age(&tx,951,250);assert(!tx.lora.time_valid&&tx.lora.state==RC8_ACQUIRING);
 r2_ui_render(expected,&tx,&st,951);unsigned after_blue=0,after_amber=0;
 for(unsigned i=0;i<sizeof expected;i++){after_blue+=expected[i]==R2_UI_COLOR_BLUE;after_amber+=expected[i]==R2_UI_COLOR_AMBER;}
 assert(after_blue<blue&&after_amber>amber);
 tx.lora.time_valid=true;tx.lora.state=RC8_AVAILABLE;
 model_fence(&tx,0,old_hint);assert(!tx.lora.time_valid); /* Hard root fence. */
 tx.lora.time_valid=true;tx.lora.state=RC8_AVAILABLE;
 model_fence(&tx,(uint32_t)atomic_get(&context_revision),old_hint);
 assert(!tx.lora.time_valid); /* Soft revision/loss cannot retain old TXblue. */
 ctx.flags|=R2_UI_CONTEXT_SWITCH;r2_ui_context_update(&ctx);model_fence(&mask,0,old_hint);
 assert(mask.lora.choice==RC8_SELECT_NONE&&!mask.lora.time_valid);
 ctx.flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE;
#endif
 atomic_set(&source_hint,0);atomic_set(&source_loss_revision,0);atomic_set(&source_loss_ms,0);atomic_set(&model_requested,0);fake_now=0;
 if(scene==7)r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 if(scene==6||scene==9)ctx.flags=R2_UI_CONTEXT_LIVE;
 r2_ui_context_update(&ctx);produce(0);if(scene==1)r2_ui_page(1);
 r2_ui_start();assert(priority==9&&r2_ui_close_result()==-EAGAIN);
 entry(NULL,NULL,NULL);
 assert(status.stopped&&!r2_ui_close_result()&&frame_calls>20&&builds>15);
 assert(builds>400&&builds<650); /* mandatory50Hz, renderer still4/8Hz */
 if(scene==2)assert(builds_during_busy>90);
 if(scene==2)assert(busy_expiry_seen&&learned_survived);
 if(scene==3)assert(render_invalidated&&dropped==1&&render_calls>(unsigned)frame_calls);
 if(scene==4)assert(spi_invalidated&&spi_checked);
 if(scene==5)assert(precommit_invalidated&&spi_checked);
 if(scene==6)assert(history_checked);
 if(scene==7)assert(aba_checked);
 if(scene==8)assert(atomic_commit_checked);
 if(scene==9)assert(history_atomic_checked);
 if(scene>=11)assert(yield_calls>20);
 if(scene==12)assert(injected_tap&&status.buttons==1&&status.page==1);
 for(unsigned i=1;i<builds;i++){
  assert(produced[i]>produced[i-1]||(i==1&&produced[i]==0&&produced[i-1]==0));
  if(scene<3&&i>1)assert(produced[i]-produced[i-1]>=20);
 }
 puts("PASS mandatory logical refresh despite busy owner, optional coalesced frames, original validity/TZ/source/control fences");
}
