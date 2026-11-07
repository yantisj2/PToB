/* MIT. Jonathan Yantis, 2026. Production pixels and advancing UTC; no physical claims. */
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
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.unit="TEST",.ble_rx_supported=true,.lora={.requested=true,.active=true,.choice=RC8_SELECT_NONE,.state=RC8_EXPIRED,.display_held=true,.context_known=true,.fit_span_ms=150000,.sampled_us=1000000,.utc_us=UINT64_C(86399900000),.until_us=1000000+R2_LORA_DISPLAY_HOLD_US,.display_until_us=UINT64_MAX,.age_ms=700000,.link_samples=6,.link_rms_ns=4552}};
 int64_t utc,prior=0;bool coarse;uint64_t start=(uint64_t)UINT32_MAX-5;
 for(uint64_t t=start;t<start+2000;t+=25){
  m.sampled_ms=t;m.lora.sampled_us=t*1000;m.lora.utc_us=UINT64_C(86399900000)+(t-start)*1000;
  assert(r2_ui_display_time(&m,t,&utc,&coarse)&&coarse&&r2_ui_analog(&m,t));if(prior)assert(utc-prior==25000);prior=utc;
  assert(!lora_input_current(&m,t)&&!lora_rms_current(&m,t));
 }
 m.sampled_ms=1000;m.lora.sampled_us=1000000;m.lora.utc_us=UINT64_C(86400100000);
 assert(!strcmp(r2_ui_servo_state(&m,1000),"HOLD"));
 struct r2_ui_status s={0};r2_ui_render(fb,&m,&s,1000);text_at(36,20,"HOLD",C_AMBER);text_at(26,122,"REF AGE",C_DIM);text_at(28,133,"700s",C_DIM);save(argv[1],"hold-watch");
 m.lora.age_ms=UINT32_MAX;r2_ui_render(fb,&m,&s,1000);text_at(16,133,"4294967s",C_DIM);m.lora.age_ms=700000;
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(4,20,"DISPLAY HOLD",C_AMBER);save(argv[1],"hold-detail");
 s.page=0;s.screensaver=true;r2_ui_render(fb,&m,&s,1000);text_at(36,(int)r2_ui_saver_top(0)+3,"HOLD",C_AMBER);save(argv[1],"hold-saver");
 s.page=0;s.screensaver=false;m.lora.display_held=false;m.lora.display_acquiring=true;
 assert(!strcmp(r2_ui_servo_state(&m,1000),"ACQUIRING"));
 for(uint64_t t=1000;t<=2000;t+=25){
  assert(r2_ui_display_time(&m,t,&utc,&coarse)&&coarse&&utc==(int64_t)m.lora.utc_us+(int64_t)(t-1000)*1000&&r2_ui_analog(&m,t));
  assert(!lora_input_current(&m,t)&&!lora_rms_current(&m,t));
 }
 r2_ui_render(fb,&m,&s,1000);text_at(36,20,"ACQ",C_AMBER);text_at(26,122,"SYNCING",C_DIM);text_at(16,133,"150/180s",C_AMBER);save(argv[1],"acquisition-watch");
 s.page=2;r2_ui_render(fb,&m,&s,1000);text_at(4,20,"LORA SYNC",C_AMBER);text_at(4,61,"6 PAIRS",C_DIM);save(argv[1],"acquisition-detail");
 s.page=0;s.screensaver=true;r2_ui_render(fb,&m,&s,1000);text_at(36,(int)r2_ui_saver_top(0)+3,"ACQ",C_AMBER);save(argv[1],"acquisition-saver");
 m.lora.display_acquiring=false;m.lora.display_held=false;m.lora.state=RC8_AVAILABLE;m.lora.choice=RC8_SELECT_LORA;m.lora.time_valid=m.lora.quality_known=true;
 s.page=0;s.screensaver=false;assert(!strcmp(r2_ui_servo_state(&m,1000),"RECEIVING")&&lora_rms_current(&m,1000));
 m.lora.fit_age_ms=62510;assert(lora_rms_current(&m,1000));m.lora.fit_age_ms=119999;assert(lora_rms_current(&m,1000));m.lora.fit_age_ms=120000;assert(!lora_rms_current(&m,1000));m.lora.fit_age_ms=0;
 r2_ui_render(fb,&m,&s,1000);text_at(36,20,"LOR",C_GREEN);text_at(26,122,"FIT RMS",C_DIM);save(argv[1],"acquisition-trained");
 m.lora.display_held=true;m.lora.state=RC8_EXPIRED;m.lora.choice=RC8_SELECT_NONE;m.lora.time_valid=m.lora.quality_known=false;
 assert(!strcmp(r2_ui_servo_state(&m,1000),"HOLD")&&!lora_rms_current(&m,1000));
 r2_ui_render(fb,&m,&s,1000);text_at(36,20,"HOLD",C_AMBER);text_at(26,122,"REF AGE",C_DIM);save(argv[1],"acquisition-trained-hold");
 /* Presentation expires from precision reception, independently of the
  * much older frozen fit reference and ongoing authenticated control packets. */
 m.lora.age_ms=700000;m.lora.rx_age_ms=120000;
 assert(r2_ui_saver_allowed(&m,1000)&&r2_ui_analog(&m,1000));
 assert(!r2_ui_saver_allowed(&m,1001)&&r2_ui_analog(&m,1001));
 m.lora.rx_age_ms=120001;assert(!r2_ui_saver_allowed(&m,1000));
 m.lora.rx_age_ms=300000;assert(r2_ui_analog(&m,1000)&&r2_ui_face_locked(&m,1000));
 assert(!r2_ui_analog(&m,1001)&&!r2_ui_face_locked(&m,1001));
 m.lora.rx_age_ms=300001;
 assert(!r2_ui_saver_allowed(&m,1000)&&!r2_ui_analog(&m,1000)&&!r2_ui_face_locked(&m,1000));
 assert(r2_ui_display_time(&m,1000,&utc,&coarse)&&coarse);
 assert(!strcmp(r2_ui_servo_state(&m,1000),"ACQUIRING"));
 s.page=0;s.screensaver=true;r2_ui_render(fb,&m,&s,1000);
 text_at(36,20,"ACQ",C_AMBER);text_at(13,62,"LORA SYNC",C_AMBER);text_at(16,82,"PAIR GAP",C_AMBER);save(argv[1],"stale-precision-acquiring");
 m.lora.rx_age_ms=UINT32_MAX;assert(!r2_ui_saver_allowed(&m,1000)&&!r2_ui_analog(&m,1000));
 /* A newly accepted pair permits presentation without discarding cached UTC. */
 m.lora.rx_age_ms=0;assert(r2_ui_saver_allowed(&m,1000)&&r2_ui_analog(&m,1000));
 for(unsigned i=0;i<8;i++){
  struct r2_ui_model bad=m;
  if(i==0)bad.lora.fault=true;if(i==1)bad.lora.requested=false;if(i==2)bad.lora.active=false;if(i==3)bad.profile_switching=true;
  if(i==4)bad.lora.display_held=false;if(i==5)bad.lora.time_valid=true;if(i==6)bad.lora.quality_known=true;if(i==7)bad.sampled_ms=1001;
  assert(!r2_ui_display_time(&bad,1000,&utc,&coarse)&&!r2_ui_analog(&bad,1000));
 }
 puts("PASS slow HOLD pixels/reference age, UTC midnight and32-bit uptime rollover, stale metrics and invalidation guards");
}
