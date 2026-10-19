/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "pc_projection_under_test.h"
static struct rc_shadow_map map(uint64_t tick){
 return (struct rc_shadow_map){.epoch=7,.lo=tick,.active=tick+1,.root=11,.root_boot=12,.root_epoch=13,
  .map={.effective=tick,.anchor_tick=tick,.anchor_ns=1000000000,.generation=1,.bound=RC5_UNKNOWN,.rate_bound=RC5_UNKNOWN,.capture_bound=RC5_UNKNOWN}};
}
static struct rc5_reference pair(uint64_t tick,uint64_t ns){return (struct rc5_reference){.valid=true,.rx=tick,.completed_at=tick+8,.parent_tx_ns=ns,.origin={.root=11,.boot=12,.epoch=13}};}
int main(void){
 struct pc_experiment p;pc_init(&p);struct rc_shadow_map m=map(16000000);pc_offer(&p,&m);
 assert(pc_begin(&p,0,1000,1000,16000100,7)==-EINVAL);
 assert(pc_begin(&p,1,1000,300001,16000100,7)==-EINVAL);
 assert(!pc_begin(&p,PC_WARM_SHADOW,1000,60000,16000100,7));
 assert(pc_begin(&p,1,1000,1000,16000101,7)==-EBUSY);
 struct rc5_reference r=pair(16000200,1000012550);assert(pc_observe(&p,&r,7));
 assert(p.phase==PC_LISTEN&&p.listen_until==r.completed_at+16000000);
 struct pc_record q;assert(pc_read(&p,0,&q)&&q.valid&&q.error_ns==50);
 uint64_t until=p.listen_until;
 m=map(until-1000);pc_offer(&p,&m);pc_tick(&p,until,7);
 assert(p.phase==PC_HOLD&&p.have_frozen&&!pc_quiet(&p));
 uint64_t frozen=p.frozen.map.anchor_ns;
 r=pair(until+16000000,2000001000);assert(!pc_observe(&p,&r,7));
 assert(p.frozen.map.anchor_ns==frozen&&p.training_pairs==1);
 struct r2_capture edge={.tick=until+32000000,.epoch=7,.kind=R2_CAPTURE_PPS};struct rc_shadow_map w;
 assert(pc_witness_map(&p,edge,&w)&&w.experimental);
 uint64_t ns,elapsed;assert(rc5_map_project(&w.map,edge.tick,&ns,&elapsed));
 assert(w.map.bound==RC5_UNKNOWN); /* Projection never creates a bound. */
 edge.epoch=8;assert(!pc_witness_map(&p,edge,&w));
 pc_tick(&p,p.hold_until,7);assert(p.phase==PC_DONE);
 r=pair(p.hold_until+100,100000000000);assert(pc_observe(&p,&r,7)&&p.first_return_sequence==3);
 assert(!pc_begin(&p,PC_COLD_RADIO,1,1000,r.completed_at+1,7)&&p.reset_pending&&!p.have_map);
 r=pair(r.completed_at+100,100000000100);assert(pc_observe(&p,&r,7));
 m=map(r.completed_at);pc_offer(&p,&m);pc_tick(&p,p.listen_until,7);assert(pc_quiet(&p));
 pc_tick(&p,p.last_tick+1,8);assert(p.phase==PC_FAILED&&p.failures==1&&!pc_quiet(&p));
 pc_init(&p);assert(!pc_begin(&p,1,1,1,100,7));pc_tick(&p,100+60000*PC_TICKS_MS,7);assert(p.phase==PC_FAILED);
 pc_init(&p);pc_offer(&p,&m);assert(!pc_begin(&p,1,1,1,m.active+1,7));r=pair(m.active+100,1000000100);assert(pc_observe(&p,&r,7));p.have_map=false;pc_tick(&p,p.listen_until,7);assert(p.phase==PC_FAILED);
 pc_init(&p);pc_offer(&p,&m);for(unsigned i=0;i<40;i++){r=pair(m.active+100+i,1000000000+i);assert(pc_observe(&p,&r,7));}assert(p.produced==40&&p.overwritten==8&&pc_read(&p,0,&q)&&q.sequence==9);
 r.origin.boot++;assert(pc_observe(&p,&r,7));assert(pc_read(&p,40,&q)&&!q.valid);
 struct r2_request req;char command[192];
 snprintf(command,sizeof command,"r2 00000001 1111111111111111 %064u 0000000000000001 pc-run 00000002 000003e80000ea60",0);
 assert(!r2_parse(command,&req)&&req.op==R2_PC_RUN&&req.trace_page==2&&req.nonce==UINT64_C(0x3e80000ea60));
 strcat(command," x");assert(r2_parse(command,&req));
 puts("PASS PC fixed budgets, frozen causal projection, cold reset, RF mode, epochs, failure denominators, ring gaps, parser");
}
