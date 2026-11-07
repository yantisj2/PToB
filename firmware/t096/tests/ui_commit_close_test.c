/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual worker: main preemption, explicit stop and release proof boundary. */
#define r2_display_init original_init
#define r2_display_frame original_frame
#define r2_display_recover original_recover
#define r2_display_off original_off
#define r2_display_button original_button
#include UI_FAKE_HEADER
#undef r2_display_init
#undef r2_display_frame
#undef r2_display_recover
#undef r2_display_off
#undef r2_display_button
#include <stdio.h>
#include <stdlib.h>
static unsigned scenario,recover_calls;static bool armed,in_callback,injected;
static uint64_t commit_at,frame_end;static void publish_current(uint64_t);
int r2_display_init(void){int rc=original_init();return scenario==2?-EIO:rc;}
int r2_display_button(void){return scenario==3?-ENODEV:0;}
int r2_display_frame(const uint8_t *fb){
 ++frame_calls;if(scenario==0){armed=true;commit_at=fake_now+26;}
 input_main_advance(26);
 if(scenario==1||scenario==4||scenario==5||scenario==7){
  memcpy(retained,fb,sizeof retained);retained_fb=fb;return -ETIMEDOUT;
 }
 return 0;
}
int r2_display_recover(void){++recover_calls;return scenario==4?-EIO:scenario==7?-EAGAIN:0;}
int r2_display_off(void){++off_calls;if(retained_fb)buffer_changed|=memcmp(retained,retained_fb,sizeof retained)!=0;return scenario==5?-EACCES:scenario==6?-EIO:0;}
#include UI_SERVICE_SOURCE
static void publish_current(uint64_t at){
 struct r2_ui_model m={.sampled_ms=at,.platform_ready=true,.profile_supported=true,
  .profile_rc=true,.boundary_clock=true,.ble_active=true,.scope_active=true,
  .role=R2_UI_FOLLOWER,.authenticated_gm=true,.ble_time_selected=true,
  .ble_valid_until_us=(at+5000)*1000,.clock={.valid=true,.utc_us=1793972200000000LL+(int64_t)at*1000},
  .precision={.authorized=true,.locked=true,.map_valid=true}};r2_ui_publish(&m);
}
static void ui_schedule_atomic(atomic_t *v,int value){(void)v;(void)value;}
static void ui_schedule_unlock(void){}
static void ui_schedule_yield(void){}
static void ui_schedule_frame(void){}
static void ui_schedule_lock(void){
 if(armed&&!in_callback&&fake_now>=commit_at){
  armed=false;in_callback=true;frame_end=fake_now;++fake_now;
  publish_current(fake_now);injected=true;in_callback=false;
 }
}
static void ui_schedule_poll(void){
 if(scenario==2){if(fake_now>=1000)r2_ui_stop();}
 else if(scenario!=3&&frame_calls)r2_ui_stop();
}
int main(int argc,char **argv){
 assert(argc==2);scenario=(unsigned)atoi(argv[1]);mode=99;
 struct r2_ui_context ctx={.incarnation=1,.root=4,.boot=73,.source_epoch=13,.train=13,
  .flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE};
 r2_ui_context_update(&ctx);r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 publish_current(0);r2_ui_start();entry(NULL,NULL,NULL);
 assert(status.stopped&&!status.ready&&off_calls==1&&!buffer_changed);
 if(scenario==0){
  struct r2_ui_status observed;r2_ui_snapshot(&observed);
  assert(injected&&status.frames==1&&status.analog&&status.drawn_ble&&status.last_frame_ms==frame_end);
  assert(model.sampled_ms>frame_end&&(observed.logic_flags&R2_UI_LOGIC_VALID));
#ifdef R2_BASELINE319
  assert(!observed.drawn_time&&!analog_ack.pending);
#else
  assert(observed.drawn_time&&analog_ack.pending&&analog_ack.at_ms==model.sampled_ms&&analog_ack.completed_ms==frame_end);
#endif
 }else if(scenario==2){assert(!frame_calls&&r2_ui_close_result()==-EIO&&status.error==-EIO);}
 else if(scenario==3){assert(r2_ui_close_result()==-ENODEV&&status.error==-ENODEV);}
 else {
  assert(frame_calls==1);
#ifdef R2_BASELINE319
  assert(scenario==1&&r2_ui_close_result()==-ETIMEDOUT&&recover_calls==0);
#else
  int expected=scenario==4?-EIO:scenario==5?-EACCES:scenario==6?-EIO:scenario==7?-EAGAIN:0;
  assert(r2_ui_close_result()==expected);
  assert(recover_calls==(scenario==6?0:1));
  assert(status.error==(scenario==6?-EIO:-ETIMEDOUT)); /* draw evidence retained */
#endif
 }
 printf("PASS %s scenario%u frames%u close%d draw_error%d recover%u\n",
#ifdef R2_BASELINE319
  "319_NEGATIVE",
#else
  "320",
#endif
 scenario,frame_calls,r2_ui_close_result(),status.error,recover_calls);
}
