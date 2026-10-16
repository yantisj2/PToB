/* Review fixtures use the production renderer. All values are synthetic. */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t fb[R2_UI_BYTES];
static uint8_t saver_top[R2_UI_BYTES];
static uint64_t preview_at=1000;
static void save(const char *dir,const char *name,struct r2_ui_model *m,struct r2_ui_status *s){
 r2_ui_render(fb,m,s,preview_at);char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof fb;i+=2){unsigned p=(fb[i]<<8)|fb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 for(unsigned current=0;current<3;current++){
  unsigned a=r2_ui_saver_next(current,0),b=r2_ui_saver_next(current,1);
  assert(a<3&&b<3&&a!=current&&b!=current&&a!=b);
  assert(r2_ui_saver_next(current,UINT32_MAX)==b);
 }
 struct r2_ui_model m={.sampled_ms=1000,.unit="TC-02",.platform_ready=true,.bt_ready=true,
  .scope_active=true,.ble_active=true,.profile_supported=true,.role=R2_UI_GM,
  .gm_capable=true,.gm_source_fresh=true,.precision_beat_hz=16,
  .temperature_valid=true,.temperature_qc=92,
  .clock={.valid=true,.utc_us=36642000000LL,.source_available=true,.fix_active=true,
   .sats_used=18,.pps_present=true,.pps_age_us=100000,.gps_age_us=100000,
   .rx_age_us=50000,.gga_age_us=100000,.state=R2_TIME_SYNCED},
  .precision={.authorized=true,.map_valid=true,.fit_valid=true,.locked=true,
   .rms_valid=true,.rms_ns=63,.samples=64},
  .pps_precision={.epoch=1,.age_us=100000,.rate_valid=true,.samples=64,.prediction_rms_ns=100}};
 for(unsigned i=0;i<8;i++)m.lamps[i]=i%3;
 struct r2_ui_status s={0};assert(r2_ui_face_locked(&m,1000));save(argv[1],"main-gm",&m,&s);
 m.temperature_qc=144;save(argv[1],"main-gm-green",&m,&s);
 m.temperature_qc=168;save(argv[1],"main-gm-amber",&m,&s);
 m.temperature_valid=false;save(argv[1],"main-gm-temperature-unknown",&m,&s);
 m.temperature_valid=true;m.temperature_qc=92;
 s.screensaver=true;s.screensaver_position=1;save(argv[1],"saver-gm",&m,&s);s.screensaver=false;
 s.page=1;save(argv[1],"radio-gm",&m,&s);
 s.page=0;m.role=R2_UI_FOLLOWER;m.authenticated_gm=true;m.local_hot_standby=true;
 strcpy(m.root_name,"TC-01");m.face_lock_until_us=2000000;m.face_rms_ns=63;m.follower_pps_valid=true;
 r2_ui_select_ble_time(&m,36642000000LL,1000000,1000000,2000000,true);
 struct r2_link_quality_view q={.last_rx=950000,.samples=12,.delivery_per_mille=950};
 r2_ui_reception(&m,&q,1000000);assert(m.ble_reception_valid&&m.ble_rx_age_ms==50);
 assert(r2_ui_face_locked(&m,1000));save(argv[1],"main-follower",&m,&s);
 uint8_t follower_cool[R2_UI_BYTES];memcpy(follower_cool,fb,sizeof fb);
 m.temperature_qc=168;save(argv[1],"main-follower-amber",&m,&s);
 for(unsigned y=0;y<160;y++)for(unsigned x=0;x<80;x++)if(x<26||x>54||y<90||y>102)
  assert(!memcmp(follower_cool+2*(y*80+x),fb+2*(y*80+x),2));
 m.temperature_qc=92;
 s.page=1;save(argv[1],"radio-hot",&m,&s);
 s.page=0;m.local_hot_standby=false;q.delivery_per_mille=750;r2_ui_reception(&m,&q,1000000);
 save(argv[1],"main-three-bars",&m,&s);
 q.last_rx=0;r2_ui_reception(&m,&q,1000000);assert(!m.ble_reception_valid);save(argv[1],"main-no-reception",&m,&s);
 q.last_rx=950000;q.delivery_per_mille=950;r2_ui_reception(&m,&q,1000000);
 s.screensaver=true;
 for(unsigned pos=0;pos<3;pos++){
  s.screensaver_position=pos;save(argv[1],pos==0?"saver-top":pos==1?"saver-center":"saver-bottom",&m,&s);
  int top=(int)r2_ui_saver_top(pos);
  for(unsigned y=0;y<160;y++)for(unsigned x=0;x<80;x++)if(x<3||x>=77||y<(unsigned)top||y>=(unsigned)(top+R2_UI_SAVER_HEIGHT))
   assert(fb[2*(y*80+x)]==0&&fb[2*(y*80+x)+1]==0);
  if(!pos)memcpy(saver_top,fb,sizeof fb);
  else for(unsigned y=0;y<R2_UI_SAVER_HEIGHT;y++)
   assert(!memcmp(fb+2*((top+y)*80),saver_top+2*((4+y)*80),80*2));
 }
 s.screensaver_position=1;save(argv[1],"saver-follower",&m,&s);
 m.temperature_qc=168;save(argv[1],"saver-follower-amber",&m,&s);
 m.temperature_valid=false;save(argv[1],"saver-temperature-unknown",&m,&s);
 m.temperature_valid=true;m.temperature_qc=144;
 m.boundary_clock=m.profile_rc=m.relay_selected=true;m.time_relay_hops=1;
 strcpy(m.upstream_name,"TC-03");save(argv[1],"saver-rc",&m,&s);
 strcpy(m.upstream_name,"Kitchen");save(argv[1],"saver-long-name",&m,&s);
 strcpy(m.upstream_name,"LivingRoomClock");save(argv[1],"saver-name-overflow",&m,&s);
 m.clock.pps_present=false;save(argv[1],"saver-no-local-pps",&m,&s);m.clock.pps_present=true;
 m.boundary_clock=m.profile_rc=m.relay_selected=false;m.time_relay_hops=0;
 m.temperature_qc=92;
 s.screensaver=false;s.page=4;m.reset_kind=R2_UI_RESET_WD_PROFILE;
 preview_at=UINT64_C(10713599000);m.sampled_ms=preview_at;save(argv[1],"system-watchdog",&m,&s);
 preview_at=UINT64_C(4294967296);m.sampled_ms=preview_at;m.reset_kind=R2_UI_RESET_WD_RECOVERY;save(argv[1],"system-long-uptime",&m,&s);
 preview_at=1000;m.sampled_ms=preview_at;m.reset_kind=R2_UI_RESET_UNKNOWN;save(argv[1],"system-unknown",&m,&s);
 puts("Casio fixtures: actual dial/role/System pages; selected reception; saver black extents PASS");
}
