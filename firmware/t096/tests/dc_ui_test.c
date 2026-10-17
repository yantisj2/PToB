/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t frame[R2_UI_BYTES],active[R2_UI_BYTES];
static void save(const char *dir,const char *name){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof frame;i+=2){unsigned p=(unsigned)frame[i]*256+frame[i+1];
  fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 assert(!fclose(f));
}
int main(int argc,char **argv){
 assert(argc==2&&R2_UI_PAGES==4);
 struct r2_ui_model m={.sampled_ms=1000,.unit="TC-04",.platform_ready=true,.role=R2_UI_GM,
  .scope_active=true,.gm_source_fresh=true,.source_eligible=true,
  .clock={.valid=true,.source_available=true,.utc_us=36642000000LL,.fix_active=true,
   .pps_present=true,.pps_age_us=1000,.gps_age_us=200000,.sats_used=22},
  .precision={.rms_ns=123,.rms_valid=true,.samples=12}};
 m.temperature_valid=true;m.temperature_qc=35*4;
 struct r2_ui_status s={0};
 assert(!m.profile_supported&&!m.ble_active&&r2_ui_face_locked(&m,1000));
 assert(r2_ui_saver_allowed(&m,1000));
 r2_ui_render(frame,&m,&s,1000);save(argv[1],"dc-active");memcpy(active,frame,sizeof frame);
 m.precision.rms_valid=false;r2_ui_render(frame,&m,&s,1000);save(argv[1],"dc-usb-wait");
 assert(memcmp(frame,active,sizeof frame));m.precision.rms_valid=true;
 for(unsigned page=1;page<4;page++){s.page=page;r2_ui_render(frame,&m,&s,1000);
  save(argv[1],page==1?"dc-usb":page==2?"dc-gnss":"dc-system");}
 s.page=0;s.screensaver=true;r2_ui_render(frame,&m,&s,1000);save(argv[1],"dc-saver");
 m.scope_active=m.gm_source_fresh=m.clock.valid=m.clock.source_available=false;
 assert(!r2_ui_face_locked(&m,1000)&&!r2_ui_saver_allowed(&m,1000));
 s.screensaver=false;r2_ui_render(frame,&m,&s,1000);save(argv[1],"dc-source-loss");
 s.help=true;r2_ui_render(frame,&m,&s,1000);save(argv[1],"dc-help");
 puts("PASS DC renderer: fixed role, analog/saver gates, RMS unavailable, GNSS/USB/system/help pages");
}
