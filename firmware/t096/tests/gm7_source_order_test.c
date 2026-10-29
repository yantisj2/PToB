/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual GM source extraction and owner policy; fake kernel/capture boundaries. */
#define main historical_service_cases
#ifdef R2_SOURCE_ORDER_FIXTURE
#include R2_SOURCE_ORDER_FIXTURE
#else
#include "gm7_service_test.c"
#endif
#undef main
#include <stdlib.h>
static bool read_source(struct gm7_source *s,struct r2_source_clock *m,
                        struct r2_clock *c,struct gm7_bridge *b){
#ifdef R2_SOURCE_ORDER_LEGACY
 return source_read(s,m,c,*b);
#else
 return source_read(s,m,c,b);
#endif
}
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);assert(test<11);
 setup(true);tick_to(test==1?66400000:66999990);healthy_input();
 struct gm7_bridge b;assert(bridge(&b));struct gm7_bridge original=b;
 struct gm7_source s;struct r2_source_clock map;struct r2_clock c;struct gm7_preparation prep={0};
 if(test<=1){
  int64_t prior_rmc=input.last_rmc;
  tick_to(test==1?66400020:67000010);healthy_input();
  if(test==0)input.last_rmc=prior_rmc; /* Isolate newer PPS, RMC stays prior. */
  assert(test==0?input.last_pps>(int64_t)b.kernel_hi:input.last_rmc>(int64_t)b.kernel_hi);
  assert(read_source(&s,&map,&c,&b));struct gm7_bridge now;assert(bridge(&now));
  gm7_owner_update(&owner,&s,&prep,now.hf);
  printf("case%u inputvalid=%u fix=%u PPS=%u source=%u/%u/%u/%u eligible=%u serving=%u reason=%u bridgeadvanced=%u\n",test,c.view.valid,c.view.fix_active,c.view.pps_present,s.gnss_healthy,s.pps_healthy,s.utc_available,s.physical_ready,owner.eligible,owner.serving,owner.reason,b.hf>original.hf);
  if(!owner.eligible||!owner.serving)return 1;
  assert(s.gnss_healthy&&s.pps_healthy&&s.utc_available&&s.physical_ready&&b.hf==fake_hf);
  assert(s.source_gen==3&&s.capture_gen==4&&s.association_gen==1);
 }else if(test==2){
  input.last_pps=(int64_t)fake_kernel+1;
  assert(read_source(&s,&map,&c,&b)&&!s.pps_healthy&&!s.gnss_healthy);
 }else if(test==3){
  input.last_pps=(int64_t)fake_kernel-R2_PPS_FRESH_US;
  assert(read_source(&s,&map,&c,&b)&&!s.pps_healthy&&!s.gnss_healthy);
 }else if(test==4){
  input.last_rmc=(int64_t)fake_kernel-R2_GPS_FRESH_US;
  assert(read_source(&s,&map,&c,&b)&&s.pps_healthy&&!s.gnss_healthy);
 }else if(test==5){
  uint64_t original_until=(uint64_t)input.last_pps+R2_PPS_FRESH_US,converted;
  fake_capture_advance=20000;
  assert(read_source(&s,&map,&c,&b)&&s.physical_ready&&s.sampled_us==fake_hf&&s.sampled_us>b.hf);
  assert(gm7_bridge_to_hf(b,original_until,&converted)&&s.until_us==converted);
  uint64_t utc;assert(!source_phase(&s,b,&utc));assert(bridge(&b)&&source_phase(&s,b,&utc));
 }else if(test==6||test==7){
  if(test==6)fake_clock_epoch=5;else fake_hf_valid=false;
  assert(!read_source(&s,&map,&c,&b)&&resource_fault&&!memcmp(&b,&original,sizeof b));
 }else if(test==8){
  r2_source_clock_invalidate(&functional_source);input.translated_epoch=3;
  input.translated_at_us=(int64_t)fake_kernel+1;input.translated_utc_us=INT64_C(1792800000000000)+(int64_t)fake_hf;
  assert(read_source(&s,&map,&c,&b)&&s.gnss_healthy&&s.pps_healthy&&!s.utc_available&&!s.physical_ready);
 }else if(test==9){
  input.stopped=true;assert(read_source(&s,&map,&c,&b)&&!s.gnss_healthy&&!s.physical_ready);
 }else{
  input.fatal=true;assert(read_source(&s,&map,&c,&b)&&!s.gnss_healthy&&!s.physical_ready);
 }
 printf("PASS actual source order/freshness guard case%u\n",test);return 0;
}
