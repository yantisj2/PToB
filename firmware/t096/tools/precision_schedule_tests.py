#!/usr/bin/env python3
"""Exact generated ticker/LLL arbitration and bounded phase policy; no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from precision_schedule import generate
from radio_capture_hooks import generate as radio
from ticker_backport import generate as ticker
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--legacy-scheduler',action='store_true',help='Expected-failure control using the pinned stock collision function');p.add_argument('--legacy-phase-dispatch',action='store_true',help='Expected-failure control omitting fractional work from the empty-update gate');a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(exist_ok=False)
generate(a.deps/'zephyr',out/'ull');radio(a.deps/'zephyr',out/'lll');ticker(a.deps/'zephyr',out/'ticker')
def fn(body,name):
 m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 end=body.index('\n}',body.index('{',m.start()))+2
 return body[m.start():end]
body=(out/'ticker/ticker/ticker.c').read_text()
collision=(a.deps/'zephyr/subsys/bluetooth/controller/ticker/ticker.c').read_text() if a.legacy_scheduler else body
src='''#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "rc_phase_policy.h"
#define CONFIG_BT_TICKER_PRIORITY_SET 1
#define TICKER_PRIORITY_CRITICAL -128
#define TICKER_NULL 255
#define TICKER_HAS_SLOT_WINDOW(n) 0
#define HAL_TICKER_RESCHEDULE_MARGIN 1
#define HAL_TICKER_REMAINDER_RANGE 30517578u
struct ticker_node {uint8_t next,force;int8_t priority;uint16_t lazy_current,lazy_periodic;
 uint32_t ticks_slot,ticks_periodic,ticks_to_expire;};
typedef void (*lll_prepare_cb_t)(void);
static int precision;
bool r2_precision_radio(void *p){return p==&precision;}
static int lll_is_abort_cb(void *a,void *b,lll_prepare_cb_t *c){(void)a;(void)b;(void)c;return -ECANCELED;}
'''+fn(collision,'ticker_resolve_collision')+'\n'+fn(body,'r2_ticker_remainder_step')+'\n'+fn((out/'lll/lll_adv_sync.c').read_text(),'r2_tx_is_abort')+'''
int main(void){
 struct ticker_node n[3]={{.next=1,.ticks_slot=20,.ticks_periodic=100,.priority=-1},
  {.next=255,.ticks_to_expire=10,.ticks_slot=20,.ticks_periodic=100,.force=255,.lazy_current=65535}};
 assert(!ticker_resolve_collision(n,&n[0])); /* even aged/forced ACL loses */
 n[0].priority=0;n[1].priority=-1;n[0].force=255;n[1].force=0;n[0].lazy_current=65535;
 assert(ticker_resolve_collision(n,&n[0]));
 n[1].ticks_to_expire=21;assert(!ticker_resolve_collision(n,&n[0])); /* no overlap */
 n[1].ticks_to_expire=10;n[1].ticks_slot=0;assert(!ticker_resolve_collision(n,&n[0]));
 n[1].ticks_slot=20;n[1].lazy_periodic=2;n[1].lazy_current=0;assert(!ticker_resolve_collision(n,&n[0]));
 n[0].priority=n[1].priority=-1;n[0].lazy_current=n[1].lazy_current=0;
 n[0].force=0;n[1].force=1;n[1].lazy_periodic=0;assert(ticker_resolve_collision(n,&n[0]));
 int control;assert(!r2_tx_is_abort(&control,&precision,NULL));
 assert(r2_tx_is_abort(&precision,&precision,NULL)==-ECANCELED);
 assert(r2_tx_is_abort(NULL,&precision,NULL)==-ECANCELED); /* explicit cancellation */
 assert(!rc_phase_step(500000,6000000,10000000,10000001,9000001,true));
 assert(!rc_phase_step(500000,6000000,10000000,11000000,0,true));
 assert(!rc_phase_step(500000,6000000,10000000,9999999,0,true));
 assert(!rc_phase_step(6000001,6000000,10000000,10000001,0,true));
 assert(!rc_phase_step(500000,6000000,10000000,10000001,0,false));
 /* Six simulated hours in either direction; frequency error is physical RF
  * phase only. No source/map/deadline is synthesized by the servo. */
 for(int drift=-80;drift<=80;drift+=160){
  int64_t error=0;uint64_t last=0;unsigned steps=0;
  for(uint64_t i=1;i<=16*21600;i++){
   uint64_t tx=i*1000000;error+=drift*62500/1000;
   assert(error<6000000&&error>-6000000);
   int step=rc_phase_step(error,6000000,tx,tx+16000,last,true);
   if(step){error-=step*8000;last=tx;++steps;}
  }
  assert(steps>1000&&error<300000&&error>-300000);
 }
 /* Fractional drift across both RTC carry directions: no31us jump. */
 uint32_t rem=0;int64_t ticks=0;
 for(unsigned i=0;i<100000;i++)ticks+=r2_ticker_remainder_step(&rem,8);
 assert(ticks*30517578+(int32_t)rem==800000000000LL);
 for(unsigned i=0;i<100000;i++)ticks+=r2_ticker_remainder_step(&rem,-8);
 assert(!ticks&&!rem);
 puts("PASS exact ticker collision/LLL preemption, bounded six-hour RF phase policy, stale/illegal/deadline guards");
}
'''
(out/'exact.c').write_text(src)
dispatch=fn(body,'ticker_job_list_manage')
if a.legacy_phase_dispatch:
 dispatch=dispatch.replace('(user_op->params.update.r2_phase_us == 0) &&\n\t\t\t     ','')
dispatch_src='''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TICKER_STATUS_FAILURE 1
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_BUSY 2
#define TICKER_CALL_ID_JOB 0
#define TICKER_USER_OP_TYPE_START 1
#define TICKER_USER_OP_TYPE_UPDATE 2
#define TICKER_USER_OP_TYPE_STOP_ABS 4
#define TICKER_USER_OP_TYPE_YIELD_ABS 5
#define CONFIG_BT_TICKER_EXT 1
#define CONFIG_BT_TICKER_EXT_EXPIRE_INFO 1
#define cpu_dmb() ((void)0)
typedef void (*ticker_op_func)(uint32_t,void *);
struct ticker_node {uint8_t req,ack;void *ext_data;};
struct ticker_user_op {uint8_t op,id,status;struct {struct {
 int8_t r2_phase_us;uint32_t ticks_drift_plus,ticks_drift_minus,ticks_slot_plus,ticks_slot_minus;
 uint16_t lazy;uint8_t force,must_expire,expire_info_id;
 } update;} params;ticker_op_func fp_op_func;void *op_context;};
struct ticker_user {uint8_t first,middle,last,count_user_op;struct ticker_user_op *user_op;};
struct ticker_instance {struct ticker_node *nodes;struct ticker_user *users;uint8_t count_user;
 void (*sched_cb)(uint8_t,uint8_t,uint8_t,void *);uint8_t (*caller_id_get_cb)(uint8_t);};
static struct ticker_instance _instance[1];
static int accepted,scheduled;static int8_t accepted_step;
static void ticker_job_op_cb(struct ticker_user_op *op,uint32_t status){op->status=status;if(op->fp_op_func)op->fp_op_func(status,op->op_context);}
static void ticker_job_node_manage(struct ticker_instance *i,struct ticker_node *n,struct ticker_user_op *o,uint32_t now,uint32_t elapsed,uint8_t *head){
 (void)i;(void)n;(void)now;(void)elapsed;(void)head;++accepted;accepted_step=o->params.update.r2_phase_us;ticker_job_op_cb(o,TICKER_STATUS_SUCCESS);}
static void schedule(uint8_t a,uint8_t b,uint8_t c,void *p){(void)a;(void)b;(void)c;(void)p;++scheduled;}
static uint8_t caller(uint8_t x){return x;}
'''+dispatch+'\n'+fn(body,'r2_ticker_phase_step')+'''
int main(void){
 struct ticker_node node[1]={{.req=1}};struct ticker_user_op ops[2]={0};
 struct ticker_user user[1]={{.count_user_op=2,.user_op=ops}};
 _instance[0]=(struct ticker_instance){.nodes=node,.users=user,.count_user=1,.sched_cb=schedule,.caller_id_get_cb=caller};
 for(int step=-8;step<=8;step+=16){
  memset(ops,0,sizeof ops);user[0].first=user[0].middle=user[0].last=0;accepted=scheduled=0;uint8_t head=255;
  assert(r2_ticker_phase_step(0,0,0,step,0,0)==TICKER_STATUS_BUSY&&scheduled==1);
  assert(!ticker_job_list_manage(&_instance[0],100,0,&head));
  assert(accepted==1&&accepted_step==step&&ops[0].status==TICKER_STATUS_SUCCESS);
 }
 /* A genuinely empty stock update still fails; inactive tickers cannot be
  * revived by a phase request. Expired work is deferred until the bottom half. */
 user[0].middle=0;user[0].last=1;ops[0].params.update.r2_phase_us=0;accepted=0;uint8_t head=255;
 assert(!ticker_job_list_manage(&_instance[0],100,0,&head)&&!accepted&&ops[0].status==TICKER_STATUS_FAILURE);
 user[0].middle=0;ops[0].params.update.r2_phase_us=8;node[0].req=0;
 assert(!ticker_job_list_manage(&_instance[0],100,0,&head)&&!accepted&&ops[0].status==TICKER_STATUS_FAILURE);
 node[0].req=2;user[0].middle=0;scheduled=0;
 assert(ticker_job_list_manage(&_instance[0],100,0,&head)&&!accepted&&scheduled==1&&user[0].middle==0);
 node[0].req=1;assert(!ticker_job_list_manage(&_instance[0],100,0,&head)&&accepted==1);
 puts("PASS exact queued fractional API and dispatcher: both signs execute; empty/stopped rejected; expired work defers");
}
'''
(out/'dispatch.c').write_text(dispatch_src)
cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(app/'include'),str(out/'exact.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  dispatch_cmd=cmd[:-3]+[str(out/'dispatch.c'),'-o',str(out/'dispatch-test')]
  subprocess.run(dispatch_cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'dispatch-test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'extracted_sha256':hashlib.sha256(src.encode()).hexdigest(),'dispatch_sha256':hashlib.sha256(dispatch_src.encode()).hexdigest(),'legacy_phase_dispatch':a.legacy_phase_dispatch},indent=2)+'\n')
print('PASS timing-first controller and bounded phase tests')
