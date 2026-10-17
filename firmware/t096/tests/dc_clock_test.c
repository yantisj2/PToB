/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dc_projection_under_test.h"
int main(void){
 struct r2_source_clock source;r2_source_clock_init(&source);
 struct r2_pps_precision pps;r2_pps_precision_init(&pps);
 struct r2_clock clock={.view={.source_epoch=7,.valid=true,.source_available=true,
  .fix_active=true,.pps_present=true,.association=R2_ASSOC_UNKNOWN}};
 struct r2_capture at={.epoch=1,.kind=R2_CAPTURE_NOW};struct r2_uc_clock out;
 for(unsigned second=1;second<=7200;second++){
  uint64_t edge=(uint64_t)second*16000007;
  assert(r2_pps_precision_add(&pps,(struct r2_capture){edge,1,R2_CAPTURE_PPS}));
  at.tick=edge+1600;clock.view.mono_us=(int64_t)second*1000000+100;
  clock.view.pps_age_us=100;clock.view.gps_age_us=100000;
  if(second==1)continue;
  assert(r2_source_clock_publish(&source,&pps,at));
  assert(r2_source_utc_bind(&source,at,INT64_C(1790000000000000000)+(int64_t)second*1000000000+100000,7,R2_ASSOC_UNKNOWN)>0);
  assert(r2_dc_clock(&source,&clock,at,clock.view.mono_us,17,99,&out));
  assert(out.depth==0&&out.origin.root==17&&out.origin.boot==99&&out.origin.epoch==7);
  assert(out.valid&&out.estimate&&out.map.bound==UINT32_MAX&&out.map.capture_bound==UINT32_MAX);
  assert(!memcmp(out.scope,"DC-GNSS-USB-v1",14));
  assert(out.until_tick<=edge+24000000&&out.session_until==out.until_tick);
  for(unsigned j=0;j<15;j++){
   struct r2_capture sample=at;sample.tick+=(uint64_t)j*1500000;
   uint64_t ns,gen,wire,elapsed;
   assert(r2_source_clock_read(&source,sample,&ns,&gen));
   assert(rc5_map_project(&out.map,sample.tick,&wire,&elapsed));
   uint64_t difference=ns>wire?ns-wire:wire-ns;assert(difference<=3);
  }
 }
 struct r2_clock good=clock;
 for(unsigned failure=0;failure<12;failure++){
  clock=good;
  if(failure==0)clock.fatal=true;
  if(failure==1)clock.stopped=true;
  if(failure==2)clock.view.fix_active=false;
  if(failure==3)clock.view.pps_present=false;
  if(failure==4)clock.view.valid=false;
  if(failure==5)clock.view.source_epoch++;
  if(failure==6)clock.view.association=R2_ASSOC_PRECEDING_UTC_SECOND;
  if(failure==7)clock.view.gps_age_us=R2_GPS_FRESH_US;
  if(failure==8)clock.view.pps_age_us=R2_PPS_FRESH_US;
  if(failure==9)clock.view.pps_age_us=-1;
  if(failure==10)clock.view.mono_us+=1;
  if(failure==11)clock.view.mono_us-=100000;
  assert(!r2_dc_clock(&source,&clock,at,good.view.mono_us,17,99,&out)&&!out.valid);
 }
 clock=good;at.epoch++;assert(!r2_dc_clock(&source,&clock,at,clock.view.mono_us,17,99,&out));at.epoch--;
 clock.view.gps_age_us=R2_GPS_FRESH_US-100;
 assert(!r2_dc_clock(&source,&clock,at,clock.view.mono_us+100,17,99,&out));
 /* Exercise the real board-default parser/clock, rather than supplying a
  * synthetic commissioned source_available bit that UNKNOWN can never earn. */
 struct r2_clock raw;r2_clock_init(&raw,0,R2_ASSOC_UNKNOWN);
 struct r2_source_clock local;r2_source_clock_init(&local);
 struct r2_pps_precision fit;r2_pps_precision_init(&fit);
 for(unsigned second=0;second<16;second++){
  int64_t edge_us=(int64_t)(second+1)*1000000;
  struct r2_capture edge={(uint64_t)(second+1)*16000000,2,R2_CAPTURE_PPS};
  r2_clock_pps(&raw,edge_us,edge_us);assert(r2_pps_precision_add(&fit,edge));
  char body[128],line[144];snprintf(body,sizeof body,"GPRMC,1200%02u.000,A,0000.0000,N,00000.0000,E,0.0,0.0,171026,,,A",second);
  unsigned checksum=0;for(unsigned i=0;body[i];i++)checksum^=(unsigned char)body[i];
  int n=snprintf(line,sizeof line,"$%s*%02X\r\n",body,checksum);
  r2_clock_bytes(&raw,(const uint8_t *)line,(size_t)n,edge_us+100000,edge_us+100000);
  struct r2_capture available={edge.tick+1600000,2,R2_CAPTURE_NOW};
  if(second)assert(r2_source_clock_publish(&local,&fit,available));
  if(second<2)continue;
  assert(raw.view.valid&&raw.view.fix_active&&raw.view.pps_present);
  assert(!raw.view.source_available&&!raw.view.rate_valid&&raw.view.association==R2_ASSOC_UNKNOWN);
  assert(r2_source_utc_bind(&local,available,raw.view.utc_us*1000,raw.view.source_epoch,raw.view.association)>0);
  assert(r2_dc_clock(&local,&raw,available,(uint64_t)raw.view.mono_us,17,99,&out)&&out.valid);
 }
 r2_source_clock_invalidate(&source);
 assert(!r2_dc_clock(&source,&good,at,good.view.mono_us,17,99,&out));
 struct r2_dc_rms metric={0};uint32_t value;unsigned samples;
 for(unsigned i=0;i<40;i++)r2_dc_rms_add(&metric,1000+(uint64_t)i*512000,
  1000000+(uint64_t)i*32003200,(2040+i*32)&2047,1,1,7,4,i*32);
 assert(r2_dc_rms_read(&metric,1248,&value,&samples)&&value==100&&samples==32);
 r2_dc_rms_add(&metric,metric.tick,metric.ns,metric.frame,1,1,7,4,1249);
 assert(metric.count==32); /* Duplicate reads never manufacture samples. */
 assert(!r2_dc_rms_read(&metric,2248,&value,&samples));
 for(unsigned fence=0;fence<4;fence++){
  struct r2_dc_rms copy=metric;
  r2_dc_rms_add(&copy,copy.tick+512000,copy.ns+32000000,(copy.frame+32)&2047,
   1+(fence==0),1+(fence==1),7+(fence==2),4+(fence==3),1280);
  assert(!r2_dc_rms_read(&copy,1280,&value,&samples)&&!samples);
 }
 puts("PASS DC: 7199 causal GNSS maps / 107985 projections, expiry/identity fences, USB span RMS and frame wrap");
}
