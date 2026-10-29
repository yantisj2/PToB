/* Copyright (c) 2026 Jonathan Yantis. MIT. Offline production UI checks. */
#include "../src/ui_render.c"
#include "ui_rgb565_fixture.h"
#include <assert.h>
static uint8_t fb[R2_UI_BYTES],locked_fb[R2_UI_BYTES];
static unsigned color_count(int y,int height,uint16_t color){
 unsigned count=0;
 for(int row=y;row<y+height;row++)for(unsigned x=0;x<80;x++){
  unsigned i=2*((unsigned)row*80+x);count+=(((unsigned)fb[i]<<8)|fb[i+1])==color;
 }
 return count;
}
static void fixture(const char *dir,const char *name,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){
 r2_test_render_rgb565(fb,m,s,now);char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof fb;i+=2){unsigned p=((unsigned)fb[i]<<8)|fb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.profile_supported=true,.platform_ready=true,.ble_active=true,
  .scope_active=true,.authenticated_gm=true,.ble_time_selected=true,.ble_valid_until_us=63000000,.role=R2_UI_FOLLOWER,.gm_capable=true,.local_hot_standby=true,
  .clock={.pps_present=true,.pps_age_us=0,.fix_active=true,.sats_used=22},
  .precision={.authorized=true,.map_valid=true,.locked=true,.rms_valid=true,.rms_ns=137}};
 struct r2_ui_holds h={0};r2_ui_holds_update(&h,&m,1000);assert(h.pps_since==2&&h.hot_since==2);
 m.sampled_ms=62000;r2_ui_holds_update(&h,&m,62000);assert(h.pps_since==2&&h.hot_since==2);
 char text[24];hold_text(text,"PPS",h.pps_since,62000);assert(!strcmp(text,"PPS 01:01"));
 m.clock.pps_present=false;r2_ui_holds_update(&h,&m,62000);assert(!h.pps_since&&h.hot_since==2);
 m.clock.pps_present=true;m.local_hot_standby=false;r2_ui_holds_update(&h,&m,62000);assert(h.pps_since==63&&!h.hot_since);
 m.local_hot_standby=true;r2_ui_holds_update(&h,&m,62000);assert(h.hot_since==63);
 r2_ui_holds_update(&h,&m,65000);assert(!h.pps_since&&!h.hot_since); /* stale model */
 m.sampled_ms=62000;m.clock.pps_age_us=R2_PPS_FRESH_US-1000;
 r2_ui_holds_update(&h,&m,62001);assert(!h.pps_since); /* age advances between models */
 m.clock.pps_age_us=0;m.profile_rc=true;r2_ui_holds_update(&h,&m,62000);assert(!h.hot_since);
 assert(r2_ui_witness_page(&m,2));m.profile_rc=false;assert(r2_ui_witness_page(&m,2));
 assert(r2_ui_witness_page(&m,6)&&!r2_ui_witness_page(&m,0));
 /* Role duration is distinct from PPS/HOT and survives repeated models. */
 m.role=R2_UI_GM;r2_ui_holds_update(&h,&m,62000);assert(h.active_since==63&&!h.hot_since);
 m.sampled_ms=123000;r2_ui_holds_update(&h,&m,123000);assert(h.active_since==63);
 hold_text(text,"ACTIVE",h.active_since,123000);assert(!strcmp(text,"ACTIVE 01:01"));
 m.role=R2_UI_FOLLOWER;r2_ui_holds_update(&h,&m,123000);assert(!h.active_since);
 m.role=R2_UI_GM;r2_ui_holds_update(&h,&m,123000);assert(h.active_since==124);
 m.scope_active=false;r2_ui_holds_update(&h,&m,123000);assert(!h.active_since);
 m.scope_active=true;r2_ui_holds_update(&h,&m,123000);assert(h.active_since==124);
 m.profile_switching=true;r2_ui_holds_update(&h,&m,123000);assert(!h.active_since);
 m.profile_switching=false;m.profile_rc=true;r2_ui_holds_update(&h,&m,123000);assert(!h.active_since);
 m.profile_rc=false;r2_ui_holds_update(&h,&m,123000);assert(h.active_since==124);
 r2_ui_holds_update(&h,&m,126000);assert(!h.active_since);
 m.role=R2_UI_FOLLOWER;m.sampled_ms=62000;
 struct r2_ui_status s={.page=2,.holds={.pps_since=2,.hot_since=32,.active_since=2},
  .shadow={.state=R2_SHADOW_RUNNING,.active=true,.valid=true,.mean_tenth_ns=-184,.rms_ns=63,.samples=436}};
 fixture(argv[1],"precision-hot",&m,&s,62000);
 assert(color_count(20,7,R2_UI_RGB565_GREEN)&&!color_count(20,7,R2_UI_RGB565_AMBER));
 assert(color_count(32,10,R2_UI_RGB565_GREEN)&&!color_count(32,10,R2_UI_RGB565_AMBER));
 assert(color_count(64,7,R2_UI_RGB565_CYAN)&&!color_count(92,7,R2_UI_RGB565_CYAN));
 /* Precision-page durations use the existing holds for HOT and every GM role. */
 uint8_t pps_duration[80*2*7],hot_duration[80*2*7];
 memcpy(pps_duration,fb+109*80*2,sizeof pps_duration);memcpy(hot_duration,fb+128*80*2,sizeof hot_duration);
 s.holds.pps_since=62;s.holds.hot_since=62;
 fixture(argv[1],"precision-hot-timers-reset",&m,&s,62000);
 assert(memcmp(pps_duration,fb+109*80*2,sizeof pps_duration));
 assert(memcmp(hot_duration,fb+128*80*2,sizeof hot_duration));
 s.holds.pps_since=2;s.holds.hot_since=32;
 fixture(argv[1],"precision-hot-timers-restored",&m,&s,62000);
 m.local_hot_standby=false;m.gm_cold=true;
 fixture(argv[1],"precision-cold-pps-timer",&m,&s,62000);
 assert(!memcmp(pps_duration,fb+109*80*2,sizeof pps_duration));
 m.gm_cold=false;m.local_hot_standby=true;
 fixture(argv[1],"precision-hot",&m,&s,62000);
 memcpy(locked_fb,fb,sizeof fb);
 m.precision.locked=false; /* HOT remains eligible across short fit-lock changes. */
 fixture(argv[1],"precision-hot-fit-unlocked",&m,&s,62000);
 assert(!memcmp(locked_fb,fb,80*2*44));
