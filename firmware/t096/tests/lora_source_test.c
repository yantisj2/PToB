/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_source.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
static struct rc8_scope scope(bool gm){return(struct rc8_scope){.emitter=1,.emitter_boot=2,.stream=1,.root=gm?1:3,.root_boot=4,.source_epoch=5,.scale=RC8_UTC,.kind=gm?RC8_OWN_GM:RC8_SELECTED_BLE};}
static uint64_t projected(const struct r2_source_clock *s,uint64_t tick){
 const struct r2_source_map *m=&s->maps[0];
 uint64_t delta=(__uint128_t)(tick-m->from)*65536*1000000000/m->rate_q16;
 int64_t correction=(int64_t)delta*m->phase_ppb/1000000000;
 return m->anchor_ns+delta-correction+s->utc_offset_ns;
}
int main(void){
 for(unsigned r=0;r<3;r++)for(unsigned p=0;p<3;p++){
  struct r2_source_clock s={.epoch=7,.count=1,.head=1,.utc_offset_ns=1000000000000LL,.utc_source_epoch=5,.utc_bound=true};
  s.maps[0]=(struct r2_source_map){.from=16000000,.until=40000000,.anchor_ns=2000000000,.generation=11,.rate_q16=(15990000+10000*r)*UINT64_C(65536),.phase_ppb=-5000000+(int32_t)p*5000000};
  struct r2_capture now={.tick=17000000,.epoch=7,.kind=R2_CAPTURE_NOW};
  struct rc8_export map;uint64_t grant=28000000;
  assert(r2_lora_source_own(&map,&s,scope(true),now,grant));assert(map.mode==RC8_MAP_SOURCE_Q16);
  int64_t exact;assert(r2_source_utc_read(&s,(struct r2_capture){grant,7,R2_CAPTURE_NOW},5,&exact));assert(map.expires_ns==(uint64_t)exact);
  for(uint64_t tick=17000000;tick<grant;tick+=77777){
   struct rc8_correction out;assert(rc8_export_event(&out,&map,1,(struct rc8_event){.tick=tick,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
   assert(r2_source_utc_read(&s,(struct r2_capture){tick,7,R2_CAPTURE_RADIO},5,&exact));assert(out.event_ns==(uint64_t)exact);
  }
  struct rc8_correction out;assert(!rc8_export_event(&out,&map,1,(struct rc8_event){.tick=grant,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
  assert(!r2_lora_source_own(&map,&s,scope(false),now,grant));
  s.last_edge=16000000;
  struct r2_source_clock original=s;
  const uint64_t passive_until=s.last_edge+R2_LORA_OWN_WINDOW_US*16;
  assert(r2_lora_source_passive_own(&map,&s,scope(true),now));
  assert(map.mode==RC8_MAP_PASSIVE_Q16&&map.until_tick==passive_until&&map.until_tick>s.maps[0].until);
  assert(map.expires_ns==projected(&s,passive_until));
  uint64_t expiry=map.expires_ns;
  for(uint64_t tick=17000000;tick<passive_until;tick+=77777){
   assert(rc8_export_event(&out,&map,1,(struct rc8_event){.tick=tick,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
   assert(out.event_ns==projected(&s,tick));
  }
  assert(!rc8_export_event(&out,&map,1,(struct rc8_event){.tick=passive_until,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
  struct rc8_export widened=map;widened.until_tick=passive_until+100;widened.expires_ns=UINT64_MAX;
  assert(!rc8_export_event(&out,&widened,1,(struct rc8_event){.tick=passive_until+1,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
  widened=map;widened.scope.kind=RC8_SELECTED_BLE;
  assert(!rc8_export_event(&out,&widened,1,(struct rc8_event){.tick=17000000,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
  assert(r2_lora_source_passive_own(&map,&s,scope(true),(struct r2_capture){25000000,7,R2_CAPTURE_NOW}));
  assert(map.until_tick==passive_until&&map.expires_ns==expiry); /* no snapshot renewal */
  assert(!r2_lora_source_passive_own(&map,&s,scope(true),(struct r2_capture){s.maps[0].until,7,R2_CAPTURE_NOW}));
  assert(!map.valid); /* stale source cannot create another grant */
  assert(!r2_lora_source_passive_own(&map,&s,scope(false),now));
  assert(!memcmp(&s,&original,sizeof s)); /* original BLE/source bounds unchanged */
 }
 for(int phase=-500000;phase<=500000;phase+=500000)for(int rate=-50000;rate<=50000;rate+=50000){
  r2_servo_map_t original={.delivery=16000000,.anchor=2000000000,.phase=phase,.rate=rate,.valid=true};
  struct rc8_export map;struct r2_capture now={17000000,9,R2_CAPTURE_NOW};
  assert(r2_lora_source_follower(&map,&original,scope(false),now,36000000,2000000000,4000000000,1000000000000LL,12));
  for(uint64_t tick=17000000;tick<36000000;tick+=33333){
   int64_t exact;assert(r2_servo_mapped(&original,tick,&exact));struct rc8_correction c;
   assert(rc8_export_event(&c,&map,2,(struct rc8_event){.tick=tick,.capture_epoch=9,.kind=RC8_EVENT_TX_START}));assert(c.event_ns==(uint64_t)exact+1000000000000ULL);
  }
 }
 r2_servo_map_t equal={.delivery=16000000,.anchor=2000000000,.valid=true};struct rc8_export map;
 assert(r2_lora_source_follower(&map,&equal,scope(false),(struct r2_capture){16000000,9,R2_CAPTURE_NOW},17000000,2000000000,4000000000,0,1));
 puts("PASS exact source/follower frozen-map parity and permission boundaries");
}
