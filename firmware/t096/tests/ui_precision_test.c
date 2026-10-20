/* Copyright (c) 2026 Jonathan Yantis. MIT. Offline production UI checks. */
#include "../src/ui_render.c"
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
 r2_ui_render(fb,m,s,now);char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof fb;i+=2){unsigned p=((unsigned)fb[i]<<8)|fb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 struct r2_ui_model m={.sampled_ms=1000,.profile_supported=true,.platform_ready=true,.ble_active=true,
  .scope_active=true,.authenticated_gm=true,.role=R2_UI_FOLLOWER,.gm_capable=true,.local_hot_standby=true,
  .clock={.pps_present=true,.pps_age_us=0,.fix_active=true,.sats_used=22},
  .precision={.authorized=true,.map_valid=true,.locked=true}};
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
 assert(!r2_ui_witness_page(&m,2));m.profile_rc=false;assert(r2_ui_witness_page(&m,2));
 assert(r2_ui_witness_page(&m,6)&&!r2_ui_witness_page(&m,0));
 struct r2_ui_status s={.page=2,.holds={.pps_since=2,.hot_since=32},
  .shadow={.state=R2_SHADOW_RUNNING,.active=true,.valid=true,.mean_tenth_ns=-184,.rms_ns=63,.samples=436}};
 fixture(argv[1],"precision-hot",&m,&s,62000);
 assert(color_count(20,7,C_GREEN)&&!color_count(20,7,C_AMBER));
 assert(color_count(32,10,C_GREEN)&&!color_count(32,10,C_AMBER));
 assert(color_count(67,7,C_CYAN)&&color_count(89,7,C_CYAN));
 memcpy(locked_fb,fb,sizeof fb);
 m.precision.locked=false; /* HOT remains eligible across short fit-lock changes. */
 fixture(argv[1],"precision-hot-fit-unlocked",&m,&s,62000);
 assert(!memcmp(locked_fb,fb,80*2*44));
 s.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_SETTLING,.settling_ms=5000};
 fixture(argv[1],"precision-settling",&m,&s,62000);assert(!color_count(67,29,C_CYAN));
 s.shadow=(struct r2_shadow_session_view){.state=R2_SHADOW_RUNNING,.active=true,.valid=true,.mean_tenth_ns=-5000000000LL,.rms_ns=500000000,.samples=UINT32_MAX};
 fixture(argv[1],"precision-large",&m,&s,62000);
 m.clock.fix_active=m.clock.pps_present=false;m.local_hot_standby=false;
 fixture(argv[1],"precision-no-gps",&m,&s,62000);assert(!color_count(67,29,C_CYAN));
 fixture(argv[1],"precision-stale",&m,&s,65001);assert(!color_count(20,24,C_GREEN));
 m.role=R2_UI_GM;m.clock.fix_active=m.clock.pps_present=m.gm_source_fresh=true;m.pps_precision.rate_valid=true;
 fixture(argv[1],"precision-serving-gm",&m,&s,62000);assert(!color_count(67,29,C_CYAN));
 m.role=R2_UI_FOLLOWER;m.local_hot_standby=false;m.source_eligible=false;m.admission_ms=42000;
 fixture(argv[1],"precision-hot-dwell",&m,&s,62000);assert(!strcmp(hot_reason(&m,true,false),"SOURCE DWELL")&&color_count(150,7,C_DIM));
 m.source_eligible=true;
 const unsigned refusals[]={1,4,16,32};const char *reasons[]={"PEER WAIT","PEER SOURCE","AUTH WAIT","BLE MAP WAIT"};
 for(unsigned i=0;i<4;i++){m.hot_refusal=refusals[i];assert(!strcmp(hot_reason(&m,true,false),reasons[i]));}
 m.hot_refusal=16;fixture(argv[1],"precision-hot-auth",&m,&s,62000);assert(color_count(150,7,C_DIM));
 m.profile_rc=m.boundary_clock=true;m.role=R2_UI_FOLLOWER;
 fixture(argv[1],"precision-rc",&m,&s,62000);
 puts("PASS Precision: real holds/loss/staleness, stable green HOT/SoftPLL, witness eligibility, missing and extreme values");
}
