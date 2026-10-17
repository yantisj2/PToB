/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "../src/direct_connect.c"
#include "../src/usb_source.c"
#include "dc_projection_under_test.h"
#include <assert.h>
#include <stdio.h>
static uint64_t now=1000000;
int64_t k_uptime_get(void){return (int64_t)(now/1000);}
static struct r2_clock test_clock;
static struct r2_source_clock test_source;
static struct r2_capture at={.tick=16000000,.epoch=1,.kind=R2_CAPTURE_NOW};
bool r2_capture_now(struct r2_capture *out){*out=at;return true;}
bool r2_capture_now_at(struct r2_capture *out,uint64_t *mono){*out=at;*mono=now;return true;}
void r2_time_copy(struct r2_clock *out){*out=test_clock;}
void r2_time_source_snapshot(struct r2_source_clock *out){*out=test_source;}
void r2_time_precision_snapshot(struct r2_pps_precision_view *out){*out=(struct r2_pps_precision_view){0};}
bool r2_time_source_utc_now(uint32_t epoch,int64_t *utc){int64_t ns;bool ok=r2_source_utc_read(&test_source,at,epoch,&ns);*utc=ns/1000;return ok;}
bool r2_usb_sof_scope(uint32_t *epoch){*epoch=1;return true;}
uint8_t r2_usb_reference_number(void){return 2;}
int main(void){
 struct r2_identity id={.silicon="1000000000000004",.boot=99};
 r2_dc_init(&id);r2_usb_source_init(&id);
 struct usb_setup_packet request={0xc1,0x57,0,2,256};int32_t size;uint8_t *data;
 test_clock=(struct r2_clock){.view={.valid=true,.source_available=true,.fix_active=true,
  .pps_present=true,.source_epoch=7,.mono_us=(int64_t)now}};
 test_source=(struct r2_source_clock){.count=1,.head=1,.epoch=1,.last_edge=15999000,
  .utc_bound=true,.utc_source_epoch=7,.utc_offset_ns=INT64_C(1790000000000000000),
  .maps={{.from=15999000,.until=39999000,.anchor_ns=1000000000,
   .rate_q16=UINT64_C(16000000)*65536,.generation=1}}};
 r2_dc_poll(true);assert(publishing&&permission);
 uint32_t revision_before=revision;r2_dc_poll(true);assert(revision==revision_before);
 for(unsigned i=0;i<12;i++){
  at.tick=16016000+(uint64_t)i*512000;now=1001000+(uint64_t)i*32000;test_clock.view.mono_us=now;
  test_clock.view.pps_age_us=(int64_t)i*32000;test_clock.view.gps_age_us=(int64_t)i*32000;
  test_raw=(struct r2_usb_sof_snapshot){.current_tick=at.tick+1000,.cc_before=(uint32_t)at.tick,
   .cc_after=(uint32_t)at.tick,.capture_epoch=1,.frame_before=(uint16_t)(32*i),.frame_after=(uint16_t)(32*i)};
  assert(!r2_usb_source_request(&request,&size,&data)&&size==256);
  assert((sys_get_le32(data+36)&1)&&sys_get_le64(data+80)==UINT64_C(0x1000000000000004));
  assert(sys_get_le64(data+88)==99&&sys_get_le32(data+96)==7);
  assert(!memcmp(data+200,"DC-GNSS-USB-v1",14)&&sys_get_le32(data+172)==UINT32_MAX);
 }
 uint32_t value=1;unsigned samples=0;
 assert(r2_dc_metric(&value,&samples)&&value==0&&samples==11);
 /* A quality withdrawal fences strict observations and estimates immediately. */
 test_clock.view.fix_active=false;r2_dc_poll(true);
 assert(!r2_dc_metric(&value,&samples));
 assert(!r2_usb_source_request(&request,&size,&data)&&!(sys_get_le32(data+36)&1));
 test_clock.view.fix_active=true;r2_dc_poll(true);assert(publishing);
 r2_dc_poll(false);assert(!publishing&&!permission);
 assert(!r2_usb_source_request(&request,&size,&data)&&!(sys_get_le32(data+36)&1));
 request.bRequest=0x53;
 assert(!r2_usb_source_request(&request,&size,&data)&&!sys_get_le32(data+36)&&!sys_get_le64(data+64));
 puts("PASS actual DC owner -> USB UTE1 -> local root provenance, immutable renewal and withdrawal");
}
