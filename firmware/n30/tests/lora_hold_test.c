// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Real authenticated pipeline, synthetic oscillator/peripheral events only.
#include "lora_core.h"
#include "lora_passive.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static const struct lp_scope scope={.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};
static const uint64_t base=UINT64_C(1700000000000000000);
static uint8_t context[16],wire[112];static uint32_t gen;static uint64_t now;
static uint64_t utc(uint64_t us){double t=(double)us/1000000.;return base+us*1000+llround(-2700*t+1500*120*expm1(-t/120));}
static void encode(struct lp_packet p){assert(!lp_encode(wire,sizeof wire,&p,context,&group));}
static void meta(unsigned serial,unsigned cursor,unsigned stream,bool valid){
 struct lp_packet p={.kind=LP_META,.schedule=8,.scope=scope,.serial=serial,.cursor=cursor,.utc_ns=utc(now),.tz={2,-360},.time_valid=valid};p.scope.stream=stream;encode(p);assert(!n30_rc8_receive(wire,112,now*16,7,now,now,gen));
}
static void pair(unsigned i,uint64_t edge){
 n30_rc8_clock(edge*16,7,edge);struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=2*i};encode(p);assert(!n30_rc8_receive(wire,24,edge*16,7,edge,edge,gen));
 p.kind=LP_FOLLOW;p.utc_ns=utc(edge)-99328000;encode(p);now=edge+2500000;assert(n30_rc8_receive(wire,32,now*16,7,now,now,gen));n30_rc8_clock(now*16,7,now);
}
static struct n30_lora_view snapshot(void){struct n30_lora_view v;n30_rc8_snapshot(&v,now);return v;}
static void setup(uint64_t first){
 assert(n30_rc8_init(2,1,11,1,group.psk));gen=n30_rc8_switch(true);assert(!lp_context(context,&scope,8,&group));now=1000000;meta(1,0,1,true);
 assert(!n30_rc8_hold60(1,now));
 for(unsigned i=0;i<8;i++)pair(i,first+UINT64_C(30000000)*i);
 assert(snapshot().model_valid);
}
int main(void){
 setup(2000000);assert(!n30_rc8_hold60(0,now));assert(n30_rc8_hold60(1,now));assert(!n30_rc8_hold60(2,now));
 const struct n30_lora_view t0=snapshot();assert(t0.hold_active&&t0.hold_state==1&&t0.hold_id==1&&t0.hold_runs==1&&t0.hold_until_us-t0.hold_start_us==60000000);
 pair(8,242000000);struct n30_lora_view v=snapshot();assert(v.hold_active&&v.model_tick16==t0.model_tick16&&v.model_ns==t0.model_ns&&v.model_rate_milli_ppb==t0.model_rate_milli_ppb&&v.model_accel_micro_ppb_s==t0.model_accel_micro_ppb_s);
 assert(v.reference_age_ms==32500&&v.age_ms==2500&&v.until_us==t0.until_us&&v.hold_grant_until_us==t0.hold_grant_until_us);
 meta(2,16,1,true);n30_rc8_clock(now*16,7,now);v=snapshot();assert(v.hold_active&&v.until_us==t0.until_us&&v.hold_grant_until_us==t0.hold_grant_until_us);
 // The physical60s event precedes hold expiry; FOLLOW finishes exactly at the
 // finite deadline. Its immutable held model must still be scored afterward.
 pair(9,272000000);v=snapshot();assert(!v.hold_active&&v.hold_state==2&&v.hold_abort==0&&v.hold60_n==1&&v.hold60_rms_ns<200&&v.hold60_over1us==0&&v.model_valid);
 assert(v.watch_prediction_mode==3&&v.watch_prediction_age_ms==60000);
 pair(10,302000000);v=snapshot();assert(v.hold_state==2&&v.watch_prediction_mode==2&&v.watch_prediction_age_ms==30000);
 // Reference older than10s, old counter sampling and insufficient original
 // META grant cannot arm/extend the experiment.
 now+=8000000;n30_rc8_clock(now*16,7,now);assert(!n30_rc8_hold60(2,now));
 setup(200000000);assert(!n30_rc8_hold60(2,now));
 setup(2000000);assert(n30_rc8_hold60(3,now));now+=1000;meta(2,14,2,true);v=snapshot();assert(!v.utc_us&&v.hold_state==3&&v.hold_abort==2&&v.hold_id==3);
 setup(2000000);assert(n30_rc8_hold60(4,now));now+=1000;meta(2,14,1,false);v=snapshot();assert(!v.utc_us&&v.hold_state==3&&v.hold_abort==3);
 setup(2000000);assert(n30_rc8_hold60(5,now));n30_rc8_clock(now*16,8,now);v=snapshot();assert(!v.utc_us&&v.hold_state==3&&v.hold_abort==4);
 setup(2000000);assert(n30_rc8_hold60(6,now));n30_rc8_switch(false);v=snapshot();assert(!v.utc_us&&v.hold_state==3&&v.hold_abort==5&&v.hold_id==6);
 setup(2000000);assert(n30_rc8_hold60(8,now));n30_rc8_radio_fault();v=snapshot();assert(!v.utc_us&&v.hold_state==3&&v.hold_abort==7&&v.hold_id==8);
 setup(2000000);assert(n30_rc8_hold60(7,now));now=212000000+LP_HOLD_US;n30_rc8_clock(now*16,7,now);v=snapshot();assert(!v.utc_us&&!v.until_us&&!v.hold_active&&v.hold_state==2);
 puts("PASS actual-delivery finiteHOLD originalT0grant/immutablemodel, RF-observed60s after delayedFOLLOW, admission/refusedextend/META/capture/OFF/expiry fences; synthetic only");
}
