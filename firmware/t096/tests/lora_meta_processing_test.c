/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "lora_passive_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct r2_group_config group={.id=9,.epoch=1,.psk={1,2,3}};
static struct lp_packet meta(bool current){
 struct lp_packet p={.kind=LP_META,.schedule=LP_SCHEDULE,
  .scope={.emitter=3,.boot=99,.root=4,.root_boot=88,.parent=4,.parent_boot=88,
   .epoch=5,.stream=1,.seed=4208471279u,.kind=2},
  .cursor=40,.serial=4,.utc_ns=UINT64_C(1793762730000000000),
  .has_pair=true,.time_valid=true,.delay_10ms=620,.tz={.id=2,.offset_min=-300}};
 if(current){
  p.scope.root=3;p.scope.root_boot=99;p.scope.parent=p.scope.parent_boot=0;
  p.scope.epoch=1;p.scope.stream=3;p.scope.kind=1;
  p.cursor=106;p.serial=8;p.utc_ns+=UINT64_C(900000000000);
 }
 return p;
}
static void cache(struct lp_rx *r,struct lp_replay *floor){
 lp_rx_clear(r);struct lp_packet p=meta(false);
 assert(lp_rx_meta(r,&p,&group,NULL,100000000,100000001));
 r->highest=58;r->accepted=53;
 *floor=(struct lp_replay){.boot=99,.meta_ns=p.utc_ns,.seed=p.scope.seed,.cursor=60,.serial=4};
}
int main(void){
 struct lp_rx r;struct lp_replay floor;cache(&r,&floor);
 struct lp_packet p=meta(true),decoded;uint8_t wire[112],context[16];
 assert(!lp_context(context,&p.scope,LP_SCHEDULE,&group));
 assert(!lp_encode(wire,sizeof wire,&p,context,&group));
 assert(!lp_decode(&decoded,wire,sizeof wire,r.context,&group));
 assert(lp_replay_admit(&floor,&decoded));
 uint64_t header=1000000000;
#ifdef R2_LORA_FIXED500_EXPERIMENT
 assert(LP_META_PROCESS_US==1787456);
 assert(LP_META_PROCESS_US==LP_CONTROL_PROCESS_US+lp_duration(LP_META)-10000);
 assert(lp_rx_meta(&r,&decoded,&group,NULL,header,header+1025444));
 assert(r.scope.kind==1&&r.scope.epoch==1&&r.scope.root_boot==99&&r.scope.stream==3);
 assert(r.serial==8&&r.highest==106);
 assert(r.observed_deadline_us==header+LP_LEASE_US&&!r.bounded&&!r.meta_deadline_us&&!r.tz_deadline_us);
 lp_replay_commit(&floor,&decoded);
 assert(!lp_replay_admit(&floor,&decoded));
 struct lp_packet stale=meta(false);assert(!lp_replay_admit(&floor,&stale));
#else
 assert(LP_META_PROCESS_US==LP_CONTROL_PROCESS_US&&LP_META_PROCESS_US==1000000);
 assert(!lp_rx_meta(&r,&decoded,&group,NULL,header,header+1025444));
 assert(r.scope.epoch==5&&r.scope.root_boot==88&&r.serial==4&&r.accepted==53);
#endif
 cache(&r,&floor);
 assert(lp_rx_meta(&r,&decoded,&group,NULL,header,header+LP_META_PROCESS_US));
 assert(r.observed_deadline_us==header+LP_LEASE_US);
 cache(&r,&floor);
 assert(!lp_rx_meta(&r,&decoded,&group,NULL,header,header+LP_META_PROCESS_US+1));
 assert(r.scope.epoch==5&&r.serial==4&&r.accepted==53);
 wire[111]^=1;assert(lp_decode(&decoded,wire,sizeof wire,r.context,&group));
 puts("PASS META processing boundary/root transition/original lease/replay/auth; nonfixed limits preserved");
 return 0;
}