#if defined(R2_GM7_NATIVE)
 /* Functional GM7 HOT and face quality do not manufacture strict SoftPLL. */
 m.gm7_owner=true;
 fixture(argv[1],"precision-gm7-hot-unlocked",&m,&s,62000);
 uint8_t unlocked_label[80*2*12];memcpy(unlocked_label,fb+32*80*2,sizeof unlocked_label);
 assert(!color_count(32,10,R2_UI_RGB565_GREEN));
 assert(color_count(128,7,R2_UI_RGB565_GREEN));
 m.face_lock_until_us=63000000;m.face_rms_ns=137;
 m.clock.valid=true;m.clock.utc_us=36642000000LL;
 assert(r2_ui_face_locked(&m,62000));
 fixture(argv[1],"precision-gm7-hot-face-only",&m,&s,62000);
 assert(!color_count(32,10,R2_UI_RGB565_GREEN));
 memcpy(unlocked_label,fb+32*80*2,sizeof unlocked_label);
 m.local_hot_standby=false;fixture(argv[1],"precision-gm7-cold-unlocked",&m,&s,62000);
 assert(!memcmp(unlocked_label,fb+32*80*2,sizeof unlocked_label));
 m.local_hot_standby=true;m.precision.locked=true;
 fixture(argv[1],"precision-gm7-hot-locked",&m,&s,62000);
 assert(memcmp(unlocked_label,fb+32*80*2,sizeof unlocked_label)&&color_count(32,10,R2_UI_RGB565_GREEN));
 m.gm7_owner=false;m.precision.locked=false;m.face_lock_until_us=0;m.clock.valid=false;
