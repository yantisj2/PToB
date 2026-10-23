// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_core.h"
#include "lora_rc8.h"
#include <assert.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static struct rc8_scope scope={.emitter=1,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
int main(void){
 assert(w12_rc8_init(2,1,group.id,group.epoch,group.psk));uint32_t gen=w12_rc8_switch(true);uint64_t tick=16000000,local=1000000,ns=1000000000000ULL;
 uint8_t wire[128];struct rc8_companion meta={.scope=scope,.sequence=1,.time_valid=true,.quality={.known=RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY,.parent_class=248,.parent_accuracy=254}};
 // Authenticated metadata from a different emitter cannot unlock this receiver.
 struct rc8_companion foreign=meta;foreign.scope.emitter=4;
 assert(!rc8_companion_encode(wire,&foreign,&group));assert(!w12_rc8_receive(wire,128,tick,7,local,local,gen));
 struct w12_lora_view rejected;w12_rc8_snapshot(&rejected,local);assert(!rejected.metadata_known&&!rejected.accepted&&!rejected.utc_us);
 assert(!rc8_companion_encode(wire,&meta,&group));assert(!w12_rc8_receive(wire,128,tick,7,local,local,gen));
 for(uint32_t i=1;i<=7;i++){
  struct rc8_packet p={.sequence=i,.root=scope.root,.source_epoch=scope.source_epoch,.scale=scope.scale,.kind=scope.kind};assert(!rc8_context_id(p.context,&scope,&group));
  if(i>1){p.corrected=true;p.correction=(struct rc8_correction){.sequence=i-1,.event_ns=ns,.created_ns=ns-1234,.expires_ns=ns+5000000000ULL,.map_generation=7,.map_age_us=100,.upstream_bound_ns=RC8_UNKNOWN};}
  assert(!rc8_encode(wire,&p,&group));assert(w12_rc8_receive(wire,80,tick,7,local,local,gen)==(i>1));
  struct w12_lora_view v;w12_rc8_snapshot(&v,local);assert(v.metadata_known&&v.root_boot==44&&v.emitter==1);
  if(i>1){assert(v.state==2&&v.until_us==local-4000000+12000000&&v.utc_us==ns/1000+4000000);assert(v.quality_known==meta.quality.known);}
  if(i>=5)assert(v.link_samples>=4&&v.link_rms_ns==0);
  assert(!w12_rc8_receive(wire,80,tick,7,local,local,gen)); // Replay never refreshes.
  wire[50]^=1;assert(!w12_rc8_receive(wire,80,tick,7,local,local,gen));wire[50]^=1;
  tick+=64000000;local+=4000000;if(i>1)ns+=4000000000ULL;
 }
 struct w12_lora_view before,after;w12_rc8_snapshot(&before,local-4000000);assert(before.replayed>=7&&before.rejected>=7&&before.accepted==6);
 w12_rc8_snapshot(&after,before.until_us);assert(after.state==3&&after.utc_us==0&&after.until_us==0);
 // Companions describe metadata but cannot renew an expired timing pair.
 meta.sequence=2;assert(!rc8_companion_encode(wire,&meta,&group));assert(!w12_rc8_receive(wire,128,tick,7,before.until_us,before.until_us,gen));w12_rc8_snapshot(&after,before.until_us);assert(after.utc_us==0);
 w12_rc8_switch(false);w12_rc8_snapshot(&after,local);assert(after.state==0&&!after.utc_us&&after.accepted==before.accepted);
 assert(!w12_rc8_init(1,1,group.id,group.epoch,group.psk));
 // Same N30 functional path: signed physical pairs can precede companion
 // metadata. Do not invent the missing boot, metadata validity or authority.
 assert(w12_rc8_init(2,1,group.id,group.epoch,group.psk));gen=w12_rc8_switch(true);
 tick=16000000;local=1000000;ns=1000000000000ULL;
 for(uint32_t i=1;i<=5;++i){
  struct rc8_packet p={.sequence=i,.root=scope.root,.source_epoch=scope.source_epoch,.scale=scope.scale,.kind=scope.kind};assert(!rc8_context_id(p.context,&scope,&group));
  if(i>1){p.corrected=true;p.correction=(struct rc8_correction){.sequence=i-1,.event_ns=ns,.created_ns=ns-1234,.expires_ns=ns+5000000000ULL,.map_generation=7,.upstream_bound_ns=RC8_UNKNOWN};}
  assert(!rc8_encode(wire,&p,&group));assert(w12_rc8_receive(wire,80,tick,7,local,local,gen)==(i>1));
  tick+=64000000;local+=4000000;if(i>1)ns+=4000000000ULL;
 }
 w12_rc8_snapshot(&after,local-4000000);assert(after.state==2&&after.utc_us&&after.link_samples==4&&after.link_rms_ns==0&&!after.root_boot&&!after.metadata_known&&!after.time_valid);
 // A later matching signed withdrawal still revokes the functional sample.
 meta.sequence=1;meta.timing_sequence=5;meta.time_valid=false;
 assert(!rc8_companion_encode(wire,&meta,&group));assert(!w12_rc8_receive(wire,128,tick,7,local,local,gen));
 w12_rc8_snapshot(&after,local);assert(after.state==1&&!after.utc_us&&!after.until_us);
}
