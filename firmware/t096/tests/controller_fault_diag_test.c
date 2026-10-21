/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual helper, generated callback/getter and health fatal-copy bodies.
 * Fake IRQ/LL types establish bounded contracts, not physical IRQ latency. */
#include "health_fake.h"
#include "../src/health.c"
#include <assert.h>
#include <stdlib.h>
#include "controller_snapshot.h"
#define CONFIG_BT_MAX_CONN 6
#define TICKER_INSTANCE_MAX 1
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_ID_CONN_BASE 30
#define TICKER_STATUS_SUCCESS 0
struct proc_ctx {uint8_t proc,state,done;};
struct list {struct proc_ctx *head;};
struct ll_conn {
 struct {uint16_t handle,event_counter,latency_event;uint8_t role,access_addr[4];} lll;
 struct {uint8_t ref;} ull;
 struct {uint8_t latency_cancel;} periph;
 struct {struct {struct list pend_proc_list;uint8_t state;uint16_t prt_expire;} local,remote;uint16_t prt_reload;} llcp;
 struct {uint8_t reason_final;} llcp_terminate;
 uint16_t supervision_expire,connect_expire;
};
static struct ll_conn pool[6];
static unsigned lookups;
static struct ll_conn *ll_conn_get(unsigned h){assert(irq_depth&&h<6);++lookups;return &pool[h];}
static struct proc_ctx *sys_slist_peek_head(struct list *l){assert(irq_depth);return l->head;}
static void *update_mark,*disable_mark;
static void *ull_update_mark_get(void){assert(irq_depth);return update_mark;}
static void *ull_disable_mark_get(void){assert(irq_depth);return disable_mark;}
struct ticker_node {uint8_t req,ack;uint16_t lazy_current;uint32_t ticks_periodic;void *context;};
struct ticker_instance {struct ticker_node *nodes;uint8_t count_node,job_guard,worker_trigger;};
static struct ticker_instance _instance[1];
#include "fault_getter.inc"
/* Model one higher-priority interrupt after hook unlock. Thread PA writers
 * remain pending while the ULL ISR executes the unchanged fatal assertion. */
