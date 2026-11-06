// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Conditional exponential oscillator, real authenticated codec/core. This is
// software consistency and lifecycle coverage, never hardware qualification.
#include "lora_core.h"
#include "lora_passive.h"
#include "lora_thermal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static const struct lp_scope scope={.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};
static const uint64_t base=UINT64_C(1700000000000000000);
static uint8_t context[16],wire[112];
static uint64_t utc(uint64_t us){double t=(double)us/1000000.;int64_t phase=llround(-2700*t+1500*120*expm1(-t/120));return base+us*1000+phase;}
static void encode(struct lp_packet p){assert(!lp_encode(wire,sizeof wire,&p,context,&group));}
int main(void){
 // Actual observer gaps keep the original physical reference/covariance,
 // whereas the qualified update still refuses its original >300s limit.
 assert(!gm_point(NULL,1,1)&&!gm_observed_point(NULL,1,1));
 struct gm_clock c={0};const uint64_t first=16000000;
 for(unsigned i=0;i<8;i++)assert(gm_point(&c,first+UINT64_C(480000000)*i,base+UINT64_C(30000000000)*i));
 const uint32_t points=c.points;const uint64_t origin=c.first_tick;struct gm_clock strict=c;
 assert(!gm_point(&strict,c.tick+UINT64_C(790000000)*16,c.ns+UINT64_C(790000000000))&&!strict.points);
 assert(gm_observed_point(&c,c.tick+UINT64_C(790000000)*16,c.ns+UINT64_C(790000000000))&&c.points==points+1&&c.first_tick==origin);
 for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)assert(isfinite(c.p[i][j])&&c.p[i][j]==c.p[j][i]&&(i!=j||c.p[i][i]>=0));
 c.p[0][0]=NAN;assert(!gm_observed_point(&c,c.tick+480000000,c.ns+30000000000)&&!c.points);
 double previous=1;
 for(unsigned i=0;i<=100000;i++){
  const double seconds=i*.003,value=n30_lora_decay(seconds);
  assert(isfinite(value)&&value>0&&value<=previous&&fabs(value-exp(-seconds/120.0))<1e-11);previous=value;
 }
 assert(n30_rc8_init(2,1,11,1,group.psk));uint32_t gen=n30_rc8_switch(true);assert(!lp_context(context,&scope,8,&group));
 uint64_t now=1000000;struct lp_packet meta={.kind=LP_META,.schedule=8,.scope=scope,.serial=1,.cursor=0,.utc_ns=utc(now),.tz={2,-360},.time_valid=true};encode(meta);n30_rc8_receive(wire,112,now*16,7,now,now,gen);
 struct n30_lora_view v;
 for(unsigned i=0;i<20;i++){
  uint64_t edge=2000000+UINT64_C(30000000)*i;
  if(i==10){meta.serial=2;meta.cursor=20;meta.utc_ns=utc(edge);encode(meta);assert(!n30_rc8_receive(wire,112,edge*16,7,edge,edge,gen));}
  n30_rc8_clock(edge*16,7,edge);
  struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=2*i};encode(p);assert(!n30_rc8_receive(wire,24,edge*16,7,edge,edge,gen));
  p.kind=LP_FOLLOW;p.utc_ns=utc(edge)-99328000;encode(p);now=edge+2500000;
  assert(n30_rc8_receive(wire,32,now*16,7,now,now,gen));n30_rc8_clock(now*16,7,now);n30_rc8_snapshot(&v,now);
  assert(v.accepted==i+1&&v.authority==0&&v.assurance==0);
  if(i<6)assert(!v.utc_us&&!v.model_valid&&!v.prediction_n);
  else assert(v.utc_us&&v.model_valid&&v.model_kind==1);
  if(i==7)assert(v.link_rms_ns>1000&&v.prediction_n==1&&v.prediction_rms_ns<200);
 }
 assert(v.prediction_n==13&&v.frozen60_n==12&&v.prediction_rms_ns<200&&v.frozen60_rms_ns<200);
 // Same-context fresh META is control refresh, not a reset or new physical pair.
 const uint32_t accepted=v.accepted,n=v.prediction_n;meta.serial=3;meta.cursor=38;meta.utc_ns=utc(now);encode(meta);
 assert(!n30_rc8_receive(wire,112,now*16,7,now,now,gen));n30_rc8_clock(now*16,7,now);n30_rc8_snapshot(&v,now);assert(v.model_valid&&v.accepted==accepted&&v.prediction_n==n);
 // Original physical120s cutoff cannot be renewed by snapshots or META.
 now=2000000+UINT64_C(30000000)*19+LP_HOLD_US;n30_rc8_clock(now*16,7,now);n30_rc8_snapshot(&v,now);assert(!v.utc_us&&!v.model_valid&&!v.until_us);
 // The first post-gap pair cannot revive the retained raw fit as a new model.
 now+=1000000;struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=40};encode(p);n30_rc8_receive(wire,24,now*16,7,now,now,gen);p.kind=LP_FOLLOW;p.utc_ns=utc(now)-99328000;encode(p);assert(n30_rc8_receive(wire,32,(now+2500000)*16,7,now+2500000,now+2500000,gen));now+=2500000;n30_rc8_clock(now*16,7,now);n30_rc8_snapshot(&v,now);assert(!v.utc_us&&!v.model_valid);
 // A capture incarnation change withdraws the trained coordinate immediately.
 n30_rc8_clock(now*16,8,now);n30_rc8_snapshot(&v,now);assert(!v.utc_us&&!v.model_valid);
 n30_rc8_switch(false);n30_rc8_snapshot(&v,now);assert(v.state==0&&!v.prediction_n&&!v.frozen60_n);
 puts("PASS conditional warmup causal delivery, distinct raw fit, frozen60 statistics, original training/META/physical expiry/capture/OFF fences; synthetic only");
}
