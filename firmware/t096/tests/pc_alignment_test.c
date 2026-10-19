/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "pc_projection_under_test.h"
static struct rc_shadow_map map(uint64_t t,unsigned gen){
 return (struct rc_shadow_map){.epoch=7,.lo=t,.active=t+1,.root=11,.root_boot=12,.root_epoch=13,
  .map={.effective=t,.anchor_tick=t,.anchor_ns=1000000000,.generation=gen}};
}
static struct rc5_reference pair(uint64_t rx){
 return (struct rc5_reference){.valid=true,.rx=rx,.completed_at=rx+70*PC_TICKS_MS,
  .parent_tx_ns=1000000000,.origin={.root=11,.boot=12,.epoch=13}};
}
static void ready(struct pc_experiment *p,uint64_t base){
 pc_init(p);struct rc5_reference r=pair(base-2*PC_PACKET_TICKS);
 assert(pc_observe(p,&r,7));struct rc_shadow_map m=map(r.completed_at,1);pc_offer(p,&m);
}
int main(void){
 const uint64_t base=UINT64_C(16000000);
 unsigned holds[]={5000,10000,20000};
 for(unsigned h=0;h<3;h++)for(unsigned offset=0;offset<63;offset++){
  struct pc_experiment p;ready(&p,base);uint64_t command=base+offset*PC_TICKS_MS;
  uint64_t anchor=p.phase_rx;assert(!pc_begin_aligned(&p,2,125,holds[h],command,7));
  uint64_t start=p.requested,period=(125+holds[h])*PC_TICKS_MS;
  assert(p.mode==PC_ALIGNED_SHADOW&&p.commanded==command&&p.alignment_rx==anchor);
  assert(start>=command+5*PC_TICKS_MS&&start<command+5*PC_TICKS_MS+PC_PACKET_TICKS);
  assert((start+PC_PHASE_GUARD_TICKS-anchor)%PC_PACKET_TICKS==0);
  pc_tick(&p,start-1,7);assert(p.phase==PC_LISTEN&&!p.training_pairs);
  for(unsigned c=0;c<2;c++){
   uint64_t lo=start+c*period,hi=lo+125*PC_TICKS_MS;
   struct rc5_reference r=pair(lo+PC_PHASE_GUARD_TICKS-PC_PACKET_TICKS);
   assert(r.completed_at>=lo&&r.rx<lo&&!pc_observe(&p,&r,7));
   r=pair(lo+PC_PHASE_GUARD_TICKS);assert(r.completed_at<hi&&pc_observe(&p,&r,7));
   struct rc_shadow_map m=map(r.completed_at,c+2);pc_offer(&p,&m);
   /* The next capture is in LISTEN, but its follow-up is too late. */
   r=pair(lo+PC_PHASE_GUARD_TICKS+PC_PACKET_TICKS);
   assert(r.rx<hi&&r.completed_at>=hi&&!pc_observe(&p,&r,7));
   assert(p.phase==PC_HOLD&&p.training_pairs==c+1&&!p.missed_bursts);
   assert(p.requested==start&&p.alignment_rx==anchor); /* No off-window retime. */
   pc_tick(&p,lo+period,7);
  }
  assert(p.phase==PC_DONE&&p.training_pairs==2&&!p.missed_bursts);
  struct rc5_reference r=pair(p.last_tick+1);assert(!pc_observe(&p,&r,7));
 }
 /* Unaligned control at an unfavorable phase misses the same 70ms pair. */
 struct pc_experiment p;ready(&p,base);
 assert(!pc_begin_cadence(&p,1,125,5000,base+2*PC_TICKS_MS,7));
 struct rc5_reference r=pair(base+PC_PACKET_TICKS);
 assert(r.rx<p.listen_until&&r.completed_at>=p.listen_until&&!pc_observe(&p,&r,7));
 assert(p.missed_bursts==1&&!p.training_pairs);
 ready(&p,base);assert(pc_begin_aligned(&p,1,250,5000,base,7)==-EINVAL);
 assert(pc_begin_aligned(&p,1,125,1001,base,7)==-EINVAL);
 assert(pc_begin_aligned(&p,UINT32_MAX,125,UINT32_MAX,base,7)==-EINVAL);
 p.phase_rx=base-9000000;p.phase_available=base-8000000;assert(pc_begin_aligned(&p,1,125,5000,base,7)==-EAGAIN);
 ready(&p,base);p.phase_root++;assert(pc_begin_aligned(&p,1,125,5000,base,7)==-EAGAIN);
 ready(&p,base);assert(pc_begin_aligned(&p,1,125,5000,base,8)==-EAGAIN);
 ready(&p,base);assert(!pc_begin_aligned(&p,1,125,5000,base,7));
 r=pair(p.requested+PC_PHASE_GUARD_TICKS);r.origin.boot++;
 assert(!pc_observe(&p,&r,7)&&p.phase==PC_FAILED&&!p.training_pairs);
 ready(&p,base);assert(!pc_begin_aligned(&p,1,125,5000,base,7));
 r=pair(p.requested+PC_PHASE_GUARD_TICKS);r.completed_at=r.rx+132*PC_TICKS_MS;
 assert(!pc_observe(&p,&r,7)&&p.missed_bursts==1); /* No widening after loss. */
 ready(&p,UINT64_MAX-10000000);
 struct rc_shadow_map m=map(UINT64_MAX-2000,1);pc_offer(&p,&m);
 p.phase_rx=UINT64_MAX-3000000;p.phase_available=UINT64_MAX-1880000;
 assert(pc_begin_aligned(&p,1,125,5000,UINT64_MAX-1000,7)==-EOVERFLOW);
 puts("PASS PC aligned125ms:189 phase/HOLD schedules, complete-pair gate, no hidden retiming, late/missing pairs, source changes, stale anchors and overflow; unaligned phase control misses");
}
