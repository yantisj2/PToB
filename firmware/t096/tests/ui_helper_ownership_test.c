/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include UI_RENDER_SOURCE
void baseline_r2_ui_uptime_text(char[16],uint64_t);
void baseline_r2_ui_identity_name(char[25],uint64_t,const char *);
const char *baseline_r2_ui_reset_text(unsigned);
void baseline_r2_ui_holds_update(struct r2_ui_holds *,const struct r2_ui_model *,uint64_t);
bool baseline_r2_ui_analog(const struct r2_ui_model *,uint64_t);
bool baseline_r2_ui_face_locked(const struct r2_ui_model *,uint64_t);
bool baseline_r2_ui_saver_allowed(const struct r2_ui_model *,uint64_t);
bool baseline_r2_ui_display_time(const struct r2_ui_model *,uint64_t,int64_t *,bool *);
bool baseline_r2_ui_ble_current(const struct r2_ui_model *,uint64_t);
const char *baseline_r2_ui_servo_state(const struct r2_ui_model *,uint64_t);
void baseline_r2_ui_rf_lamps(uint8_t[8],const struct r2_ui_model *,uint64_t);
void baseline_ui_format_count(char[8],uint64_t);
void baseline_ui_format_ppb(char[16],uint32_t);
bool baseline_ui_full_frame_phase_safe(bool,uint64_t,bool);
ui_pulse_t baseline_ui_pps_pulse(bool,uint32_t,bool);
static unsigned called;
static bool forbidden(void *p,enum r2_ui_render_stage stage){(void)p;(void)stage;++called;assert(false);return false;}
int main(void){
 for(unsigned state=0;state<8;state++)for(unsigned sample=0;sample<64;sample++){
  struct r2_ui_model m={.sampled_ms=1000,.profile_supported=true,.platform_ready=true,
   .profile_rc=sample&1,.boundary_clock=sample&1,.scope_active=true,.ble_active=true,
   .role=sample%3,.authenticated_gm=true,.gm_source_fresh=true,.follower_pps_valid=true,
   .ble_time_selected=true,.ble_valid_until_us=1500000,
   .clock={.valid=true,.utc_us=1792340000000000LL,.pps_present=true,.pps_age_us=sample*5000},
   .precision={.authorized=true,.map_valid=true,.locked=true,.rms_valid=true}};
  uint64_t now=1000+sample*50;
  memset(&ui_raster,0,sizeof ui_raster);ui_raster.active=state&1;ui_raster.aborted=state&2;
  ui_raster.critical=state&4;ui_raster.checkpoint=forbidden;ui_raster.context=&m;ui_raster.remaining=1;
  unsigned char saved[sizeof ui_raster];memcpy(saved,&ui_raster,sizeof saved);
  char a[25],b[25];memset(a,0xa5,sizeof a);memset(b,0xa5,sizeof b);
  uint64_t value=sample==63?UINT64_MAX:sample*UINT64_C(1000000000000);
  r2_ui_uptime_text(a,value);baseline_r2_ui_uptime_text(b,value);assert(!memcmp(a,b,sizeof a));
  r2_ui_identity_name(a,value,sample&1?"TC-04":NULL);baseline_r2_ui_identity_name(b,value,sample&1?"TC-04":NULL);assert(!memcmp(a,b,sizeof a));
  assert(!strcmp(r2_ui_reset_text(sample),baseline_r2_ui_reset_text(sample)));
  struct r2_ui_holds ha={0},hb={0};r2_ui_holds_update(&ha,&m,now);baseline_r2_ui_holds_update(&hb,&m,now);assert(!memcmp(&ha,&hb,sizeof ha));
  assert(r2_ui_analog(&m,now)==baseline_r2_ui_analog(&m,now));
  assert(r2_ui_face_locked(&m,now)==baseline_r2_ui_face_locked(&m,now));
  assert(r2_ui_saver_allowed(&m,now)==baseline_r2_ui_saver_allowed(&m,now));
  assert(r2_ui_ble_current(&m,now)==baseline_r2_ui_ble_current(&m,now));
  assert(!strcmp(r2_ui_servo_state(&m,now),baseline_r2_ui_servo_state(&m,now)));
  int64_t ta=0,tb=0;bool ca=false,cb=false;
  assert(r2_ui_display_time(&m,now,&ta,&ca)==baseline_r2_ui_display_time(&m,now,&tb,&cb));assert(ta==tb&&ca==cb);
  uint8_t la[8],lb[8];r2_ui_rf_lamps(la,&m,now);baseline_r2_ui_rf_lamps(lb,&m,now);assert(!memcmp(la,lb,8));
  ui_format_count(a,value);baseline_ui_format_count(b,value);assert(!strcmp(a,b));
  ui_format_ppb(a,(uint32_t)value);baseline_ui_format_ppb(b,(uint32_t)value);assert(!strcmp(a,b));
  assert(ui_full_frame_phase_safe(sample&1,value,sample&2)==baseline_ui_full_frame_phase_safe(sample&1,value,sample&2));
  assert(ui_pps_pulse(sample&1,(uint32_t)value,sample&2)==baseline_ui_pps_pulse(sample&1,(uint32_t)value,sample&2));
  (void)r2_ui_menu_enabled(&m);(void)r2_ui_witness_page(&m,sample);(void)r2_ui_local_pulse(&m,now);
  struct r2_ui_sync_display d={0};r2_ui_sync_display_update(&d,&m,now,false);(void)r2_ui_sync_display_time(&d,&m,now,&ta);
  struct r2_link_quality_view quality={0};r2_ui_reception(&m,&quality,now);
  r2_ui_select_ble_time(&m,value,now*1000,now*1000,now*1000+1000000,true);
  assert(!called&&!memcmp(saved,&ui_raster,sizeof saved));
 }
 puts("PASS512 active/aborted/critical helper states: exact scalar outputs, no callback or raster mutation");
}
