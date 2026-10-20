/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production codec/policy input; physical event pairs are synthetic. */
#include "lora_rc8_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static struct rc8_receiver rx;
static struct rc8_scope scope={.emitter=1,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
static uint32_t sequence;static uint64_t tick,ns,epoch;
static void receive(const uint8_t *wire,size_t length,uint64_t at,uint64_t capture_epoch,bool accepted){
 struct rc8_event event={.tick=at,.capture_epoch=capture_epoch,.local_us=at/16,.generation=rx.generation,.kind=RC8_EVENT_RX_DONE};
 assert(rc8_receiver_freeze(&rx,wire,length,event));assert(rc8_receiver_process(&rx,&group,event.local_us)==accepted);
}
static void timing(uint64_t next_tick,uint64_t next_ns,uint64_t next_epoch,bool accepted){
 uint8_t wire[80];struct rc8_packet p={.sequence=++sequence,.root=scope.root,.source_epoch=scope.source_epoch,.scale=scope.scale,.kind=scope.kind};
 assert(!rc8_context_id(p.context,&scope,&group));
 if(sequence>1){p.corrected=true;p.correction=(struct rc8_correction){.sequence=sequence-1,.event_ns=ns,.created_ns=ns-1234,.expires_ns=ns+5000000000ULL,.map_generation=7,.map_age_us=100,.upstream_bound_ns=RC8_UNKNOWN};}
 assert(!rc8_encode(wire,&p,&group));receive(wire,80,next_tick,next_epoch,accepted);tick=next_tick;ns=next_ns;epoch=next_epoch;
}
static void init(void){struct rc8_enrollment e={.factory=2,.transmitter_factory=1,.enrolled=true};assert(rc8_receiver_init(&rx,&e));rc8_receiver_switch(&rx,true);sequence=0;scope.stream=1;timing(16000000,1000000000000ULL,7,false);}
static void companion(bool valid,uint32_t floor,uint32_t diagnostic){
 uint8_t wire[128];struct rc8_companion p={.scope=scope,.sequence=diagnostic,.timing_sequence=floor,.events=valid?RC8_D_QUALITY:RC8_D_LOSS,.time_valid=valid};
 assert(!rc8_companion_encode(wire,&p,&group));receive(wire,128,tick+16,epoch,false);
}
static void block(unsigned scenario){
 static const int jitter[16]={0,5000,-3000,8000,-7000,3000,11000,-9000,2000,-1000,6000,-4000,1000,9000,-6000,4000};
 init();
 for(unsigned i=0;i<32;i++){
  uint64_t original_tick=tick,original_ns=ns;
  uint64_t t=16000000+UINT64_C(64000000)*(i+1);
  int64_t offset=scenario==0?0:(int64_t)(i+1)*(scenario==1?280000:900000)+jitter[(i+1)%16];
  timing(t,1000000000000ULL+UINT64_C(4000000000)*(i+1)+(uint64_t)offset,7,true);
  printf("FIT %u %u %llu %llu %u %u\n",scenario,i,(unsigned long long)original_tick,(unsigned long long)original_ns,rx.fit.n,rx.fit.rms_ns);
  if(i==3){struct rc8_link_fit before=rx.fit;companion(true,sequence,1);assert(!memcmp(&before,&rx.fit,sizeof before));companion(false,sequence-1,2);assert(!memcmp(&before,&rx.fit,sizeof before));}
 }
 assert(rx.fit.n==16);companion(false,sequence,3);assert(!rx.fit.n&&!rx.sample.valid);
 rc8_receiver_switch(&rx,false);assert(!rx.fit.n);
}
int main(void){
 for(unsigned s=0;s<3;s++)block(s);
 init();for(unsigned i=0;i<4;i++)timing(tick+64000000,ns+4000000000ULL,7,true);assert(rx.fit.n==4);
 scope.stream++;timing(tick+64000000,ns+4000000000ULL,7,false);assert(!rx.fit.n);timing(tick+64000000,ns+4000000000ULL,7,true);assert(rx.fit.n==1);
 timing(tick+64000000,ns+4000000000ULL,8,false);assert(!rx.fit.n);timing(tick+64000000,ns+4000000000ULL,8,true);assert(rx.fit.n==1);
 timing(tick+208000000,ns+13000000000ULL,8,false);timing(tick+64000000,ns+4000000000ULL,8,true);assert(rx.fit.n==1);
 /* A valid admitted17ms source offset is outside the fit numeric domain:
  * collect anew, rather than overflow or discard the accepted timing. */
 timing(tick+64000000,ns+4017000000ULL,8,true);timing(tick+64000000,ns+4000000000ULL,8,true);assert(rx.fit.n==1);
 init();timing(tick+16,ns+1000,7,true);timing(tick+16,ns+1000,7,true);timing(tick+16,ns+1000,7,true);timing(tick+16,ns+1000,7,true);assert(rx.fit.n==1);
 puts("PASS synthetic wire-corrected link OLS: rollover, quality, stale loss, current loss, scope, epoch, gap, OFF, numeric bounds");
}
