/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual producer/dispatcher/node-manage/callback bodies. Dequeue, node-update
 * and release/reuse are reduced boundaries. Schedules are prescribed controls,
 * never a reconstruction of field ordering or complete controller lifecycle. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TICKER_STATUS_FAILURE 1
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_BUSY 2
#define TICKER_CALL_ID_JOB 0
#define TICKER_USER_OP_TYPE_NONE 0
#define TICKER_USER_OP_TYPE_START 1
#define TICKER_USER_OP_TYPE_UPDATE 2
#define TICKER_USER_OP_TYPE_STOP 3
#define TICKER_USER_OP_TYPE_STOP_ABS 4
#define TICKER_USER_OP_TYPE_YIELD_ABS 5
#define CONFIG_BT_TICKER_EXT 1
#define CONFIG_BT_TICKER_EXT_EXPIRE_INFO 1
#define CONFIG_BT_TICKER_UPDATE 1
#define IS_ENABLED(x) (x)
#define TICKER_NULL 255
#define BIT(x) (1u<<(x))
#define HAL_TICKER_CNTR_MSBIT 23
static void test_dmb(void);
#define cpu_dmb() test_dmb()
typedef void (*ticker_op_func)(uint32_t,void *);
struct ticker_ext {uint8_t expire_info_id;void *other_expire_info;};
struct ticker_node {uint8_t req,ack;uint32_t ticks_to_expire;struct ticker_ext *ext_data;};
struct ticker_user_op {uint8_t op,id,status;struct {struct {
 int8_t r2_phase_us;uint32_t ticks_drift_plus,ticks_drift_minus,ticks_slot_plus,ticks_slot_minus;
 uint16_t lazy;uint8_t force,must_expire,expire_info_id;
 } update;struct {uint32_t ticks_at_yield;} yield;} params;ticker_op_func fp_op_func;void *op_context;};
struct ticker_user {uint8_t first,middle,last,count_user_op;struct ticker_user_op *user_op;};
struct ticker_instance {struct ticker_node *nodes;struct ticker_user *users;uint8_t count_user;
 uint32_t ticks_current,ticks_slot_previous;uint8_t ticker_id_slot_previous;
 void (*sched_cb)(uint8_t,uint8_t,uint8_t,void *);uint8_t (*caller_id_get_cb)(uint8_t);};
static struct ticker_instance _instance[1];
static uint32_t ticker_dequeue(struct ticker_instance *i,uint8_t id){(void)i;(void)id;return 0;}
static uint32_t ticker_job_node_update(struct ticker_instance *i,struct ticker_node *n,struct ticker_user_op *o,uint32_t a,uint32_t b,uint32_t c,uint8_t *h){(void)i;(void)n;(void)o;(void)a;(void)b;(void)c;(void)h;return 0;}
static void ticker_free_expire_info(struct ticker_instance *i,uint8_t id){(void)i;(void)id;}
static void ticker_mark_expire_info_outdated(struct ticker_instance *i,uint8_t id){(void)i;(void)id;}
static uint32_t ticker_ticks_diff_get(uint32_t a,uint32_t b){return a-b;}
struct ll_conn {struct {unsigned latency_cancel;} periph;uint16_t handle;};
static unsigned assertions,hooks,scheduled,preempt,reused;
static uint32_t raw_status;
static void ptob_latency_cancel_fault(uint32_t s,void *p){assert(p);++hooks;raw_status=s;}
#define LL_ASSERT(x) do{if(!(x)){++assertions;return;}}while(0)
uint8_t ticker_update_ext(uint8_t,uint8_t,uint8_t,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,uint8_t,ticker_op_func,void *,uint8_t,uint8_t);
#include "fault_dispatch.inc"
static struct ticker_node n;
static struct ticker_user_op q[4][2];
static struct ticker_user u[4];
static struct ll_conn c;
static uint8_t head;
static void sched(uint8_t a,uint8_t b,uint8_t ch,void *p){(void)a;(void)b;(void)ch;(void)p;++scheduled;}
static uint8_t caller(uint8_t user){return user;}
static void run(void){assert(!ticker_job_list_manage(&_instance[0],0,0,&head));}
static void setup(void){
 memset(q,0,sizeof q);memset(u,0,sizeof u);n=(struct ticker_node){.req=1};c=(struct ll_conn){.periph.latency_cancel=1};
 for(unsigned i=0;i<4;i++){u[i].count_user_op=2;u[i].user_op=q[i];}
 _instance[0]=(struct ticker_instance){.nodes=&n,.users=u,.count_user=4,.sched_cb=sched,.caller_id_get_cb=caller,.ticker_id_slot_previous=255};
 head=255;assertions=hooks=scheduled=preempt=reused=0;
}
static uint8_t update(void){return ticker_update(0,3,0,0,0,0,0,1,0,ticker_update_latency_cancel_op_cb,&c);}
static void test_dmb(void){
 if(preempt){
  unsigned mode=preempt;preempt=0;
  assert(u[3].last==0&&q[3][0].op==TICKER_USER_OP_TYPE_UPDATE);
  assert(ticker_stop(0,1,0,NULL,NULL)==TICKER_STATUS_BUSY);run();assert(n.req==n.ack);
  /* Reduced release/reuse boundary: no real ll_conn allocator or LLL drain. */
  c.handle=UINT16_MAX;
  if(mode==2){c.handle=0;c.periph.latency_cancel=1;n.req=n.ack+1;++reused;}
 }
}
int main(void){
 setup();assert(update()==TICKER_STATUS_BUSY);run();assert(!assertions&&!hooks&&!c.periph.latency_cancel);
 setup();assert(ticker_stop(0,1,0,NULL,NULL)==TICKER_STATUS_BUSY);run();assert(n.req==n.ack);
 assert(update()==TICKER_STATUS_BUSY);run();assert(assertions==1&&hooks==1&&raw_status==1&&c.periph.latency_cancel);
 /* Both queues published: descending user3 executes update before user1 stop. */
 setup();assert(ticker_stop(0,1,0,NULL,NULL)==TICKER_STATUS_BUSY);assert(update()==TICKER_STATUS_BUSY);
 assert(ticker_job_list_manage(&_instance[0],0,0,&head)==1);
 assert(!assertions&&!hooks&&!c.periph.latency_cancel&&u[1].middle==0);
 /* Reduced worker-bottom-half acknowledgement before the deferred stop. */
 ++n.ack;run();assert(n.req==n.ack);
 /* Actual producer is interrupted at its prepublication DMB. */
 setup();preempt=1;assert(update()==TICKER_STATUS_BUSY);run();assert(assertions==1&&hooks==1&&c.periph.latency_cancel);
 setup();preempt=2;assert(update()==TICKER_STATUS_BUSY);run();assert(reused==1&&!assertions&&!hooks&&!c.periph.latency_cancel);
 /* Queue-full submit refuses before callback, preserving original distinction. */
 setup();u[3].first=1;assert(update()==TICKER_STATUS_FAILURE);assert(!assertions&&!hooks&&c.periph.latency_cancel);
 puts("PASS actual producer/dispatcher/callback: active, processed stop, simultaneous queues, prepublication stop, reduced immediate reuse, full queue");
}
