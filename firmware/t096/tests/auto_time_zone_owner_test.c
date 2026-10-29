/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main original_time_owner_main
#include "time_service_test.c"
#undef main
#include "time_zone_config.h"
int main(void){
 assert(!original_time_owner_main());
 ticks=0;current_capture=0;r2_time_init();r2_time_start();
 for(unsigned second=0;second<=125;second++){
  char body[100],line[128];
  int n=snprintf(body,sizeof body,"GNRMC,12%02u%02u,A,4152.0000,N,08737.8000,W,0,0,291026,,,A",second/60,second%60);
  assert(n>0&&(size_t)n<sizeof body);unsigned checksum=0;
  for(const char *p=body;*p;p++)checksum^=(unsigned char)*p;
  n=snprintf(line,sizeof line,"$%s*%02X\n",body,checksum);assert(n>0&&(size_t)n<sizeof line);
  ticks=(int64_t)second*1000000;
  /* Preserve the real two-chunk UART queue boundary and original receipt. */
  size_t split=(size_t)n>R2_TIME_CHUNK?R2_TIME_CHUNK:(size_t)n;
  r2_time_input_bytes((const uint8_t *)line,split,ticks);
  if((size_t)n>split)r2_time_input_bytes((const uint8_t *)line+split,(size_t)n-split,ticks);
  r2_time_service();
 }
 struct r2_clock c;r2_time_copy(&c);
 assert(c.position.valid&&!c.view.faults);
 struct r2_tz t=r2_time_tz_at((uint64_t)ticks,(uint64_t)c.view.utc_us,c.view.valid);
 assert(t.id==2&&t.offset_min==-300);
 struct r2_tz_local_status status;r2_time_tz_status(&status,(uint64_t)ticks);
 assert(status.selected==2&&!status.hold&&status.mode==R2_TZ_AUTOMATIC);
 /* The queried instant is metadata-only: the owner clock does not move. */
 int64_t before=c.view.utc_us;
 t=r2_time_tz_at((uint64_t)ticks,UINT64_C(1793516399000000),true);assert(t.offset_min==-300);
 t=r2_time_tz_at((uint64_t)ticks,UINT64_C(1793516400000000),true);assert(t.offset_min==-360);
 r2_time_copy(&c);assert(c.view.utc_us==before);
 r2_time_stop();assert(!r2_time_tz_at((uint64_t)ticks,(uint64_t)c.view.utc_us,true).id);
 puts("PASS real queued UART/time owner -> automatic selection, DST query and stop fence; UTC unchanged");
 return 0;
}
