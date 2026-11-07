/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual renderer and validity predicates; synthetic state, no hardware. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui_render.c"
static uint8_t fb[R2_UI_FRAME_BYTES],digital[R2_UI_FRAME_BYTES];
static void text_at(int x,int y,const char *word,uint8_t color){
 uint8_t expected[R2_UI_FRAME_BYTES]={0};label(expected,x,y,word,1,color);unsigned n=0;
 for(unsigned i=0;i<sizeof expected;i++)if(expected[i]==color){assert(fb[i]==color);++n;}assert(n);
}
static void save(const char *dir,const char *name){
 char path[1024];snprintf(path,sizeof path,"%s/watch-%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 uint8_t rgb[R2_UI_BYTES];assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
static struct r2_ui_model example(void){
 return (struct r2_ui_model){.sampled_ms=1000,.platform_ready=true,.unit="TC-02",.upstream_name="TC-03",.root_name="TC-04",
  .profile_supported=true,.profile_rc=true,.ble_rx_supported=true,
  .lora={.requested=true,.active=true,.state=RC8_EXPIRED,.sampled_us=1000000,
   .metadata_cached=true,.observer_available=true,.context_known=true,.display_acquiring=true,
   .utc_us=UINT64_C(43200000000),.display_until_us=UINT64_MAX,.link_samples=1,
   .rx_age_ms=2000,.age_ms=2000,.fit_age_ms=2000,.activity_age_ms=UINT32_MAX,
   .prediction_rms_ns=UINT32_MAX,.prediction_age_ms=UINT32_MAX,.tz={2,-360}}};
}
int main(int argc,char **argv){assert(argc==2);
 struct r2_ui_model m=example();struct r2_ui_status s={0};int64_t utc,local;bool coarse;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==(int64_t)m.lora.utc_us&&coarse);
 assert(!r2_ui_analog(&m,1000)&&!r2_ui_face_locked(&m,1000)&&!r2_ui_saver_allowed(&m,1000));
 assert(!strcmp(r2_ui_clock_zone(&m,1000,utc,&local),"CST")&&local==utc-INT64_C(21600000000));
 r2_ui_render(fb,&m,&s,1000);text_at(13,61,"ACQ / EST",C_AMBER);text_at(7,103,"ERR UNKNOWN",C_DIM);text_at(26,122,"PRED RMS",C_DIM);text_at(13,133,"NEXT PAIR",C_AMBER);
 memcpy(digital,fb,sizeof digital);save(argv[1],"first-pair");
 m.lora.state=RC8_AVAILABLE;m.lora.choice=RC8_SELECT_LORA;m.lora.until_us=2000000;m.lora.time_valid=m.lora.quality_known=true;m.lora.display_acquiring=false;
 m.lora.prediction_n=8;m.lora.prediction_rms_ns=675;m.lora.prediction_age_ms=2000;
 r2_ui_render(fb,&m,&s,1000);
 for(unsigned y=30;y<49;y++)for(unsigned x=5;x<76;x++)assert(fb[y*80+x]==digital[y*80+x]);
 assert(r2_ui_analog(&m,1000));save(argv[1],"qualified");
 m=example();m.lora.link_samples=2;m.lora.fit_span_ms=30000;m.lora.prediction_n=1;m.lora.prediction_rms_ns=81230;m.lora.prediction_age_ms=2000;
 r2_ui_render(fb,&m,&s,1000);assert(lora_rms_current(&m,1000));text_at(26,122,"PRED RMS",C_DIM);save(argv[1],"pair2-error");
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(4,33,"PRED RMS",C_DIM);text_at(4,61,"N2 SPAN30s",C_DIM);save(argv[1],"pair2-detail");
 s.page=0;m.lora.context_known=false;m.lora.display_acquiring=false;m.lora.display_held=true;m.lora.link_samples=16;m.lora.fit_span_ms=300000;m.lora.prediction_rms_ns=675;m.lora.prediction_n=20;
 r2_ui_render(fb,&m,&s,1000);text_at(10,61,"META STALE",C_AMBER);assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&lora_rms_current(&m,1000));save(argv[1],"meta-stale");
 m.lora.rx_age_ms=m.lora.age_ms=m.lora.prediction_age_ms=90000;
 r2_ui_render(fb,&m,&s,1000);text_at(10,61,"HOLD / EST",C_AMBER);text_at(16,89,"PAIR 90s",C_DIM);text_at(19,103,"ERR 90s",C_DIM);assert(!r2_ui_saver_allowed(&m,1000));save(argv[1],"two-pair-loss");
 m.lora.rx_age_ms=m.lora.age_ms=m.lora.prediction_age_ms=1000000;
 r2_ui_render(fb,&m,&s,1000);assert(r2_ui_display_time(&m,1000,&utc,&coarse));save(argv[1],"long-hold");
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(4,20,"HOLD / EST",C_AMBER);text_at(4,121,"META CACHED",C_DIM);save(argv[1],"hold-detail");
 s.page=R2_UI_LORA_PAGE;r2_ui_render(fb,&m,&s,1000);text_at(4,44,"HOLD / EST",C_WHITE);text_at(4,58,"12:00:00",C_WHITE);save(argv[1],"hold-radio");
 m.lora.rx_age_ms=m.lora.prediction_age_ms=UINT32_MAX-1;m.lora.fit_span_ms=UINT32_MAX;m.lora.link_samples=255;
 r2_ui_render(fb,&m,&s,1000);save(argv[1],"bounded-ages");
 m=example();m.lora.display_acquiring=false;m.lora.context_known=false;m.lora.utc_us+=7000000;
 m.ble_rx_requested=m.ble_active=m.ble_time_selected=true;m.role=R2_UI_FOLLOWER;m.scope_active=m.authenticated_gm=true;
 m.clock.valid=true;m.clock.utc_us=UINT64_C(43200000000);m.ble_valid_until_us=2000000;
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==m.clock.utc_us&&!coarse);
 s.page=1;r2_ui_render(fb,&m,&s,1000);text_at(16,85,"12:00:00",C_WHITE);text_at(16,110,"12:00:07",C_WHITE);save(argv[1],"independent-observer");
 /* BLE acquisition/current time wins over an unqualified LoRa watch cache.
  * The independent RF clock remains visible and source metadata is retained. */
 m.lora.display_held=true;m.lora.rx_age_ms=90000;m.sync_attempt=true;
 assert(!r2_ui_lora_primary(&m,1000));
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&utc==m.clock.utc_us&&!coarse);
 s.page=0;r2_ui_render(fb,&m,&s,1000);text_at(19,62,"SYNCING",C_AMBER);save(argv[1],"ble-sync-over-lora-hold");
 m.lora.display_held=false;m.lora.display_acquiring=true;m.lora.rx_age_ms=2000;
 assert(!r2_ui_lora_primary(&m,1000));r2_ui_render(fb,&m,&s,1000);text_at(19,62,"SYNCING",C_AMBER);save(argv[1],"ble-sync-over-lora-est");
 m.lora.choice=RC8_SELECT_LORA;m.lora.state=RC8_AVAILABLE;m.lora.until_us=2000000;
 m.lora.time_valid=m.lora.quality_known=m.lora.context_known=true;m.lora.display_acquiring=false;
 assert(r2_ui_lora_primary(&m,1000)); /* Qualified fallback retains its owner's return dwell. */
 m.lora.state=RC8_EXPIRED;m.lora.choice=RC8_SELECT_NONE;m.lora.time_valid=m.lora.quality_known=false;m.lora.display_held=true;
 m.ble_active=false;m.sync_attempt=false;m.ble_time_selected=false;
 assert(r2_ui_lora_primary(&m,1000)); /* BLE absence does not erase field HOLD/cache. */
 /* Restored RF values are one owner-computed same-tick comparison. */
 m=example();m.lora.display_acquiring=false;
 m.ble_rx_requested=m.ble_active=m.ble_time_selected=true;m.role=R2_UI_FOLLOWER;m.scope_active=m.authenticated_gm=true;
 m.clock.valid=true;m.clock.utc_us=UINT64_C(43200000000);m.ble_valid_until_us=2000000;
 m.lora.state=RC8_AVAILABLE;m.lora.until_us=2000000;m.lora.time_valid=m.lora.quality_known=true;
 m.rf_comparison=(struct r2_ui_rf_comparison){.sampled_us=1000000,.until_us=2000000,.delta_ns=675,.drift_ppb=-42,.flags=R2_UI_RF_DELTA|R2_UI_RF_RATE};
 s.page=1;r2_ui_render(fb,&m,&s,1000);text_at(51,136,"+675",C_CYAN);text_at(57,145,"-42",C_DIM);save(argv[1],"rf-comparison");
 m.rf_comparison.flags|=R2_UI_RF_ESTIMATE;m.lora.quality_known=false;
 r2_ui_render(fb,&m,&s,1000);text_at(51,136,"+675",C_AMBER);text_at(57,145,"-42",C_AMBER);save(argv[1],"rf-estimate");
 m.rf_comparison.flags=R2_UI_RF_DELTA|R2_UI_RF_ESTIMATE;
 r2_ui_render(fb,&m,&s,1000);text_at(63,145,"--",C_AMBER);save(argv[1],"rf-first-pair-rate-unknown");
 m.rf_comparison.until_us=1000000;
 r2_ui_render(fb,&m,&s,1000);text_at(63,136,"--",C_DIM);text_at(63,145,"--",C_DIM);save(argv[1],"rf-expired");
 m.rf_comparison.until_us=2000000;m.lora.rx_age_ms=90000;
 r2_ui_render(fb,&m,&s,1000);text_at(63,136,"--",C_DIM);save(argv[1],"rf-hold-no-comparison");
 m=example();m.lora.display_acquiring=false;m.lora.observer_available=false;m.lora.utc_us=0;m.lora.link_samples=0;
 assert(!r2_ui_display_time(&m,1000,&utc,&coarse));s.page=0;r2_ui_render(fb,&m,&s,1000);save(argv[1],"new-context");
 m=example();m.lora.fault=true;assert(!r2_ui_display_time(&m,1000,&utc,&coarse));r2_ui_render(fb,&m,&s,1000);text_at(7,62,"RADIO FAULT",C_RED);save(argv[1],"fault");
 m=example();assert(!r2_ui_display_time(&m,3501,&utc,&coarse));
 puts("PASS production pixels: first-pair digital UTC/cachedTZ, pair2 actual error, same-context META loss/90s/1000sHOLD, no fake dial/lease, BLE isolation and field loss states");
}
