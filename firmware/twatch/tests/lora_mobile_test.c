// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Real MAC/codec and actual N30 observer; no hardware or UTC qualification.
#include "lora_core.h"
#include "lora_shadow_input.h"
#include "lora_passive.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static struct lp_scope scope;
static const uint64_t base=UINT64_C(1700000000000000000);
static uint8_t context[16],wire[112];static uint32_t gen;static uint64_t now;static struct n30_lora_view v;
static uint64_t step_ns;
static unsigned shadow_events,shadow_meta,shadow_pairs;
void watch_lora_shadow_event(uint8_t kind,const uint8_t ctx[16],uint64_t epoch,uint32_t cursor,uint64_t at,uint64_t pair_at,bool valid){
 assert(ctx&&epoch==7&&at);(void)cursor;++shadow_events;
 if(kind==LP_META)++shadow_meta;
 if(kind==LP_FOLLOW){assert(valid&&pair_at&&at>pair_at);++shadow_pairs;}
}

static uint64_t utc(uint64_t us){return base+us*1000-us*2700/1000000+step_ns;}
static void encode(struct lp_packet p){assert(!lp_encode(wire,sizeof wire,&p,context,&group));}
static void snapshot(uint64_t at,uint64_t epoch){now=at;n30_rc8_clock(at*16,epoch,at);n30_rc8_snapshot(&v,at);}
static void meta(unsigned serial,unsigned cursor,bool valid,uint64_t at,uint64_t epoch){
 struct lp_packet p={.kind=LP_META,.schedule=8,.scope=scope,.serial=serial,.cursor=cursor,.utc_ns=utc(at),.tz={2,-360},.time_valid=valid};encode(p);assert(!n30_rc8_receive(wire,112,at*16,epoch,at,at,gen));snapshot(at,epoch);
}
static void setup(void){scope=(struct lp_scope){.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};assert(!lp_context(context,&scope,8,&group));assert(n30_rc8_init(2,1,11,1,group.psk));watch_lora_shadow_set_observer(watch_lora_shadow_event);gen=n30_rc8_switch(true);meta(1,0,true,1000000,7);}
static void pair(unsigned cursor,uint64_t edge,uint64_t epoch,bool qualified_accept){
 n30_rc8_clock(edge*16,epoch,edge);struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=cursor};encode(p);assert(!n30_rc8_receive(wire,24,edge*16,epoch,edge,edge,gen));
 p.kind=LP_FOLLOW;p.utc_ns=utc(edge)-99328000;encode(p);now=edge+2500000;assert(n30_rc8_receive(wire,32,now*16,epoch,now,now,gen)==qualified_accept);snapshot(now,epoch);
}
int main(void){
 setup();pair(0,2000000,7,true);assert(v.watch_valid&&v.watch_pairs==1);
 step_ns=2000000;pair(2,32000000,7,true);
 assert(v.watch_valid&&v.watch_pairs==2&&v.watch_rejected&&v.watch_reason==6);
 assert(v.watch_total_prediction_n==1&&v.watch_max_ns>1000000&&!v.model_valid);
 pair(4,62000000,7,true);assert(v.watch_valid&&v.watch_pairs==3);
 // Loss and millisecond prediction error must not wait for another META.
 step_ns+=3000000;pair(6,212000000,7,true);assert(v.watch_valid&&v.watch_pairs==4&&!v.model_valid);
 const unsigned pairs=v.watch_pairs,events=shadow_events;
 struct lp_packet bad={.kind=LP_SYNC,.schedule=8,.cursor=8};encode(bad);wire[20]^=1;
 assert(!n30_rc8_receive(wire,24,(now+1)*16,7,now+1,now+1,gen));snapshot(now+2,7);assert(v.watch_pairs==pairs&&v.watch_valid&&shadow_events==events);
 // Source withdrawal, counter resets and OFF still fence this observer.
 meta(2,8,false,220000000,7);assert(!v.watch_valid&&v.watch_reason==3);
 meta(3,8,true,221000000,7);pair(8,222000000,7,true);assert(v.watch_valid);
 snapshot(now+1,8);assert(!v.watch_valid&&v.watch_reason==4);
 gen=n30_rc8_switch(false);snapshot(now+2,8);assert(!v.watch_valid);
 assert(shadow_meta==3&&shadow_pairs==5);
 puts("PASS watch LoRa real codec: firstpair coarse UTC,2ms/3ms fitdowngrades without META wait, causal errors retained, MAC/withdrawal/counter/OFF fences");
}
