// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_core.h"
#include "lora_passive.h"
#include <assert.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static const struct lp_scope scope={.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};
static const uint64_t base=UINT64_C(1700000000000000000);
static uint8_t context[16],wire[112];
static void encode(struct lp_packet p){assert(!lp_encode(wire,sizeof wire,&p,context,&group));}
int main(void){
 assert(n30_rc8_init(2,1,group.id,group.epoch,group.psk));uint32_t gen=n30_rc8_switch(true);assert(!lp_context(context,&scope,8,&group));
 uint64_t local=1000000;
 struct lp_packet meta={.kind=LP_META,.schedule=8,.scope=scope,.serial=1,.cursor=0,.utc_ns=base+1000000000,.tz={2,-360},.time_valid=true};encode(meta);
 for(unsigned old=3;old<=7;old+=2){wire[3]=old;assert(!n30_rc8_receive(wire,112,local*16,7,local,local,gen));}encode(meta);
 assert(!n30_rc8_receive(wire,112,local*16,7,local,local,gen));
 struct n30_lora_view v;n30_rc8_clock(local*16,7,local);n30_rc8_snapshot(&v,local);assert(v.metadata_known&&v.accepted==0&&!v.utc_us&&v.authority==0);
 assert(v.tz_id==2&&v.tz_offset_min==-360&&v.fit_span_ms==0&&v.age_ms==UINT32_MAX);
 assert(!n30_rc8_receive(wire,112,local*16,7,local,local,gen)); // Replayed META cannot renew.
 for(unsigned i=0;i<8;i++){
  local=2000000+UINT64_C(30000000)*i;
  struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=2*i};encode(p);
  assert(!n30_rc8_receive(wire,24,local*16,7,local,local,gen));
  p.kind=LP_FOLLOW;p.utc_ns=base+local*1000-UINT64_C(99328000);encode(p);
  assert(n30_rc8_receive(wire,32,(local+2500000)*16,7,local+2500000,local+2500000,gen));
  n30_rc8_clock((local+2500000)*16,7,local+2500000);n30_rc8_snapshot(&v,local+2500000);
  assert(v.accepted==i+1&&v.root==1&&v.root_boot==22&&v.authority==0&&v.assurance==0);
  assert(v.fit_span_ms==i*30000);
  if(i>=6){assert(v.state==2&&v.trained&&v.model_valid&&v.link_rms_ns<=1&&v.utc_us==base/1000+local+2500000);assert(v.age_ms==2500&&v.reference_age_ms==2500&&v.snapshot_age_ms>v.reference_age_ms);}
  assert(!n30_rc8_receive(wire,32,(local+2500000)*16,7,local+2500000,local+2500000,gen));
  wire[20]^=1;assert(!n30_rc8_receive(wire,32,(local+2500000)*16,7,local+2500000,local+2500000,gen));
 }
 // UTC belongs to the raw CURRENT sample, while freshness/expiry use actual
 // availability. Delayed publication cannot silently shift the clock30ms.
 const uint64_t sample=local+2500000;
 n30_rc8_clock_sample(sample*16,7,sample,sample+30000);
 n30_rc8_snapshot_sample(&v,sample,sample+30000);
 assert(v.state==2&&v.sampled_us==sample&&v.utc_us==base/1000+sample);
 assert(v.utc_us+(sample+30000-v.sampled_us)==base/1000+sample+30000);
 assert(v.age_ms==2530&&v.reference_age_ms==2530);
 assert(!n30_rc8_hold60(1,sample+250001)); // stale raw sample is not renewed
 // A fresh META cursor establishes a replay floor, never an invented pair.
 const uint32_t accepted=v.accepted;local+=4000000;meta.serial=2;meta.cursor=100;meta.utc_ns=base+local*1000;encode(meta);
 assert(!n30_rc8_receive(wire,112,local*16,7,local,local,gen));n30_rc8_clock(local*16,7,local);n30_rc8_snapshot(&v,local);assert(v.accepted==accepted);
 struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=100};encode(p);assert(!n30_rc8_receive(wire,24,local*16,7,local,local,gen));p.kind=LP_FOLLOW;p.utc_ns=base+local*1000-99328000;encode(p);assert(n30_rc8_receive(wire,32,(local+2500000)*16,7,local+2500000,local+2500000,gen));
 n30_rc8_clock((local+LP_HOLD_US)*16,7,local+LP_HOLD_US);n30_rc8_snapshot(&v,local+LP_HOLD_US);assert(v.state==3&&!v.utc_us&&!v.until_us&&!v.trained&&v.link_rms_ns==UINT32_MAX);
 local+=LP_HOLD_US;meta.scope.stream=2;meta.serial=3;meta.utc_ns=base+local*1000;encode(meta);
 assert(!n30_rc8_receive(wire,112,local*16,7,local,local,gen));n30_rc8_clock(local*16,7,local);n30_rc8_snapshot(&v,local);
 assert(v.stream==2&&v.metadata_known&&!v.link_samples&&!v.fit_span_ms&&v.age_ms==UINT32_MAX&&!v.utc_us&&!v.trained);
 n30_rc8_switch(false);n30_rc8_snapshot(&v,local+LP_HOLD_US);assert(v.state==0&&!v.utc_us&&!v.metadata_known);
 assert(!n30_rc8_init(1,1,group.id,group.epoch,group.psk));
}
