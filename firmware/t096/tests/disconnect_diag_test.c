/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#if defined(TEST_TRANSPORT)
#define R2_DISCONNECT_DIAGNOSTICS 1
#define main existing_transport_main
#include "gm_standby_transport_test.c"
#undef main
static unsigned snapshot_calls;
uint16_t r2_disconnect_snapshot(struct bt_conn *c,uint64_t p,uint32_t i)
{assert(c&&c->refs>0);(void)p;(void)i;++snapshot_calls;return 0xf5d8;}
int main(void){
 first_fault=(struct r2_transport_fault){0};assert(stranded_control()==-ETIMEDOUT);
 assert(snapshot_calls==1&&first_fault.host_detail==0xf5d8&&first_fault.operation==3);
 struct r2_transport_fault first=first_fault;
 assert(!transport_error(-EIO,"SCAN_START",2));assert(first_fault.host_detail==first.host_detail&&first_fault.at==first.at);
 assert(stranded_control()==-ETIMEDOUT);assert(snapshot_calls==1&&first_fault.host_detail==0xf5d8);
 first_fault=(struct r2_transport_fault){0};assert(transport_error(-EIO,"MTU",2));
 assert(stranded_control()==-ETIMEDOUT);assert(snapshot_calls==1&&!first_fault.host_detail&&first_fault.operation==1);
 puts("PASS actual transport: first terminal samples once; retained prior fault and unrelated error cannot overwrite detail");
}
#elif defined(TEST_DISABLED)
#include <assert.h>
#include "disconnect_diag.h"
int main(void){assert(!r2_disconnect_snapshot(0,1,2));}
#else
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define R2_DISCONNECT_DIAGNOSTICS 1
#define R2_TIMING_EVENTS_TEST 1
#include "conn_internal.h"
#include "controller_snapshot.h"
static unsigned tx_calls,deferred_calls,events,raw_seen,detail_seen,a_seen,b_seen;
static unsigned irq_depth,locks,unlocks,controller_calls;
static uint64_t peer_seen;
static struct bt_conn *mutate,*current;
static unsigned mutate_on_lock;
static struct ptob_controller_snapshot injected;
static unsigned irq_lock(void){assert(!irq_depth);irq_depth=1;++locks;if(mutate_on_lock){current->state=BT_CONN_DISCONNECTING;current->handle=2;}return 42;}
static void irq_unlock(unsigned key){assert(key==42&&irq_depth==1);irq_depth=0;++unlocks;}
void ptob_controller_snapshot_locked(uint16_t h,struct ptob_controller_snapshot *out)
{assert(irq_depth==1&&current&&h==current->handle);++controller_calls;*out=injected;}
static int k_work_busy_get(const struct k_work *w){assert(!irq_depth);++tx_calls;if(mutate){mutate->state=BT_CONN_DISCONNECTED;mutate->handle=123;}return w->flags;}
static int k_work_delayable_busy_get(const struct k_work_delayable *w){assert(!irq_depth);++deferred_calls;return w->work.flags;}
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){
 assert(!irq_depth&&k==32);++events;assert(p==peer_seen&&a==a_seen);
 if(d==25){raw_seen=(unsigned)r;detail_seen=d;b_seen=b;}
 else if(d==26){assert((unsigned)r==injected.lll_handle);assert(b==(injected.handle_in_range|(injected.handle_matches<<1)|(injected.head_present<<2)|(injected.head_done<<3)|(injected.local_state<<8)|(injected.remote_state<<12)|(injected.head_proc<<16)|((uint32_t)injected.head_state<<24)));}
 else if(d==27){assert((unsigned)r==injected.reason_final);assert(b==(injected.local_prt|((uint32_t)injected.remote_prt<<16)));}
 else if(d==28){assert((unsigned)r==injected.prt_reload);assert(b==(injected.supervision_expire|((uint32_t)injected.connect_expire<<16)));}
 else assert(0);
}
#include "../src/disconnect_diag.c"
int main(void){
 assert(!r2_disconnect_snapshot(NULL,1,2)&&!events&&!tx_calls&&!deferred_calls);
 injected=(struct ptob_controller_snapshot){.lll_handle=5,.handle_in_range=1,.handle_matches=1,.head_present=1,.head_done=1,.local_state=3,.remote_state=4,.head_proc=10,.head_state=2,.local_prt=133,.remote_prt=21,.prt_reload=134,.supervision_expire=9,.connect_expire=2,.reason_final=22};
 for(unsigned raw=0;raw<256;raw++)for(unsigned tx=0;tx<32;tx++)for(unsigned deferred=0;deferred<32;deferred++){
  struct bt_conn c={.state=(bt_conn_state_t)raw,.handle=0xbeef,.tx_complete_work={.flags=tx},.deferred_work={.work={.flags=deferred}}};
  unsigned expected=raw==BT_CONN_CONNECTED?1:raw==BT_CONN_DISCONNECTING?2:raw==BT_CONN_DISCONNECT_COMPLETE?3:raw==BT_CONN_DISCONNECTED?4:5;
  current=&c;peer_seen=0x123456789abcdef0ULL;a_seen=1506;unsigned before=events;uint16_t got=r2_disconnect_snapshot(&c,0x123456789abcdef0ULL,1506);
#ifdef CONFIG_BT_CONN_TX
  unsigned observed_tx=tx;
#else
  unsigned observed_tx=0;
#endif
  assert(got==((expected<<3)|(observed_tx<<6)|(deferred<<11))&&!(got&7));
  assert(events==before+4&&raw_seen==raw&&peer_seen==0x123456789abcdef0ULL&&detail_seen==25&&a_seen==1506);
  assert(b_seen==(0xbeef|(observed_tx<<16)|(deferred<<21)));
 }
#ifdef CONFIG_BT_CONN_TX
 struct bt_conn c={.state=BT_CONN_CONNECTED,.handle=0xbeef};mutate=&c;current=&c;peer_seen=1;a_seen=2;
 assert((r2_disconnect_snapshot(&c,1,2)&56)==8&&raw_seen==BT_CONN_CONNECTED&&c.state==BT_CONN_DISCONNECTED&&((b_seen&0xffff)==0xbeef));
#else
 assert(tx_calls==0);
#endif
 struct bt_conn locked={.state=BT_CONN_CONNECTED,.handle=9};
 current=&locked;mutate=NULL;mutate_on_lock=1;peer_seen=7;a_seen=8;
 assert((r2_disconnect_snapshot(&locked,7,8)&56)==16);
 assert(raw_seen==BT_CONN_DISCONNECTING&&(b_seen&0xffff)==2);
 mutate_on_lock=0;
 assert(deferred_calls*4==events&&controller_calls==deferred_calls&&locks==controller_calls&&unlocks==locks&&!irq_depth);
 puts("PASS actual snapshot:256raw states x32x32work masks; unavailable; event fields; shared IRQ snapshot; unlocked work and journal; legacy bits preserved");
}
#endif
