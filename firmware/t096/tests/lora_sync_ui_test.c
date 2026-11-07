/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Production-rendered sync states and independent input diagnostics only. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui_render.c"
static uint8_t fb[R2_UI_FRAME_BYTES];
static void text_at(int x,int y,const char *word,uint8_t color){
 uint8_t expected[R2_UI_FRAME_BYTES]={0};label(expected,x,y,word,1,color);unsigned n=0;
 for(unsigned i=0;i<sizeof expected;i++)if(expected[i]==color){assert(fb[i]==color);++n;}assert(n);
}
static void save(const char *dir,const char *name){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 uint8_t rgb[R2_UI_BYTES];assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.unit="TC-02",.upstream_name="TC-03",
  .profile_supported=true,.profile_rc=true,.ble_rx_supported=true,
  .lora={.requested=true,.active=true,.state=RC8_ACQUIRING,.sampled_us=1000000,
   .age_ms=UINT32_MAX,.rx_age_ms=UINT32_MAX,.activity_age_ms=UINT32_MAX,.link_rms_ns=UINT32_MAX}};
 struct r2_ui_status s={0};int64_t utc;bool coarse;
 r2_ui_render(fb,&m,&s,1000);text_at(13,62,"LORA SYNC",C_AMBER);text_at(13,82,"WAIT META",C_AMBER);text_at(26,122,"SYNCING",C_DIM);save(argv[1],"wait-meta");
 assert(!r2_ui_display_time(&m,1000,&utc,&coarse)&&!r2_ui_analog(&m,1000));
 m.lora.context_known=true;r2_ui_render(fb,&m,&s,1000);text_at(13,82,"WAIT PAIR",C_AMBER);save(argv[1],"wait-pair");
 m.lora.link_samples=4;m.lora.fit_span_ms=90000;m.lora.rx_age_ms=m.lora.age_ms=14000;m.lora.time_valid=true;
 r2_ui_render(fb,&m,&s,1000);text_at(19,62,"LORA RX",C_AMBER);text_at(16,82,"TRAINING",C_AMBER);text_at(19,133,"90/180s",C_AMBER);save(argv[1],"training-watch");
 assert(!r2_ui_display_time(&m,1000,&utc,&coarse)&&!r2_ui_analog(&m,1000));
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(4,20,"LORA SYNC",C_AMBER);text_at(4,33,"BUILDING FIT",C_DIM);text_at(4,61,"4 PAIRS",C_DIM);save(argv[1],"training-detail");
 s.page=0;m.lora.rx_age_ms=90000;r2_ui_render(fb,&m,&s,1000);text_at(16,82,"PAIR GAP",C_AMBER);text_at(19,133,"90/180s",C_AMBER);save(argv[1],"pair-gap");
 /* Cached acquisition UTC stays explicitly syncing; no precision or fake hands. */
 m.lora.display_acquiring=true;m.lora.state=RC8_EXPIRED;m.lora.time_valid=false;m.lora.utc_us=UINT64_C(43200000000);m.lora.until_us=UINT64_MAX;m.lora.display_until_us=UINT64_MAX;m.lora.rx_age_ms=14000;
 r2_ui_render(fb,&m,&s,1000);text_at(26,122,"SYNCING",C_DIM);text_at(19,133,"90/180s",C_AMBER);save(argv[1],"cached-acquisition");
 s.screensaver=true;r2_ui_render(fb,&m,&s,1000);text_at(16,(int)r2_ui_saver_top(0)+43,"TRAINING",C_AMBER);save(argv[1],"acquisition-saver");
 /* RF page keeps independent BLE and LoRa UTC even with BLE selected. */
 s.screensaver=false;s.page=1;m.ble_rx_requested=m.ble_active=m.ble_time_selected=true;m.role=R2_UI_FOLLOWER;m.scope_active=m.authenticated_gm=true;
 m.clock.valid=true;m.clock.utc_us=UINT64_C(43200000000);m.ble_valid_until_us=2000000;
 m.lora.display_acquiring=false;m.lora.state=RC8_AVAILABLE;m.lora.choice=RC8_SELECT_NONE;m.lora.time_valid=m.lora.quality_known=true;m.lora.utc_us=UINT64_C(43207000000);m.lora.until_us=2000000;m.lora.fit_age_ms=0;
 r2_ui_render(fb,&m,&s,1000);text_at(16,85,"12:00:00",C_WHITE);text_at(16,110,"12:00:07",C_WHITE);save(argv[1],"independent-rf");
 m.lora.choice=RC8_SELECT_LORA;r2_ui_render(fb,&m,&s,1000);text_at(16,85,"12:00:00",C_WHITE);text_at(16,110,"12:00:07",C_WHITE);
 m.lora.transmitter=true;m.lora.state=RC8_AVAILABLE;m.lora.age_ms=1000;r2_ui_render(fb,&m,&s,1000);text_at(11,110,"TX NO CLOCK",C_DIM);save(argv[1],"transmitter-rf");m.lora.transmitter=false;
 /* Only decoded timing and unknown packets pulse; zero-init/noise stays off. */
 m.lora.activity_age_ms=0;m.lora.activity_kind=0;r2_ui_render(fb,&m,&s,1000);uint8_t dark[R2_UI_FRAME_BYTES];memcpy(dark,fb,sizeof dark);
 m.lora.activity_kind=1;r2_ui_render(fb,&m,&s,1000);assert(memcmp(dark,fb,sizeof dark));save(argv[1],"timing-rx");
 m.lora.activity_kind=3;r2_ui_render(fb,&m,&s,1000);assert(memcmp(dark,fb,sizeof dark));save(argv[1],"unknown-rx");
 m.lora.activity_kind=2;r2_ui_render(fb,&m,&s,1000);assert(!memcmp(dark,fb,sizeof dark));
 s.help=true;r2_ui_render(fb,&m,&s,1000);save(argv[1],"rf-help");
 puts("PASS actual pixels: current-context sync phases/span, independent BLE/LoRa times, TX state and decoded-only RX lamps");
}