#endif
 s.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_SETTLING,.settling_ms=5000};
 fixture(argv[1],"precision-settling",&m,&s,62000);assert(color_count(64,7,R2_UI_RGB565_CYAN)); /* stream RMS is independent of held GM shadow */
 s.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_RUNNING,.active=true,.valid=true,.mean_tenth_ns=-5000000000LL,.rms_ns=500000000,.samples=UINT32_MAX};
 fixture(argv[1],"precision-large",&m,&s,62000);
 m.clock.fix_active=m.clock.pps_present=false;m.local_hot_standby=false;
 fixture(argv[1],"precision-no-gps",&m,&s,62000);assert(color_count(64,7,R2_UI_RGB565_CYAN)); /* local GPS is not required for received timing */
 fixture(argv[1],"precision-stale",&m,&s,65001);assert(!color_count(20,24,R2_UI_RGB565_GREEN));
 m.role=R2_UI_GM;m.clock.fix_active=m.clock.pps_present=m.gm_source_fresh=true;m.pps_precision.rate_valid=true;
 m.precision.rms_valid=true;m.precision.rms_ns=88;m.precision.samples=32;m.precision.map_valid=false;
 fixture(argv[1],"precision-serving-gm",&m,&s,62000);assert(color_count(64,7,R2_UI_RGB565_CYAN)&&!color_count(92,7,R2_UI_RGB565_CYAN));
 assert(!memcmp(pps_duration,fb+109*80*2,sizeof pps_duration));
 /* Serving GM retains its existing Bluetooth beat badge beside RMS. */
 uint8_t expected_badge[R2_UI_BYTES];memcpy(expected_badge,fb,sizeof fb);fill(expected_badge,4,49,11,11,C_BG);bluetooth_badge_at(expected_badge,4,49,true);
 for(unsigned y=49;y<60;y++)assert(!memcmp(expected_badge+((y*80+4)*2),fb+((y*80+4)*2),11*2));
 assert(color_count(20,7,R2_UI_RGB565_GREEN)&&!color_count(20,7,R2_UI_RGB565_AMBER));
 assert(color_count(128,7,R2_UI_RGB565_GREEN)&&!color_count(128,7,R2_UI_RGB565_AMBER));
 /* Long durations stay within the 80px display without losing seconds. */
 m.sampled_ms=6001000;fixture(argv[1],"precision-gm-active-100m",&m,&s,6001000);
 m.sampled_ms=600000000;fixture(argv[1],"precision-gm-active-9999m",&m,&s,600000000);
 m.sampled_ms=600001000;fixture(argv[1],"precision-gm-active-overflow",&m,&s,600001000);
 m.sampled_ms=62000;r2_test_render_rgb565(fb,&m,&s,62000);
 uint8_t beat_rms[80*2*7];memcpy(beat_rms,fb+64*80*2,sizeof beat_rms);
 s.shadow.rms_ns=1;s.shadow.mean_tenth_ns=0;m.pps_precision.prediction_rms_ns=999999;
 r2_test_render_rgb565(fb,&m,&s,62000);assert(!memcmp(beat_rms,fb+64*80*2,sizeof beat_rms));
 m.precision.rms_ns=1234;fixture(argv[1],"precision-gm-beat-us",&m,&s,62000);assert(memcmp(beat_rms,fb+64*80*2,sizeof beat_rms));
 m.precision.rms_valid=false;fixture(argv[1],"precision-gm-no-beat",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.precision.rms_valid=true;m.gm_source_fresh=false;fixture(argv[1],"precision-gm-source-wait",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.gm_source_fresh=true;fixture(argv[1],"precision-gm-stale",&m,&s,65001);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.precision.map_valid=true;
 m.role=R2_UI_FOLLOWER;m.local_hot_standby=false;m.source_eligible=false;m.admission_ms=42000;
 fixture(argv[1],"precision-hot-dwell",&m,&s,62000);assert(!strcmp(hot_reason(&m,true,false),"SOURCE DWELL")&&color_count(141,7,R2_UI_RGB565_DIM));
 m.source_eligible=true;
 const unsigned refusals[]={1,4,16,32};const char *reasons[]={"PEER WAIT","PEER SOURCE","AUTH WAIT","BLE MAP WAIT"};
 for(unsigned i=0;i<4;i++){m.hot_refusal=refusals[i];assert(!strcmp(hot_reason(&m,true,false),reasons[i]));}
 m.hot_refusal=16;fixture(argv[1],"precision-hot-auth",&m,&s,62000);assert(color_count(141,7,R2_UI_RGB565_DIM));
 m.profile_rc=m.boundary_clock=true;m.role=R2_UI_FOLLOWER;
 m.ble_time_selected=true;m.ble_valid_until_us=63000000;
 m.precision.rms_valid=true;m.precision.rms_ns=137;m.precision.correction_age_valid=true;m.precision.correction_age_ms=42;
 m.pps_precision.prediction_rms_ns=987654;m.face_rms_ns=7654321;
 fixture(argv[1],"precision-rc",&m,&s,62000);assert(color_count(64,7,R2_UI_RGB565_CYAN));
 uint8_t own_rms[80*2*7];memcpy(own_rms,fb+64*80*2,sizeof own_rms);
 m.precision.correction_age_ms=9999;m.pps_precision.prediction_rms_ns=1;m.face_rms_ns=2;s.shadow.rms_ns=3;
 r2_test_render_rgb565(fb,&m,&s,62000);assert(!memcmp(own_rms,fb+64*80*2,sizeof own_rms));
 m.precision.rms_ns=200;fixture(argv[1],"precision-rc-own-rms",&m,&s,62000);
 assert(memcmp(own_rms,fb+64*80*2,sizeof own_rms));
 m.precision.rms_ns=1234;fixture(argv[1],"precision-rc-us",&m,&s,62000);
 m.precision.rms_ns=UINT32_MAX;fixture(argv[1],"precision-rc-unknown",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.precision.rms_ns=137;m.precision.rms_valid=false;
 fixture(argv[1],"precision-rc-no-rms",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.precision.rms_valid=true;m.precision.map_valid=false;
 fixture(argv[1],"precision-rc-no-map",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.precision.map_valid=true;m.ble_valid_until_us=62000000;
 fixture(argv[1],"precision-rc-expired",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.ble_valid_until_us=63000000;m.ble_active=false;
 fixture(argv[1],"precision-rc-stopped",&m,&s,62000);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 m.ble_active=true;fixture(argv[1],"precision-rc-stale",&m,&s,65001);assert(!color_count(64,7,R2_UI_RGB565_CYAN));
 puts("PASS Precision: real holds/loss/staleness, stable green HOT/SoftPLL, witness eligibility, missing and extreme values");
}
