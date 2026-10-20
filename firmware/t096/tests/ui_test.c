/* Actual renderer and serving-role gate; output is also the visual fixture. */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static uint8_t framebuffer[R2_UI_BYTES];
static void save(const char *dir,const char *name,struct r2_ui_model *m,struct r2_ui_status *s){
 r2_ui_render(framebuffer,m,s,1000);char path[512];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<R2_UI_BYTES;i+=2){unsigned p=((unsigned)framebuffer[i]<<8)|framebuffer[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){
 char name[25];
 r2_ui_identity_name(name,UINT64_C(0x1000000000000001),"Clock-1000000000000001");assert(!strcmp(name,"0001"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000002),"Clock-1000000000000002");assert(!strcmp(name,"0002"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000003),"Clock-1000000000000003");assert(!strcmp(name,"0003"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000004),"Clock-1000000000000004");assert(!strcmp(name,"0004"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000004),"Kitchen");assert(!strcmp(name,"Kitchen"));
 r2_ui_identity_name(name,42,"Kitchen");assert(!strcmp(name,"Kitchen"));
 r2_ui_identity_name(name,42,"Clock-000000000000002a");assert(!strcmp(name,"002a"));
 r2_ui_identity_name(name,UINT64_C(0x1234567890abcdef),NULL);assert(!strcmp(name,"cdef"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000006),NULL);assert(!strcmp(name,"0006"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000007),NULL);assert(!strcmp(name,"0007"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000008),NULL);assert(!strcmp(name,"0008"));
 r2_ui_identity_name(name,UINT64_C(0x1000000000000009),NULL);assert(!strcmp(name,"0009"));
 r2_ui_identity_name(name,0,NULL);assert(!strcmp(name,"--"));

 assert(argc==2);struct r2_ui_model m={.sampled_ms=1000,.unit="TC-01",.platform_ready=true,.bt_ready=true};
 struct r2_ui_status shadow={.page=R2_UI_SHADOW_PAGE,.shadow={.state=R2_SHADOW_RUNNING,.active=true,.valid=true,.elapsed_ms=600000,.samples=436,.skipped=65,.mean_tenth_ns=-184,.p95_ns=123,.p99_ns=160,.max_ns=191}};
 save(argv[1],"shadow-measuring",&m,&shadow);
 shadow.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_DWELL};save(argv[1],"shadow-dwell",&m,&shadow);
 shadow.shadow.state=R2_SHADOW_WAIT_PPS;save(argv[1],"shadow-no-pps",&m,&shadow);
 shadow.shadow.state=R2_SHADOW_GM;save(argv[1],"shadow-gm",&m,&shadow);
 shadow.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_WAIT_MAP,.active=true,.elapsed_ms=1000};save(argv[1],"shadow-no-map",&m,&shadow);
 shadow.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_RUNNING,.valid=true,.elapsed_ms=359999000,.samples=99999,.skipped=99,.mean_tenth_ns=-5000000000LL,.p95_ns=300000000,.p99_ns=400000000,.max_ns=500000000,.approximate=true};save(argv[1],"shadow-large-tail",&m,&shadow);
 struct r2_ui_status s={0};save(argv[1],"acquiring",&m,&s);assert(!r2_ui_analog(&m,1000));
 struct r2_ui_model held=m;held.clock.valid=false;held.coarse_valid=true;held.coarse_utc_us=36642000000ULL;held.coarse_at_us=0;held.sync_attempt=true;save(argv[1],"coarse-held",&held,&s);held.sync_attempt=false;save(argv[1],"coarse-stale",&held,&s);assert(!r2_ui_analog(&held,1000));
 struct r2_ui_model rx_live=held;rx_live.clock.rx_age_us=1000;rx_live.clock.gps_age_us=-1;rx_live.clock.gga_age_us=1000;rx_live.clock.sats_used=0;
 save(argv[1],"rx-live-no-fix",&rx_live,&s);uint8_t rx_frame[R2_UI_BYTES];memcpy(rx_frame,framebuffer,sizeof rx_frame);
 rx_live.clock.rx_age_us=3000000;save(argv[1],"rx-lost",&rx_live,&s);assert(memcmp(rx_frame,framebuffer,sizeof rx_frame));
 struct r2_ui_model settling=m;strcpy(settling.unit,"TC-03");strcpy(settling.root_name,"TC-01");settling.sync_attempt=true;save(argv[1],"syncing",&settling,&s);assert(!r2_ui_analog(&settling,1000));
 m.clock.valid=true;m.clock.utc_us=36642000000LL;m.clock.fix_active=true;m.clock.sats_used=22;m.clock.pps_present=true;
 save(argv[1],"coarse",&m,&s);assert(!r2_ui_analog(&m,1000));
 m.role=R2_UI_GM;m.scope_active=true;assert(!r2_ui_analog(&m,1000));m.clock.source_available=true;
 assert(r2_ui_analog(&m,1000));save(argv[1],"grandmaster",&m,&s);
 uint8_t unknown_temperature[R2_UI_BYTES];memcpy(unknown_temperature,framebuffer,sizeof framebuffer);
 m.temperature_valid=true;m.temperature_qc=94;save(argv[1],"grandmaster-temperature",&m,&s);
 assert(memcmp(unknown_temperature,framebuffer,sizeof framebuffer)!=0);
 m.temperature_valid=false;r2_ui_render(framebuffer,&m,&s,1000);
 assert(memcmp(unknown_temperature,framebuffer,sizeof framebuffer)==0);
 assert(!r2_ui_analog(&m,3501));assert(!r2_ui_analog(&m,999));
 m.role=R2_UI_FOLLOWER;assert(!r2_ui_analog(&m,1000));m.authenticated_gm=true;
 /* BLE wins even over a locally ready GPS clock, with no local PPS. */
 m.clock.source_available=true;m.clock.pps_present=false;m.follower_pps_valid=false;
 r2_ui_select_ble_time(&m,36642000000LL,999000,1000000,1100000,true);
 assert(m.ble_time_selected&&m.clock.utc_us==36642001000LL&&!m.follower_pps_valid);
 assert(!r2_ui_analog(&m,1000));m.follower_pps_valid=true;m.precision.map_valid=true;
 assert(r2_ui_analog(&m,1000));save(argv[1],"follower",&m,&s);m.sync_age_ms=2501;assert(!r2_ui_analog(&m,1000));
 m.sync_age_ms=0;
 r2_ui_select_ble_time(&m,1,1000000,1000000,1000000,true);assert(!m.clock.valid&&!m.ble_time_selected&&!r2_ui_analog(&m,1000));
 m.clock.source_available=false;m.clock.fix_active=false;m.clock.sats_used=0;
 r2_ui_select_ble_time(&m,36642000000LL,1000000,1000000,1100000,true);assert(m.clock.valid&&r2_ui_analog(&m,1000));
 save(argv[1],"follower-no-gps",&m,&s);
 /* An active boundary service acquiring its first map has no invented UTC. */
 struct r2_ui_model joining={.sampled_ms=1000,.unit="TC-03",.platform_ready=true,.bt_ready=true,.boundary_clock=true,.sync_attempt=true};
 /* Transport lamps survive missing/expired time authority, but stale models
  * and stopped BLE cannot leave them illuminated. LoRa remains OFF. */
 joining.ble_active=true;joining.ble_time_selected=true;joining.ble_valid_until_us=1000000;
 joining.lamps[1]=1;joining.lamps[5]=2;
 struct r2_ui_status radio_page={.page=1};uint8_t live_lamps[R2_UI_BYTES],dark_lamps[R2_UI_BYTES];
 r2_ui_render(live_lamps,&joining,&radio_page,1000);
 struct r2_ui_model dark=joining;memset(dark.lamps,0,sizeof dark.lamps);
 r2_ui_render(dark_lamps,&dark,&radio_page,1000);
 for(unsigned kind=0;kind<8;kind++){
  unsigned offset=2*(34*80+8+9*kind);
  assert((memcmp(live_lamps+offset,dark_lamps+offset,2)!=0)==(kind==1||kind==5));
  offset=2*(58*80+8+9*kind);assert(!memcmp(live_lamps+offset,dark_lamps+offset,2));
 }
 /* Fresh continuous RX/TX alternates every frame: exactly four full visible
  * flashes per second rather than a continuously illuminated packet latch. */
 uint8_t blink[8];unsigned on=0,off=0;
 for(unsigned frame=0;frame<8;frame++){
  joining.sampled_ms=1000+frame*125;
  r2_ui_rf_lamps(blink,&joining,joining.sampled_ms);
  if(frame%2){assert(!blink[1]&&!blink[5]);off++;}
  else{assert(blink[1]==1&&blink[5]==2);on++;}
 }
 assert(on==4&&off==4);
 joining.sampled_ms=1000;
 r2_ui_render(live_lamps,&joining,&radio_page,1125);
 for(unsigned kind=0;kind<8;kind++)assert(!memcmp(live_lamps+2*(34*80+8+9*kind),dark_lamps+2*(34*80+8+9*kind),2));
 r2_ui_render(live_lamps,&joining,&radio_page,1250);
 for(unsigned kind=0;kind<8;kind++)assert(!memcmp(live_lamps+2*(34*80+8+9*kind),dark_lamps+2*(34*80+8+9*kind),2));
 joining.ble_active=false;r2_ui_render(live_lamps,&joining,&radio_page,1000);
 for(unsigned kind=0;kind<8;kind++)assert(!memcmp(live_lamps+2*(34*80+8+9*kind),dark_lamps+2*(34*80+8+9*kind),2));
 joining.ble_time_selected=false;memset(joining.lamps,0,sizeof joining.lamps);
 int64_t joining_utc;bool joining_coarse;
 assert(!r2_ui_display_time(&joining,1000,&joining_utc,&joining_coarse)&&!r2_ui_face_locked(&joining,1000));
 save(argv[1],"rc-syncing",&joining,&s);
 uint8_t joining_frame[R2_UI_BYTES];memcpy(joining_frame,framebuffer,sizeof framebuffer);
 joining.sync_attempt=false;save(argv[1],"rc-unavailable",&joining,&s);assert(memcmp(joining_frame,framebuffer,sizeof framebuffer));
 /* Relay analog time is functional held time, independent of legacy PPS/RMS. */
 struct r2_ui_model rc=m;rc.boundary_clock=true;rc.follower_pps_valid=false;rc.face_lock_until_us=0;rc.face_rms_ns=UINT32_MAX;
 rc.precision.rms_valid=false;rc.precision.locked=false;rc.sync_age_ms=90000;rc.ble_valid_until_us=UINT64_MAX;
 strcpy(rc.unit,"TC-03");strcpy(rc.upstream_name,"TC-01");rc.relay_selected=true;
 assert(r2_ui_analog(&rc,1000)&&r2_ui_face_locked(&rc,1000));assert(!strcmp(r2_ui_servo_state(&rc,1000),"RECEIVING"));save(argv[1],"rc-acquiring-fit",&rc,&s);
 rc.precision.authorized=true;rc.precision.rms_valid=true;rc.precision.rms_ns=707;rc.precision.samples=16;rc.precision.fit_valid=true;rc.rc_update_age_ms=180;rc.rc_source_age_ms=400;
 save(argv[1],"rc-receiving",&rc,&s);uint8_t rc_no_pll[R2_UI_BYTES];memcpy(rc_no_pll,framebuffer,sizeof framebuffer);
 rc.precision.locked=true;save(argv[1],"rc-pll-tracking",&rc,&s);assert(memcmp(rc_no_pll,framebuffer,sizeof framebuffer));
 /* The same cached model loses dial, current RMS and PLL at its real deadline. */
 rc.ble_active=true;rc.ble_valid_until_us=1500000;
 assert(r2_ui_analog(&rc,1499));assert(!r2_ui_analog(&rc,1500));
 int64_t held_utc;bool held_coarse;assert(!r2_ui_display_time(&rc,1500,&held_utc,&held_coarse));
 rc.ble_valid_until_us=1000000;save(argv[1],"rc-hold-expired",&rc,&s);
 uint8_t expired_frame[R2_UI_BYTES];memcpy(expired_frame,framebuffer,sizeof framebuffer);
 rc.precision.rms_valid=false;rc.precision.locked=false;r2_ui_render(framebuffer,&rc,&s,1000);
 assert(!memcmp(expired_frame,framebuffer,sizeof framebuffer));
 rc.precision.rms_valid=true;rc.precision.locked=true;rc.ble_valid_until_us=UINT64_MAX;
 rc.rc_source_held=true;rc.rc_update_age_ms=20000;rc.rc_source_age_ms=21000;
 assert(r2_ui_face_locked(&rc,1000)&&!strcmp(r2_ui_servo_state(&rc,1000),"HELD SOURCE"));save(argv[1],"rc-held",&rc,&s);
 /* Downstream service affects only RC letters on the first screen. */
 struct r2_ui_model serving=rc;serving.profile_supported=serving.profile_rc=true;
 serving.ble_active=true;serving.rc_serving_clients=false;
 save(argv[1],"rc-client-idle",&serving,&s);uint8_t idle[R2_UI_BYTES];memcpy(idle,framebuffer,sizeof idle);
 serving.rc_serving_clients=true;save(argv[1],"rc-client-serving",&serving,&s);
 unsigned green=0,changed=0;
 for(unsigned y=0;y<R2_UI_HEIGHT;y++)for(unsigned x=0;x<R2_UI_WIDTH;x++){
  unsigned at=2*(y*R2_UI_WIDTH+x);bool different=memcmp(framebuffer+at,idle+at,2)!=0;
  if(different){assert(x>=10&&x<22&&y>=147&&y<154);++changed;}
  if(x>=10&&x<22&&y>=147&&y<154&&framebuffer[at]==0x4f&&framebuffer[at+1]==0xea)++green;
 }
 assert(green&&green==changed);
 serving.profile_switching=true;r2_ui_render(framebuffer,&serving,&s,1000);assert(!memcmp(framebuffer,idle,sizeof idle));
 serving.profile_switching=false;serving.ble_valid_until_us=1000000;
 save(argv[1],"rc-client-serving-expired",&serving,&s);
 for(unsigned y=147;y<154;y++)for(unsigned x=10;x<22;x++){
  unsigned at=2*(y*R2_UI_WIDTH+x);assert(!(framebuffer[at]==0x4f&&framebuffer[at+1]==0xea));
 }
 struct r2_ui_model gm_indicator=m;gm_indicator.profile_supported=true;gm_indicator.profile_rc=false;
 gm_indicator.rc_serving_clients=false;r2_ui_render(idle,&gm_indicator,&s,1000);
 gm_indicator.rc_serving_clients=true;r2_ui_render(framebuffer,&gm_indicator,&s,1000);assert(!memcmp(idle,framebuffer,sizeof idle));
 s.page=2;save(argv[1],"rc-relative-fit",&rc,&s);s.page=0;
 strcpy(rc.unit,"TC-02");strcpy(rc.upstream_name,"TC-03");rc.rc_update_age_ms=50;save(argv[1],"relay-client-held-source",&rc,&s);
 rc.precision.map_valid=false;assert(!r2_ui_analog(&rc,1000)&&!r2_ui_face_locked(&rc,1000));
 struct r2_ui_model face=m;face.face_lock_until_us=1500000;face.face_rms_ns=50;face.precision.rms_ns=50;face.precision.rms_valid=true;face.precision.locked=false;face.follower_pps_valid=false;face.local_hot_standby=false;
 strcpy(face.unit,"TC-03");strcpy(face.root_name,"TC-02");face.clock.utc_us=36642000000LL;
 assert(r2_ui_face_locked(&face,1000)&&!r2_ui_analog(&face,1000));save(argv[1],"rms-face-no-gnss",&face,&s);
 /* A BLE-locked face must not fabricate the local GNSS PPS lamp. */
 uint8_t gnss_off[R2_UI_BYTES];memcpy(gnss_off,framebuffer,sizeof framebuffer);
 face.clock.pps_present=true;face.clock.pps_age_us=0;save(argv[1],"ble-local-gnss-pps",&face,&s);
 assert(memcmp(gnss_off,framebuffer,sizeof framebuffer));
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<80;x++)if(x<66||x>=75||y<19||y>=28)
  assert(!memcmp(gnss_off+2*(y*80+x),framebuffer+2*(y*80+x),2));
 face.clock.pps_age_us=250000;save(argv[1],"ble-local-gnss-pps-ended",&face,&s);
 assert(!memcmp(gnss_off,framebuffer,sizeof framebuffer));
 face.clock.pps_age_us=-1;save(argv[1],"ble-local-gnss-pps-unknown",&face,&s);
 assert(!memcmp(gnss_off,framebuffer,sizeof framebuffer));
 face.clock.pps_present=false;face.clock.pps_age_us=0;
 face.precision.authorized=true;save(argv[1],"rms-pll-unlocked",&face,&s);
 uint8_t pll_off[R2_UI_BYTES];memcpy(pll_off,framebuffer,sizeof framebuffer);
 face.precision.locked=true;save(argv[1],"rms-pll-locked",&face,&s);
 assert(!memcmp(pll_off,framebuffer,118*80*2)); /* Face remains unchanged. */
 assert(memcmp(pll_off,framebuffer,sizeof framebuffer));
 face.precision.locked=false;save(argv[1],"rms-pll-lost",&face,&s);
 assert(!memcmp(pll_off,framebuffer,sizeof framebuffer)); /* No icon grace. */
 face.precision.locked=true;face.precision.map_valid=false;save(argv[1],"rms-pll-map-lost",&face,&s);
 assert(!memcmp(pll_off,framebuffer,sizeof framebuffer));
 face.precision.locked=false;face.precision.map_valid=true;
 face.face_lock_until_us=1000000;assert(!r2_ui_face_locked(&face,1000));save(argv[1],"rms-face-gap-expired",&face,&s);
 m.authenticated_gm=false;r2_ui_select_ble_time(&m,1,1000000,1000000,1100000,true);assert(!m.clock.valid);
 m.authenticated_gm=true;r2_ui_select_ble_time(&m,1,1000000,1050000,1100000,true);assert(m.clock.valid&&m.ble_time_selected);
 m.clock.valid=true;m.clock.utc_us=36642000000LL;m.ble_time_selected=false;
 r2_ui_select_ble_time(&m,1,1000000,1050000,1000000,false);
 assert(m.clock.valid&&!m.ble_time_selected&&!r2_ui_analog(&m,1000)); /* Independent coarse panel survives. */

 r2_ui_select_ble_time(&m,36642000000LL,1000000,1000000,1100000,true);
 m.scope_active=false;assert(!r2_ui_analog(&m,1000));m.scope_active=true;m.platform_ready=false;assert(!r2_ui_analog(&m,1000));
 m.platform_ready=true;m.clock.valid=false;assert(!r2_ui_analog(&m,1000));m.clock.valid=true;m.clock.utc_us=-1;assert(!r2_ui_analog(&m,1000));m.clock.utc_us=36642000000LL;
 m.precision=(struct r2_radio_precision_view){.rms_valid=true,.map_valid=true,.rms_ns=123,.samples=32,.trim_ppb=-17230,.drift_ppb=17230,.drift_valid=true};
 m.pps_precision=(struct r2_pps_precision_view){.rate_valid=true,.raw_rate_ppb=17230};
 save(argv[1],"follower-precision",&m,&s);
 uint8_t follower_face[R2_UI_BYTES];memcpy(follower_face,framebuffer,sizeof framebuffer);
 /* Mode and source labels never change time/analog authorization. */
 bool original_analog=r2_ui_analog(&m,1000);
 m.profile_supported=true;m.profile_rc=true;save(argv[1],"mode-rc-direct-ble",&m,&s);
 assert(r2_ui_analog(&m,1000)==original_analog);
 uint8_t rc_direct[R2_UI_BYTES];memcpy(rc_direct,framebuffer,sizeof framebuffer);
 m.profile_rc=false;save(argv[1],"mode-gm-ble",&m,&s);
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<80;x++)if(x<8||x>=40||y<147||y>=154)
  assert(!memcmp(rc_direct+2*(y*80+x),framebuffer+2*(y*80+x),2));
 m.profile_rc=true;m.time_relay_hops=1;save(argv[1],"mode-rc-source-rc1",&m,&s);
 assert(r2_ui_analog(&m,1000)==original_analog);
 m.time_relay_hops=2;save(argv[1],"mode-rc-source-rc2-layout-only",&m,&s);
 m.precision.map_valid=false;assert(!r2_ui_analog(&m,1000));m.precision.map_valid=true;
 m.profile_supported=false;m.profile_rc=false;m.time_relay_hops=0;

 m.profile_supported=true;m.profile_rc=false;m.precision.authorized=true;
 save(argv[1],"gm-standby-wait",&m,&s);uint8_t gm_wait[R2_UI_BYTES];memcpy(gm_wait,framebuffer,sizeof framebuffer);
 m.local_hot_standby=true;save(argv[1],"hot-standby",&m,&s);
 assert(memcmp(gm_wait,framebuffer,sizeof framebuffer));
 /* HOT changes only the mode line; BLE remains the direct time source. */
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<80;x++)if(x<8||x>=40||y<147||y>=154)
  assert(!memcmp(gm_wait+2*(y*80+x),framebuffer+2*(y*80+x),2));
 m.local_hot_standby=false;save(argv[1],"gm-standby-pending",&m,&s);assert(!memcmp(gm_wait,framebuffer,sizeof framebuffer));
 m.profile_supported=false;
 m.local_hot_standby=false;save(argv[1],"standby-wait",&m,&s);
 assert(!memcmp(follower_face,framebuffer,sizeof framebuffer));
 s.page=2;
 assert(!strcmp(r2_ui_servo_state(&m,1000),"ACQUIRING"));save(argv[1],"servo-acquiring",&m,&s);
 m.precision.locked=true;m.precision.fit_valid=true;m.precision.correction_age_valid=true;m.precision.correction_age_ms=63;
 assert(!strcmp(r2_ui_servo_state(&m,1000),"LOCKED"));save(argv[1],"servo-locked",&m,&s);
 assert(!strcmp(r2_ui_servo_state(&m,3501),"UNAVAILABLE"));
 m.precision.locked=false;m.precision.map_valid=false;m.precision.correction_age_ms=1500;m.precision.fit_valid=false;m.precision.rms_valid=false;m.precision.drift_valid=false;
 assert(!strcmp(r2_ui_servo_state(&m,1000),"UNAVAILABLE"));save(argv[1],"servo-stale",&m,&s);
 m.precision.authorized=false;assert(!strcmp(r2_ui_servo_state(&m,1000),"UNAVAILABLE"));
 m.role=R2_UI_GM;assert(!strcmp(r2_ui_servo_state(&m,1000),"UNAVAILABLE"));save(argv[1],"servo-gm",&m,&s);
 m.role=R2_UI_FOLLOWER;m.precision.authorized=true;m.precision.map_valid=true;m.precision.correction_age_ms=63;
 s.page=2;m.precision.rms_ns=1234;save(argv[1],"precision-us",&m,&s);m.precision.rms_ns=123;
 m.gm_capable=m.source_eligible=m.hot_ready=true;m.coordinator=m.standby=1;m.quality.clock_class=248;m.quality.accuracy=254;
 m.clock.gga_count=m.clock.faults=m.clock.pairs=m.queue_drops=UINT_MAX;s.frames=s.max_frame_ms=s.missed=UINT_MAX;
 for(unsigned page=1;page<R2_UI_PAGES;page++){s.page=page;save(argv[1],page==1?"statistics":page==2?"precision":page==3?"quality":page==4?"system":"help",&m,&s);}
 s.page=UINT8_MAX;r2_ui_render(framebuffer,&m,&s,UINT64_MAX);
 puts("UI: serving authority, BLE source priority without GPS, stale/withdrawn scopes and all pages at maximum counters PASS");
}
