/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual clock-face pixels with synthetic owner snapshots; no hardware. */
#include <assert.h>
#include <stdio.h>
#include "../src/ui_render.c"
static uint8_t fb[R2_UI_FRAME_BYTES];
static void save(const char *dir,unsigned mode,unsigned saver){
 char path[1024];snprintf(path,sizeof path,"%s/tx-%u-%u.ppm",dir,mode,saver);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 uint8_t rgb[R2_UI_BYTES];assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];
  fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 assert(!fclose(f));
}
int main(int argc,char **argv){assert(argc==2);
 for(unsigned saver=0;saver<2;saver++)for(unsigned mode=0;mode<9;mode++){
  struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.ble_active=true,.gm_source_fresh=true,.unit="TX-A",.role=R2_UI_GM,.scope_active=true,
   .clock={.valid=true,.utc_us=36642000000LL,.source_available=true,.fix_active=true,.pps_present=true},
   .lora={.transmitter=true,.requested=true,.active=true,.time_valid=true,.state=RC8_AVAILABLE,.age_ms=5000}};
  struct r2_ui_status s={.page=0,.screensaver=saver};
  if(mode==1)m.lora.time_valid=false;
  if(mode==2)m.lora.state=RC8_ACQUIRING; /* Source ready, no completed pair. */
  if(mode==3)m.lora.age_ms=LP_CADENCE_BOUND_US/1000;
  if(mode==4)m.lora.active=false;
  if(mode==5)m.lora.fault=true;
  if(mode==6)m.lora.requested=false;
  if(mode==7)m.lora.transmitter=false;
  if(mode==8)m.lora.age_ms=UINT32_MAX;
  assert(r2_ui_analog(&m,1000));r2_ui_render(fb,&m,&s,1000);
  unsigned cy=saver?r2_ui_saver_top(0)+44:83;
  uint8_t pixel=fb[(cy-14)*80+50];
  if(mode==0)assert(pixel==C_BLUE);
  else if(mode!=6&&mode!=7)assert(pixel==C_AMBER);
  else assert(pixel!=C_BLUE&&pixel!=C_AMBER);
  save(argv[1],mode,saver);
 }
 puts("PASS clock and screensaver TX mast: blue only for fresh completed train; orange for enabled waiting/stalled/fault, absent for OFF/RX");
}
