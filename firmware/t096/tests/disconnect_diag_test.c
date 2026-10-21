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
static unsigned tx_calls,deferred_calls,events,raw_seen,detail_seen,a_seen,b_seen;
static uint64_t peer_seen;
static struct bt_conn *mutate;
static int k_work_busy_get(const struct k_work *w){++tx_calls;if(mutate)mutate->state=BT_CONN_DISCONNECTED;return w->flags;}
static int k_work_delayable_busy_get(const struct k_work_delayable *w){++deferred_calls;return w->work.flags;}
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{assert(k==32);++events;raw_seen=(unsigned)r;peer_seen=p;detail_seen=d;a_seen=a;b_seen=b;}
#include "../src/disconnect_diag.c"
int main(void){
 assert(!r2_disconnect_snapshot(NULL,1,2)&&!events&&!tx_calls&&!deferred_calls);
 for(unsigned raw=0;raw<256;raw++)for(unsigned tx=0;tx<32;tx++)for(unsigned deferred=0;deferred<32;deferred++){
  struct bt_conn c={.state=(bt_conn_state_t)raw,.handle=0xbeef,.tx_complete_work={.flags=tx},.deferred_work={.work={.flags=deferred}}};
  unsigned expected=raw==BT_CONN_CONNECTED?1:raw==BT_CONN_DISCONNECTING?2:raw==BT_CONN_DISCONNECT_COMPLETE?3:raw==BT_CONN_DISCONNECTED?4:5;
  unsigned before=events;uint16_t got=r2_disconnect_snapshot(&c,0x123456789abcdef0ULL,1506);
#ifdef CONFIG_BT_CONN_TX
  unsigned observed_tx=tx;
#else
  unsigned observed_tx=0;
#endif
  assert(got==((expected<<3)|(observed_tx<<6)|(deferred<<11))&&!(got&7));
  assert(events==before+1&&raw_seen==raw&&peer_seen==0x123456789abcdef0ULL&&detail_seen==25&&a_seen==1506);
  assert(b_seen==(0xbeef|(observed_tx<<16)|(deferred<<21)));
 }
#ifdef CONFIG_BT_CONN_TX
 struct bt_conn c={.state=BT_CONN_CONNECTED};mutate=&c;
 assert((r2_disconnect_snapshot(&c,1,2)&56)==8&&raw_seen==BT_CONN_CONNECTED&&c.state==BT_CONN_DISCONNECTED);
#else
 assert(tx_calls==0);
#endif
 assert(deferred_calls==events);
 puts("PASS actual snapshot:256raw states x32x32work masks; unavailable; event fields; nontransactional state captured once");
}
#endif
