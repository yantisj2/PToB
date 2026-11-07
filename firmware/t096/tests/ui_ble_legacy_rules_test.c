/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual renderer + exact pre-LoRa BLE policy oracle. No timing substitution. */
#include "ui.h"
#include "ui_rgb565_fixture.h"
#include "ui_ble_pre_lora_oracle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t before[R2_UI_BYTES],after[R2_UI_BYTES],panel[R2_UI_BYTES];
static void image(const char *dir,const char *name,const uint8_t *fb){
 char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<R2_UI_BYTES;i+=2){unsigned p=((unsigned)fb[i]<<8)|fb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}assert(!fclose(f));
}
static struct r2_ui_model good(uint64_t now){
 return(struct r2_ui_model){.unit="TC-02",.sampled_ms=now,.platform_ready=true,.bt_ready=true,
 .profile_supported=true,.profile_rc=true,.boundary_clock=true,.ble_active=true,
 .role=R2_UI_FOLLOWER,.scope_active=true,.authenticated_gm=true,.ble_time_selected=true,
 .ble_valid_until_us=(now+1)*1000,.clock={.valid=true,.utc_us=1792340000000000LL},
 .follower_pps_valid=true,.face_lock_until_us=(now+500)*1000,.face_rms_ns=100,
 .precision={.map_valid=true,.authorized=true,.locked=true}};
}
static void lost_cached(struct r2_ui_model *m,uint64_t now){
 m->sampled_ms=now;m->scope_active=m->authenticated_gm=false;m->role=R2_UI_NO_ROLE;
 m->precision.map_valid=m->precision.authorized=m->precision.locked=false;
 m->ble_local_holdover=true;m->ble_time_selected=true;m->ble_valid_until_us=(now+500)*1000;
 m->clock.valid=true;m->clock.utc_us=1792340000000000LL+(int64_t)(now-1000)*1000;m->sync_attempt=true;
}
int main(int argc,char **argv){
 assert(argc==2||argc==3);struct r2_ui_model m=good(1000);lost_cached(&m,1020);
 if(argc==3){assert(r2_ui_analog(&m,1020)&&!pre_lora_r2_ui_analog(&m,1020));puts("PASS baseline306 actual120s-hold analog override differs from original BLE oracle");return 0;}
 assert(!r2_ui_analog(&m,1020));
 /* All old BLE branches, including GM/nonboundary cached-face rules, remain
  * exact. No new RMS200/PLL/transport criterion is introduced. */
 for(unsigned kind=0;kind<3;kind++)for(unsigned v=0;v<14;v++){
  m=good(3000);if(kind==1){m.boundary_clock=false;m.profile_rc=false;}if(kind==2){m.role=R2_UI_GM;m.clock.source_available=true;}
  if(v==1)m.scope_active=false;if(v==2)m.precision.map_valid=false;if(v==3)m.clock.valid=false;
  if(v==4)m.ble_valid_until_us=3000000;if(v==5)m.sampled_ms=0;if(v==6)m.authenticated_gm=false;
  if(v==7)m.follower_pps_valid=false;if(v==8)m.face_rms_ns=201;if(v==9)m.face_lock_until_us=3000000;
  if(v==10)m.clock.source_available=false;if(v==11)m.clock.utc_us=-1;if(v==12)m.ble_local_holdover=true;
  if(v==13){m.ble_local_holdover=true;m.scope_active=false;m.precision.map_valid=false;}
  assert(r2_ui_ble_current(&m,3000)==pre_lora_r2_ui_ble_current(&m,3000));
  assert(r2_ui_analog(&m,3000)==pre_lora_r2_ui_analog(&m,3000));
  assert(r2_ui_face_locked(&m,3000)==pre_lora_r2_ui_face_locked(&m,3000));
 }
 m=good(1000);m.precision.rms_ns=900;m.precision.locked=false;assert(r2_ui_analog(&m,1000));
 struct r2_ui_sync_display current={0},oracle={0};r2_ui_sync_display_update(&current,&m,1000,true);pre_lora_r2_ui_sync_display_update(&oracle,&m,1000,true);
 for(uint64_t now=1020;now<=1600;now++){
  lost_cached(&m,now);r2_ui_sync_display_update(&current,&m,now,false);pre_lora_r2_ui_sync_display_update(&oracle,&m,now,false);
  assert(!memcmp(&current,&oracle,sizeof current));int64_t a=0,b=0;
  assert(r2_ui_sync_display_time(&current,&m,now,&a)==pre_lora_r2_ui_sync_display_time(&oracle,&m,now,&b));assert(a==b);
  if(now<1520)assert(current.loss_ms==1020);else assert(!current.at_ms);
  if(now==1519){struct r2_ui_status st={.sync_display=current};r2_test_render_rgb565(before,&m,&st,now);st.sync_display=(struct r2_ui_sync_display){0};r2_test_render_rgb565(panel,&m,&st,now);assert(memcmp(before,panel,sizeof before));image(argv[1],"BLE-499ms-original-flicker-hold",before);}
  if(now==1520){struct r2_ui_status st={.sync_display=current};r2_test_render_rgb565(after,&m,&st,now);st.sync_display=(struct r2_ui_sync_display){0};r2_test_render_rgb565(panel,&m,&st,now);assert(!memcmp(after,panel,sizeof after));assert(!r2_ui_face_locked(&m,now));image(argv[1],"BLE-500ms-original-error-panel",after);}
 }
 /* Long-running cached BLE UTC cannot restart the one original loss deadline. */
 for(uint64_t now=2000;now<120000;now+=997){lost_cached(&m,now);r2_ui_sync_display_update(&current,&m,now,false);assert(!current.at_ms&&!r2_ui_analog(&m,now));}
#ifdef R2_LORA
 m=(struct r2_ui_model){.unit="TC-02",.sampled_ms=100000,.platform_ready=true,.ble_rx_supported=true,.ble_rx_requested=false,
  .lora={.requested=true,.active=true,.choice=RC8_SELECT_NONE,.state=RC8_EXPIRED,.display_held=true,
  .sampled_us=100000000,.utc_us=1792340000000000ULL,.display_until_us=UINT64_MAX,.rx_age_ms=90000}};
 assert(r2_ui_lora_primary(&m,100000)&&r2_ui_analog(&m,100000));struct r2_ui_status st={0};r2_test_render_rgb565(after,&m,&st,100000);image(argv[1],"LoRa-existing-90s-HOLD-unchanged",after);
 m.lora.requested=false;assert(!r2_ui_analog(&m,100000));r2_test_render_rgb565(panel,&m,&st,100000);assert(memcmp(after,panel,sizeof after));
#endif
 puts("PASS exactpreLoRa42BLEboundarycases +unchanged500ms anti-flicker 499/500 actualpixels +cached120s no renewal +existingLoRa90s HOLD; no owner changes");return 0;
}