static unsigned nested_interrupts,pending_thread_writer;
static bool in_ull_isr;
static void callback_unlock(unsigned key){
 irq_unlock(key);
 assert(in_ull_isr);
 unsigned nested=irq_lock();++nested_interrupts;
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,-7);
 irq_unlock(nested);
 assert(pending_thread_writer); /* thread cannot run inside an ISR */
}
#define irq_unlock callback_unlock
#include "../src/controller_snapshot_body.inc"
#undef irq_unlock
static unsigned assertions;
#define LL_ASSERT(x) do{if(!(x)){++assertions;k_sys_fatal_error_handler(3,NULL);}}while(0)
#include "fault_callback.inc"
static int r2_console_note(const char *s,size_t n,uint64_t e){(void)s;(void)n;(void)e;return 0;}
int main(int argc,char **argv){
 assert(argc==2);r2_health_init(UINT64_C(0x1122334455667788));
 struct ticker_node node[36]={0};
 struct ptob_ticker_snapshot tick;
 _instance[0]=(struct ticker_instance){.nodes=node,.count_node=36,.job_guard=255,.worker_trigger=254};
 node[35]=(struct ticker_node){.req=255,.ack=254,.lazy_current=65535,.ticks_periodic=UINT32_MAX,.context=&pool[5]};
 struct ticker_node before[36];memcpy(before,node,sizeof node);
 ptob_ticker_snapshot_locked(1,35,&pool[5],&tick);assert(!tick.valid);
 ptob_ticker_snapshot_locked(0,36,&pool[5],&tick);assert(!tick.valid);
 ptob_ticker_snapshot_locked(0,255,&pool[5],&tick);assert(!tick.valid);
 _instance[0].nodes=NULL;ptob_ticker_snapshot_locked(0,0,&pool[5],&tick);assert(!tick.valid);
 _instance[0].nodes=node;
 ptob_ticker_snapshot_locked(0,35,&pool[5],&tick);
 assert(tick.valid&&tick.context_matches&&tick.req==255&&tick.ack==254&&tick.lazy_current==65535&&tick.ticks_periodic==UINT32_MAX&&tick.job_guard==255&&tick.worker_trigger==254);
 ptob_ticker_snapshot_locked(0,35,&pool[4],&tick);assert(tick.valid&&!tick.context_matches);
 node[35].req=node[35].ack;ptob_ticker_snapshot_locked(0,35,&pool[5],&tick);assert(tick.valid&&tick.req==tick.ack);
 node[35]=before[35];assert(!memcmp(before,node,sizeof node));
 for(unsigned i=0;i<6;i++){
  pool[i].lll.handle=i;pool[i].periph.latency_cancel=1;
  r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_DATA,77);
  struct operation saved=operations[R2_HEALTH_PA];unsigned reads=lookups;
  ticker_update_latency_cancel_op_cb(0,&pool[i]);
  assert(!pool[i].periph.latency_cancel&&!memcmp(&saved,&operations[R2_HEALTH_PA],sizeof saved)&&reads==lookups);
 }
 const uint32_t statuses[]={1,2,UINT32_MAX,0x80000000,0x12345678};
 for(unsigned i=0;i<6;i++)for(unsigned flags=0;flags<8;flags++)for(unsigned j=0;j<5;j++){
  pool[i].lll.handle=flags==7?UINT16_MAX:i;
  pool[i].periph.latency_cancel=!!(flags&4);
  update_mark=flags&1?&pool[i]:NULL;disable_mark=flags&2?&pool[i]:NULL;
  unsigned packed_flags=1|((flags&1)<<1)|((flags&2)<<1)|((flags&4)<<1);
  struct ll_conn saved[6];memcpy(saved,pool,sizeof pool);
  in_ull_isr=true;pending_thread_writer=1;unsigned faults=assertions;
  if(!setjmp(fatal_exit))ticker_update_latency_cancel_op_cb(statuses[j],&pool[i]);
  irq_depth=0;
  assert(assertions==faults+1&&!memcmp(saved,pool,sizeof pool));
  assert(r2_fault_capsule[21]==((PTOB_LATENCY_CANCEL_FAULT_MARKER<<24)|(i<<20)|(packed_flags<<16)|pool[i].lll.handle));
  assert(r2_fault_capsule[22]==statuses[j]&&r2_fault_capsule[23]==clock_ms);
  assert(r2_fault_capsule[63]==crc(r2_fault_capsule)&&r2_fault_capsule[4]==3);
  assert(operations[R2_HEALTH_PA].op==r2_fault_capsule[21]);
  in_ull_isr=false;
 }
 void *invalid[]={NULL,(void *)(uintptr_t)1,(void *)((uintptr_t)&pool[5]+1),&node[0]};
 for(unsigned i=0;i<4;i++){
  in_ull_isr=true;
  if(!setjmp(fatal_exit))ticker_update_latency_cancel_op_cb(0x80000000,invalid[i]);
  irq_depth=0;in_ull_isr=false;
  assert(r2_fault_capsule[21]==0xc1f0ffff&&r2_fault_capsule[22]==0x80000000);
 }
 /* Current slot reuse is captured as current; a valid handle is not generation. */
 pool[5].lll.handle=5;pool[5].periph.latency_cancel=1;update_mark=disable_mark=NULL;
 in_ull_isr=true;
 if(!setjmp(fatal_exit))ticker_update_latency_cancel_op_cb(0x80000000,&pool[5]);
 irq_depth=0;in_ull_isr=false;
 assert(r2_fault_capsule[21]==0xc1590005&&r2_fault_capsule[22]==0x80000000&&nested_interrupts==245);
 assert(r2_fault_capsule[2]==0x55667788&&r2_fault_capsule[3]==0x11223344);
 FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite((const void *)r2_fault_capsule,sizeof(uint32_t),64,f)==64);assert(!fclose(f));
 r2_health_init(9);assert(previous[21]==0xc1590005&&previous[22]==0x80000000);
 memset(previous,0,sizeof previous);r2_fault_capsule[0]=FAULT_MAGIC;r2_fault_capsule[21]^=1;
 r2_health_init(10);assert(!previous[0]);
 puts("PASS actual callback/helper/getter/fatal-copy/CRC: raw statuses, all marks/slots, invalid pointers, current slot reuse, nested IRQ and strict assertions");
}
