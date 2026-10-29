/* Copyright (c) 2026 Jonathan Yantis. MIT. Actual renderer, no hardware. */
#include "../src/ui_render.c"
#include <assert.h>
static uint8_t actual[R2_UI_FRAME_BYTES],reference[R2_UI_FRAME_BYTES],utc_frame[R2_UI_FRAME_BYTES];
static void save(const char *dir,const char *name,const uint8_t *frame){
 char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<R2_UI_FRAME_BYTES;i++){uint8_t pixel[2];assert(r2_ui_expand_rgb565(pixel,2,frame+i,1));uint16_t c=((uint16_t)pixel[0]<<8)|pixel[1];
  fputc((c>>11)*255/31,f);fputc(((c>>5)&63)*255/63,f);fputc((c&31)*255/31,f);}
 fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.unit="TC-02",.platform_ready=true,
  .profile_supported=true,.scope_active=true,.role=R2_UI_GM,.ble_active=true,.gm_source_fresh=true,
  .clock={.valid=true,.source_available=true,.utc_us=36642250000LL,.pps_present=true,.pps_age_us=0},
  .tz={2,-300},.tz_until_us=1750000};
 struct r2_ui_status s={0};int64_t shown;
 assert(!strcmp(r2_ui_clock_zone(&m,1000,m.clock.utc_us,&shown),"CDT")&&shown==18642250000LL);
 assert(m.clock.utc_us==36642250000LL);
 r2_ui_render(actual,&m,&s,1000);save(argv[1],"clock-cdt",actual);
 struct r2_ui_model local=m;local.clock.utc_us=shown;local.tz_until_us=0;
 r2_ui_render(reference,&local,&s,1000);
 fill(reference,8,20,18,7,C_PANEL);label(reference,8,20,"CDT",1,C_DIM);
 assert(!memcmp(actual,reference,sizeof actual)); /* Both faces + the label. */
 local.clock.utc_us=m.clock.utc_us;r2_ui_render(utc_frame,&local,&s,1000);save(argv[1],"clock-utc",utc_frame);
 assert(memcmp(actual+80*29,utc_frame+80*29,80*23)); /* Digital changes. */
 assert(memcmp(actual+80*54,utc_frame+80*54,80*62)); /* Analog changes. */
 s.page=2;r2_ui_render(actual,&m,&s,1000);r2_ui_render(reference,&local,&s,1000);
 assert(!memcmp(actual,reference,sizeof actual)); /* Diagnostics unchanged. */
 s.page=0;s.screensaver=true;r2_ui_render(actual,&m,&s,1000);save(argv[1],"saver-cdt",actual);
 local.clock.utc_us=18642250000LL;r2_ui_render(reference,&local,&s,1000);
 fill(reference,8,7,18,7,C_PANEL);label(reference,8,7,"CDT",1,C_DIM);
 assert(!memcmp(actual,reference,sizeof actual));
 s.screensaver=false;m.tz.offset_min=-360;
 assert(!strcmp(r2_ui_clock_zone(&m,1000,m.clock.utc_us,&shown),"CST")&&shown==15042250000LL);
 r2_ui_render(actual,&m,&s,1000);save(argv[1],"clock-cst",actual);
 /* Known coarse TZ labels survive loss of the precision/time presentation. */
 struct r2_ui_model blank=m;blank.clock.valid=false;blank.tz=r2_tz_manual();
 r2_ui_render(actual,&blank,&s,1000);blank.tz_until_us=0;r2_ui_render(reference,&blank,&s,1000);
 fill(reference,8,20,18,7,C_PANEL);label(reference,8,20,"CDT",1,C_DIM);
 assert(!memcmp(actual,reference,sizeof actual));
 /* UTC02:00 is local21:00 on the previous date, shared by both faces. */
 struct r2_ui_model rollover=m;rollover.tz=r2_tz_manual();rollover.clock.utc_us=7200000000LL;
 r2_ui_render(actual,&rollover,&s,1000);rollover.clock.utc_us=75600000000LL;rollover.tz_until_us=0;
 r2_ui_render(reference,&rollover,&s,1000);fill(reference,8,20,18,7,C_PANEL);label(reference,8,20,"CDT",1,C_DIM);
 assert(!memcmp(actual,reference,sizeof actual));
 /* Local midnight/date wrapping, fractional offsets, unfamiliar IDs. */
 m.tz=(struct r2_tz){2,-300};assert(!strcmp(r2_ui_clock_zone(&m,1000,3600000000LL,&shown),"CDT")&&shown==-14400000000LL);
 m.tz=(struct r2_tz){42,345};assert(!strcmp(r2_ui_clock_zone(&m,1000,0,&shown),"LOC")&&shown==20700000000LL);
 m.tz=(struct r2_tz){1,0};assert(!strcmp(r2_ui_clock_zone(&m,1000,123,&shown),"UTC")&&shown==123);
 m.tz=r2_tz_manual();assert(!strcmp(r2_ui_clock_zone(&m,1749,123,&shown),"CDT"));
 assert(!strcmp(r2_ui_clock_zone(&m,1750,123,&shown),"UTC")&&shown==123);
 assert(!strcmp(r2_ui_clock_zone(&m,999,123,&shown),"UTC")&&shown==123);
 assert(!strcmp(r2_ui_clock_zone(&m,3501,123,&shown),"UTC")&&shown==123);
 assert(!strcmp(r2_ui_clock_zone(&m,UINT64_MAX,123,&shown),"UTC")&&shown==123);
 assert(!strcmp(r2_ui_clock_zone(&m,1000,INT64_MIN,&shown),"UTC")&&shown==INT64_MIN);
 m.tz=(struct r2_tz){42,345};assert(!strcmp(r2_ui_clock_zone(&m,1000,INT64_MAX,&shown),"UTC")&&shown==INT64_MAX);
 m.tz=r2_tz_unknown();assert(!strcmp(r2_ui_clock_zone(&m,1000,123,&shown),"UTC")&&shown==123);
 r2_ui_render(actual,&m,&s,1000);assert(!memcmp(actual,utc_frame,sizeof actual));
 m.tz=r2_tz_manual();m.tz_until_us=1000000;r2_ui_render(actual,&m,&s,1000);
 assert(!memcmp(actual,utc_frame,sizeof actual));
#ifdef R2_LORA
 m.tz=r2_tz_manual();m.tz_until_us=UINT64_MAX;m.lora.choice=RC8_SELECT_LORA;
 assert(!strcmp(r2_ui_clock_zone(&m,1000,123,&shown),"UTC")&&shown==123);
#endif
 puts("PASS front clock and saver: CDT/CST shared analog/digital offset, exact label, expiry/unknown UTC fallback, diagnostics/model UTC unchanged");
}
