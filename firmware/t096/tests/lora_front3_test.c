/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Native indexed pixels and production renderer, no physical display. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/ui_render.c"
static uint8_t fb[R2_UI_FRAME_BYTES];
static void text_at(int x,int y,const char *word,uint8_t color){
 uint8_t expected[R2_UI_FRAME_BYTES]={0};label(expected,x,y,word,1,color);unsigned ink=0;
 for(unsigned i=0;i<sizeof expected;i++)if(expected[i]==color){if(fb[i]!=color)fprintf(stderr,"label %s at %d,%d mismatch %u,%u\n",word,x,y,i%80,i/80);assert(fb[i]==color);ink++;}assert(ink);
}
static void save(const char *dir,unsigned row,unsigned page,struct r2_ui_model *m){
 struct r2_ui_status s={.page=page};r2_ui_render(fb,m,&s,1000);
 char path[512];snprintf(path,sizeof path,"%s/front3-%u-%u.ppm",dir,row,page);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 uint8_t rgb[R2_UI_BYTES];assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.profile_supported=true,.profile_rc=true,.ble_rx_supported=true,.unit="RX-A",.upstream_name="Air TX",.temperature_valid=true,.temperature_qc=140,
  .clock={.pps_present=true,.pps_age_us=50000,.gps_age_us=-1,.rx_age_us=-1},
  .lora={.requested=true,.active=true,.choice=RC8_SELECT_LORA,.state=RC8_AVAILABLE,.sampled_us=1000000,.utc_us=36642000000ULL,.until_us=9000000,.display_until_us=17000000,.age_ms=4000,.link_samples=16,.link_rms_ns=8123,.fit_age_ms=4010,.rssi_valid=true,.rssi_half_dbm=-163,.rx_age_ms=20,.activity_age_ms=20,.activity_kind=1}};
 struct r2_ui_status s={0};r2_ui_render(fb,&m,&s,1000);
 text_at(36,20,"LOR",C_GREEN);text_at(26,122,"FIT RMS",C_DIM);text_at(10,147,"RC",C_DIM);
 assert(fb[21*80+68]==C_BLUE); /* actual recent local GNSS PPS, not LoRa lock */
 for(unsigned i=0;i<3;i++)assert(fb[(153-2*i)*80+50+6*i]==C_BLUE);
 assert(fb[147*80+68]!=C_BLUE); /* -81.5dBm has three, not four, bars */
 text_at(30,93,"25",C_GREEN); /* compact .25Hz inside original badge */
 assert(lora_rms_current(&m,1000));m.lora.fit_age_ms=23999;assert(lora_rms_current(&m,1000));assert(!lora_rms_current(&m,1001));m.lora.fit_age_ms=4010;
 char provider[9];lora_header_name(provider,"1234567\xc3\xa9");assert(!strcmp(provider,"1234567"));
 s.page=1;r2_ui_render(fb,&m,&s,1000);text_at(7,(int)ui_rf_lamp_y(UI_RF_LORA,false)-10,"LORA RX",C_DIM);
 text_at(45,99,"TRACK",C_GREEN);assert(fb[(ui_rf_lamp_y(UI_RF_LORA,false)+2)*80+ui_rf_lamp_x(UI_RF_TIME)+2]==C_BLUE);
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(10,4,"LOR TIMING",C_WHITE);text_at(4,33,"FIT RMS",C_DIM);text_at(4,77,"RSSI -81dBm",C_WHITE);
 int64_t utc;bool coarse;struct r2_ui_model held=m;held.lora.state=RC8_EXPIRED;held.lora.choice=RC8_SELECT_NONE;held.lora.display_held=true;held.lora.until_us=1000000;held.lora.display_until_us=9000000;
 assert(r2_ui_display_time(&held,1000,&utc,&coarse)&&coarse&&r2_ui_analog(&held,1000));
 held.sampled_ms=8999;held.lora.sampled_us=8999000;held.lora.utc_us+=7999000;assert(r2_ui_analog(&held,8999));
 held.sampled_ms=9000;assert(!r2_ui_analog(&held,9000));held=m;held.lora.state=RC8_EXPIRED;held.lora.choice=RC8_SELECT_NONE;held.lora.display_held=true;held.lora.until_us=1000000;held.lora.display_until_us=9000001;
 assert(!r2_ui_analog(&held,1000)); /* forged display continuation beyond20s denied */
 for(unsigned row=0;row<7;row++){
  struct r2_ui_model v=m;
  if(row==1){v.profile_rc=false;v.clock.pps_present=false;}
  if(row==2){v.lora.state=RC8_EXPIRED;v.lora.choice=RC8_SELECT_NONE;v.lora.display_held=true;v.lora.until_us=1000000;v.lora.display_until_us=9000000;v.lora.age_ms=14000;v.lora.rssi_valid=false;}
  if(row==3){v.lora.state=RC8_ACQUIRING;v.lora.choice=RC8_SELECT_NONE;v.lora.utc_us=0;v.lora.link_samples=0;v.lora.rssi_valid=false;strcpy(v.upstream_name,"1234");}
  if(row==4){v.lora.active=false;v.lora.fault=true;v.lora.choice=RC8_SELECT_NONE;v.lora.utc_us=0;}
  if(row==5){v.lora.requested=v.lora.active=false;v.lora.choice=RC8_SELECT_NONE;v.lora.utc_us=0;}
  if(row==6){v.lora.choice=RC8_SELECT_BLE;v.ble_rx_requested=v.ble_active=true;v.lora.requested=false;v.clock.valid=true;v.clock.utc_us=36642000000LL;v.role=R2_UI_GM;v.scope_active=true;v.clock.source_available=true;}
  for(unsigned page=0;page<3;page++){
   save(argv[1],row,page,&v);
   if(row==4&&page==1){text_at(7,(int)ui_rf_lamp_y(UI_RF_LORA,false)-10,"LORA FAULT",C_DIM);text_at(45,99,"FAULT",C_RED);}
   if(row==4&&page==2)text_at(4,20,"RADIO FAULT",C_RED);
  }
 }
 puts("PASS restored native front3: LOR, .25Hz, original mode/RX bars, actual PPS, real relative RMS/RSSI, RF RX and bounded display hold");
}
