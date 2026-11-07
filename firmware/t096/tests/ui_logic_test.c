/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production publication/worker, synthetic independent owner/main scheduling. */
#include <stdio.h>
#include <stdlib.h>
static void logic_render(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,uint64_t);
#define r2_ui_render logic_render
#ifndef UI_SERVICE_SOURCE
#define UI_SERVICE_SOURCE "../src/ui_service.c"
#endif
#include UI_SERVICE_SOURCE
#undef r2_ui_render
static unsigned scene,publications;
static bool withdrawn,injected;
#ifndef R2_LOGIC_BASELINE
static bool observed_grace,observed_expired;
#endif
static uint64_t actual_loss,expired_grace_end,next_grace_draw;
static struct r2_ui_context ctx={.incarnation=1,.root=4,.boot=73,.source_epoch=1,.train=1,
 .flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE};
static struct r2_ui_model owner_model(uint64_t now){
 struct r2_ui_model m=sync_model(now);
 m.sampled_ms=now;m.clock.valid=m.scope_active=m.authenticated_gm=m.ble_time_selected=!withdrawn;
 m.clock.utc_us=1792340000000000LL+(int64_t)now*1000;
 m.ble_active=true;m.role=withdrawn?R2_UI_NO_ROLE:R2_UI_FOLLOWER;
 m.precision.map_valid=m.precision.authorized=m.precision.locked=!withdrawn;
 m.ble_valid_until_us=(now+(scene==2?21:500))*1000;
 m.clock.pps_present=true;m.clock.pps_age_us=0;
 m.profile_rc=scene!=10;m.boundary_clock=m.profile_rc;
 m.local_hot_standby=scene==0;
#ifdef R2_TZ_WIRE
 m.tz=(struct r2_tz){2,-300};m.tz_until_us=(now+5000)*1000;
#endif
#ifdef R2_LORA
 if(scene==9){m.lora=(struct r2_lora_view){.sampled_us=now*1000,.active=true,.requested=true,
  .choice=RC8_SELECT_LORA,.state=RC8_EXPIRED,.display_held=true,.display_until_us=(now+500)*1000,
  .utc_us=1792340000000000ULL+now*1000};}
#endif
 return m;
}
static void maintain(void){
 struct r2_ui_model m=owner_model(fake_now);
#ifdef R2_LOGIC_BASELINE
 if(!r2_ui_work_allowed(true,false,false,2,0,false,false)||!r2_ui_model_requested(fake_now))return;
 r2_ui_publish(&m);++publications;
#else
 if(!r2_ui_logic_due(fake_now,m.ble_valid_until_us,!withdrawn))return;
 r2_ui_publish_owned(&m,0,NULL);++publications;
 struct r2_ui_status out;r2_ui_snapshot(&out);
 assert(out.logic_age_ms==0);
 if(!withdrawn&&scene!=14)assert(out.logic_flags&R2_UI_LOGIC_VALID);
 if(withdrawn&&out.logic_flags&R2_UI_LOGIC_GRACE)observed_grace=true;
 if(withdrawn&&actual_loss&&fake_now>=actual_loss+500){
  assert(!(out.logic_flags&(R2_UI_LOGIC_VALID|R2_UI_LOGIC_GRACE)));observed_expired=true;
 }
#endif
}
static void ui_logic_main(void){maintain();}
static void advance_logic(unsigned ms){
 uint64_t end=fake_now+ms;
 while(fake_now<end){input_main_advance(end-fake_now>1?1:end-fake_now);maintain();}
}
static void lose(void){
 withdrawn=true;actual_loss=fake_now;r2_ui_source_hint(R2_UI_HINT_KNOWN);maintain();
}
static void ui_schedule_atomic(atomic_t *a,int v){(void)a;(void)v;}
static void ui_schedule_lock(void){}
static void ui_schedule_unlock(void){}
static void ui_schedule_yield(void){maintain();}
static void ui_schedule_poll(void){maintain();if(scene==19&&!withdrawn&&fake_now>=1000&&status.sync_display.at_ms)lose();if(fake_now>=5000)r2_ui_stop();}
static void ui_schedule_frame(void){
 if(scene==19&&actual_loss&&fake_now<actual_loss+500){
  /* Complete after the old deadline while main clears its logical cache. */
  unsigned delay=(unsigned)(actual_loss+505-fake_now);advance_logic(delay);
  expired_grace_end=fake_now+26;
 }
 if(scene==3&&!injected){lose();injected=true;}
 if(scene!=19)advance_logic(300);
}
static void logic_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){
 if(scene==19&&expired_grace_end&&!next_grace_draw)next_grace_draw=fake_now;
 r2_ui_render(fb,m,s,now);
 if(scene==4&&!injected){
  unsigned old=(unsigned)atomic_get(&source_hint);
  r2_ui_source_hint(R2_UI_HINT_KNOWN);r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
  assert(old!=(unsigned)atomic_get(&source_hint));injected=true;
 }
 if(scene==5&&!injected){ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);injected=true;}
 if(scene==6&&!injected){assert(!r2_ui_page(1));injected=true;}
 if(scene!=19)advance_logic(300);
}
int main(int argc,char **argv){
 assert(argc==2);scene=(unsigned)atoi(argv[1]);mode=99;
 r2_ui_context_update(&ctx);r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 struct r2_ui_model initial=owner_model(0);r2_ui_publish(&initial);
 if(scene==0){
  advance_logic(2000);
#ifdef R2_LOGIC_BASELINE
  assert(!publications&&model.sampled_ms==0&&!r2_ui_ble_current(&model,fake_now));
  puts("EXPECTED318_NEGATIVE: owner healthy while renderer admission blocks logical clock");
#else
  assert(publications>=100&&model.sampled_ms>=1980&&r2_ui_ble_current(&model,fake_now));
  assert(status.holds.pps_since);assert(!status.sync_display.at_ms); /* no fabricated draw */
#endif
  return 0;
 }
#ifndef R2_LOGIC_BASELINE
 if(scene==7){
  advance_logic(100);uint32_t policy=logic_revision;logic_revision=UINT32_MAX;
  assert(!frame_current(UINT32_MAX,model_context,0,control_serial,(unsigned)atomic_get(&source_hint)));
  logic_revision=policy;return 0;
 }
 if(scene==8){
  /* No LCD initialization/worker exists: actual loss expires independently. */
  advance_logic(1000);lose();advance_logic(700);
  assert(observed_expired&&!status.sync_display.at_ms&&!frame_calls);return 0;
 }
 if(scene==12){
  advance_logic(100);uint64_t until=model.ble_valid_until_us;
  /* A renewed near-expiry owner wakes logic before the next fixed tick. */
  fake_now=until/1000-1;logic_due_ms=fake_now+19;
  assert(r2_ui_logic_due(fake_now,until+500000,true));
  struct r2_ui_model m=owner_model(fake_now);r2_ui_publish(&m);
  assert(r2_ui_ble_current(&model,fake_now));return 0;
 }
 if(scene==13){
  /* Owner-projected RC local HOLD is digital only after map loss. */
  r2_ui_source_hint(R2_UI_HINT_KNOWN);
  struct r2_ui_model m=owner_model(0);m.ble_local_holdover=true;
  m.precision.map_valid=false;m.ble_valid_until_us=500000;
  r2_ui_publish(&m);fake_now=400;
  struct r2_ui_status out;r2_ui_snapshot(&out);
  assert(out.logic_flags&R2_UI_LOGIC_VALID);assert(!r2_ui_analog(&model,fake_now));
  fake_now=501;r2_ui_snapshot(&out);assert(!(out.logic_flags&R2_UI_LOGIC_VALID));
  assert(!status.sync_display.at_ms);return 0;
 }
 if(scene==14){
  /* Fresh producer data cannot defeat a global maintenance/stop fence. */
  ctx.flags|=R2_UI_CONTEXT_STOP;r2_ui_context_update(&ctx);
  advance_logic(100);struct r2_ui_status out;r2_ui_snapshot(&out);
  assert(!(out.logic_flags&R2_UI_LOGIC_VALID));return 0;
 }
 if(scene==18){
  struct r2_ui_model m=owner_model(100);m.clock.pps_present=false;
  m.pps_precision.epoch=1;m.pps_precision.age_us=100;fake_now=100;r2_ui_publish(&m);
  assert(status.holds.pps_since);uint32_t since=status.holds.pps_since;
  m.sampled_ms=1000;fake_now=1000;r2_ui_publish(&m);assert(status.holds.pps_since==since);
  m.sampled_ms=2000;m.pps_precision.age_us=R2_PPS_FRESH_US;fake_now=2000;r2_ui_publish(&m);
  assert(!status.holds.pps_since&&!frame_calls);return 0;
 }
 if(scene==17){
  advance_logic(100);
  analog_ack.utc_us=model.clock.utc_us;analog_ack.at_ms=analog_ack.completed_ms=fake_now;
  analog_ack.context=model_context;analog_ack.hint=model_hint;analog_ack.policy=logic_revision;analog_ack.pending=true;
  r2_ui_source_hint(R2_UI_HINT_KNOWN);r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
  advance_logic(20);assert(!status.sync_display.at_ms);return 0;
 }
 if(scene==11){
  /* Genuine unchanged owner expiry never renews from sampled_ms. */
  struct r2_ui_model stale=owner_model(0);stale.ble_valid_until_us=100000;
  for(fake_now=0;fake_now<800;fake_now+=20){stale.sampled_ms=fake_now;r2_ui_publish(&stale);}
  struct r2_ui_status out;r2_ui_snapshot(&out);assert(!(out.logic_flags&R2_UI_LOGIC_VALID));return 0;
 }
 if(scene==15)mode=59;
 if(scene==16)mode=58;
 r2_ui_start();entry(NULL,NULL,NULL);
 if(scene==15||scene==16){struct r2_ui_status live;r2_ui_snapshot(&live);assert(live.logic_flags&R2_UI_LOGIC_VALID);assert(publications>100&&!status.sync_display.at_ms);return 0;}
 assert(frame_calls&&status.frames&&status.logic_flags==0); /* snapshot-only metadata */
 struct r2_ui_status out;r2_ui_snapshot(&out);
 if(scene==1||scene==2){
  assert(status.frames>=3&&out.drawn_time); /* benign refresh cannot starve acknowledgment */
  assert(status.sync_display.at_ms); /* current owned projection seeds real analog ACK */
  atomic_set(&stop_requested,0);lose();advance_logic(300);assert(observed_grace);
  advance_logic(300);assert(observed_expired);
 }
 if(scene==19){assert(expired_grace_end&&next_grace_draw);assert(next_grace_draw-expired_grace_end<=20);}
 if(scene==3)assert(injected&&!out.drawn_time&&!out.analog);
 if(scene==4||scene==5||scene==6)assert(injected&&status.missed);
 printf("PASS logic scene %u publications=%u frames=%u\n",scene,publications,status.frames);
#endif
 return 0;
}
