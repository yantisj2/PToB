/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Selected queued restart reproduction, not a controller/RF simulator.
 * Generated production bodies retain Zephyr's Apache-2.0 terms.
 * Counter/compare HAL, mayfly handoff and LLCP/LLL completion are fake. */
#include <stdbool.h>
#include <stdlib.h>
#include "ticker_fairness_fake.h"
#define COUNT 9
#define ACLS 6
static uint32_t now;
static uint64_t wall;
static unsigned jobs, active, raw_wraps, invariant_checks;
static struct ticker_node n[COUNT];
static struct ticker_instance _instance[1];
#define in _instance[0]
static struct ticker_user users[4]; static struct ticker_user_op user_ops[4][16];
static unsigned completed[COUNT],case_number;
static uint8_t caller_id(uint8_t id){assert(id<4);return id;}
static void ticker_job_update_expire_infos(struct ticker_instance *i){(void)i;assert(!"unexpected expire-info refresh");}
static uint8_t ticker_job_reschedule_in_window(struct ticker_instance *i){for(unsigned x=0;x<COUNT;x++)assert(!i->nodes[x].ext_data);return 0;}
static uint8_t ticker_job_compare_update(struct ticker_instance *i,uint8_t h){(void)i;(void)h;return 0;}

static struct metric { uint64_t last, max_gap; unsigned calls, peak_lazy, callback_lazy, incarnations; } m[COUNT];
static unsigned periods[COUNT], slots[COUNT];
static uint32_t cntr_cnt_get(void){return now;}
static uint32_t ticker_ticks_diff_get(uint32_t a,uint32_t b){return (a-b)&HAL_TICKER_CNTR_MASK;}
static void ticker_mark_expire_info_outdated(struct ticker_instance *i,uint8_t id){assert(i==&in&&id<COUNT);}
static uint32_t ticker_alloc_expire_info(struct ticker_instance *i,uint8_t id){(void)i;(void)id;assert(!"unexpected ext_data allocation");return 1;}
static void ticker_free_expire_info(struct ticker_instance *i,uint8_t id){(void)i;(void)id;assert(!"unexpected ext_data free");}
#include "ticker_fairness_actual.inc"

static unsigned ticks(unsigned us){return ((uint64_t)us*1000000000ULL)/30517578125ULL;}
static unsigned remainder(unsigned us){return ((uint64_t)us*1000000000ULL-(uint64_t)ticks(us)*30517578125ULL)/1000;}
static void sched(uint8_t a,uint8_t b,uint8_t c,void *p){assert(a<4&&b==2&&c<=1&&p==&in);jobs++;}
static void producer_hook(unsigned,uint32_t);
static void cb(uint32_t at,uint32_t drift,uint32_t rem,uint16_t lazy,uint8_t force,void *p){
 (void)at;(void)drift;(void)rem;(void)force;
 unsigned id=(uintptr_t)p;assert(id<COUNT&&(active&(1u<<id)));
 uint64_t gap=wall-m[id].last;if(gap>m[id].max_gap)m[id].max_gap=gap;
 m[id].last=wall;m[id].calls++;if(lazy>m[id].callback_lazy)m[id].callback_lazy=lazy;producer_hook(id,at);
}
static void advance(unsigned delta){uint32_t old=now;now=(now+delta)&HAL_TICKER_CNTR_MASK;wall+=delta;if(now<old)raw_wraps++;}
static void observe(void){for(unsigned i=0;i<COUNT;i++)if(active&(1u<<i)){
 if(n[i].lazy_current>m[i].peak_lazy)m[i].peak_lazy=n[i].lazy_current;
 uint64_t gap=wall-m[i].last;if(gap>m[i].max_gap)m[i].max_gap=gap;
}}
static void verify(void){
 unsigned seen=0,bound=0,id=in.ticker_id_head;
 while(id!=TICKER_NULL){assert(id<COUNT&&++bound<=COUNT&&!(seen&(1u<<id)));seen|=1u<<id;
  assert((uint8_t)(n[id].req-n[id].ack)==1);assert(n[id].ticks_slot==slots[id]);
  assert(n[id].ticks_to_expire<HAL_TICKER_CNTR_MASK/2);id=n[id].next;
 }
 assert(seen==active&&in.ticks_slot_previous<=150);invariant_checks++;
}
static void op_cb(uint32_t status,void *p){unsigned id=(uintptr_t)p;assert(id<COUNT&&status==0);completed[id]++;}
static void dispatch(void){unsigned bound=0;do{assert(++bound<5);ticker_job(&in);}while(users[0].first!=users[0].last||users[1].first!=users[1].last||users[2].first!=users[2].last||users[3].first!=users[3].last);verify();}
static void bottom(void){dispatch();observe();}

static void start(unsigned id,unsigned us,unsigned slot,unsigned first){
 assert(!(active&(1u<<id))&&id<COUNT);slots[id]=slot;periods[id]=us;
 active|=1u<<id;m[id].last=wall;m[id].incarnations++;unsigned before=completed[id];
 assert(ticker_start(0,id<ACLS||id==8?1:3,id,in.ticks_current,first,ticks(us),remainder(us),0,slot,cb,(void*)(uintptr_t)id,op_cb,(void*)(uintptr_t)id)==2);
 dispatch();assert(completed[id]==before+1);
 if(id>=ACLS){assert(ticker_priority_set(0,3,id,-1,op_cb,(void*)(uintptr_t)id)==2);dispatch();}
 
}

/* Actual ull_conn_update_ticker is injected from pinned ull_conn.c, unmodified.
 * LLCP instant/LLL completion are fake caller boundaries, stated in receipt. */
static unsigned triggered,change_node,new_us,win_ticks,dle_mode;
static struct ll_conn {struct {uint32_t ticks_slot;} ull;struct {uint8_t role;} lll;unsigned id;} conns[COUNT];
static uint16_t ll_conn_handle_get(struct ll_conn *conn){return conn->id;}
static unsigned mayfly_enabled=1;
static uint32_t mayfly_is_enabled(unsigned a,unsigned b){assert(a==1&&b==2);return mayfly_enabled;}
static void mayfly_enable(unsigned a,unsigned b,unsigned enable){assert(a==1&&b==2);mayfly_enabled=enable;}
static void ticker_stop_conn_op_cb(uint32_t status,void *p){op_cb(status,(void*)(uintptr_t)((struct ll_conn*)p)->id);}
static void ticker_start_conn_op_cb(uint32_t status,void *p){op_cb(status,(void*)(uintptr_t)((struct ll_conn*)p)->id);}
static void ull_periph_ticker_cb(uint32_t a,uint32_t b,uint32_t c,uint16_t d,uint8_t e,void *p){cb(a,b,c,d,e,(void*)(uintptr_t)((struct ll_conn*)p)->id);}
static void ull_central_ticker_cb(uint32_t a,uint32_t b,uint32_t c,uint16_t d,uint8_t e,void *p){cb(a,b,c,d,e,(void*)(uintptr_t)((struct ll_conn*)p)->id);}
#include "ticker_fairness_connection.inc"
static void producer_hook(unsigned id,uint32_t at){
 if(id!=change_node||triggered||wall<8u*32768)return;
 triggered=1;conns[id].id=id;conns[id].lll.role=1;conns[id].ull.ticks_slot=slots[id];periods[id]=new_us;
 ull_conn_update_ticker(&conns[id],win_ticks,0,new_us,at);
 
}
static unsigned producer_trial(unsigned count,unsigned old_us,unsigned to_us,unsigned slot_mode,unsigned pa_mask,unsigned aligned,unsigned offset,unsigned wrap){
 memset(n,0,sizeof n);memset(m,0,sizeof m);memset(periods,0,sizeof periods);memset(slots,0,sizeof slots);memset(conns,0,sizeof conns);
 memset(users,0,sizeof users);memset(user_ops,0,sizeof user_ops);memset(completed,0,sizeof completed);
 for(unsigned x=0;x<4;x++){users[x].count_user_op=16;users[x].user_op=user_ops[x];}
 active=jobs=raw_wraps=invariant_checks=0;wall=0;now=wrap?0xff0000:0;
 triggered=0;change_node=0;new_us=to_us;win_ticks=offset;dle_mode=slot_mode;mayfly_enabled=1;
 in=(struct ticker_instance){.nodes=n,.users=users,.count_user=4,.count_node=COUNT,.caller_id_get_cb=caller_id,.ticks_current=now,.ticker_id_head=TICKER_NULL,.ticker_id_slot_previous=TICKER_NULL,.sched_cb=sched};
 /* Every initial ACL is cold44. DLE growth later uses exact producer slot delta
  * with force0/lazy0, through the actual public API and queued dispatcher. */
 for(unsigned i=0;i<count;i++)start(i,old_us,44,aligned?245:1+i*ticks(old_us)/count);
 for(unsigned i=6;i<COUNT;i++)if(pa_mask&(1u<<(i-6)))start(i,i==8?62437:62500,i==8?87:85,(i-6)*680);
 unsigned steps=0,dle_done=0;
 while(wall<24u*32768){
  assert(in.ticker_id_head!=TICKER_NULL&&++steps<200000);
  uint32_t next=(in.ticks_current+n[in.ticker_id_head].ticks_to_expire)&HAL_TICKER_CNTR_MASK;
  unsigned delta=ticker_ticks_diff_get(next,now);if(delta<HAL_TICKER_CNTR_MASK/2)advance(delta);
  ticker_worker(&in);observe();bottom();observe();
  if(slot_mode&&!dle_done&&wall>=2u*32768){
   
   for(unsigned id=0;id<count;id++){
    unsigned before=completed[id];slots[id]=150;
    assert(ticker_update(0,1,id,0,0,106,0,0,0,op_cb,(void*)(uintptr_t)id)==2);dispatch();assert(completed[id]==before+1);
   }
   dle_done=1;
  }
 }
 observe();unsigned starved=0;uint64_t gap=0;for(unsigned i=0;i<count;i++){
  if(m[i].max_gap>=6u*32768)starved|=1u<<i;if(m[i].max_gap>gap)gap=m[i].max_gap;}
 printf("case=%u DLE=%u wrap=%u callbacks=%u/%u max_gap_ticks=%llu starved=%u\n",
        case_number++,slot_mode,wrap,m[0].calls,m[1].calls,(unsigned long long)gap,starved);
 assert(triggered==1); return !!starved;
}

#ifndef LEGACY_POLICY
#include "ticker_fairness_legacy.inc"
static void pair_controls(void){
 unsigned cases=0,changed=0; int pri[]={-128,-1,0,1};
 for(unsigned a=0;a<4;a++)for(unsigned b=0;b<4;b++)for(unsigned f=0;f<2;f++)for(unsigned g=0;g<2;g++)for(unsigned l=0;l<5;l++)for(unsigned m=0;m<5;m++)for(unsigned off=0;off<2;off++){
  struct ticker_node x[2]={0}; x[0].next=1;x[1].next=TICKER_NULL;
  x[0].priority=pri[a];x[1].priority=pri[b];x[0].force=f;x[1].force=g;
  x[0].lazy_current=l;x[1].lazy_current=m;x[0].ticks_periodic=x[1].ticks_periodic=245;
  x[0].ticks_slot=x[1].ticks_slot=150;x[1].ticks_to_expire=off*40;
  unsigned old=legacy_resolve(x,x),new=ticker_resolve_collision(x,x);cases++;
  if(old!=new){changed++;assert(old==1&&new==0&&f>g);assert(pri[a]!=-128&&pri[b]!=-128&&((pri[a]==-1)==(pri[b]==-1)));}
  if(f==g||pri[a]==-128||pri[b]==-128||((pri[a]==-1)!=(pri[b]==-1)))assert(old==new);
 }
 struct ticker_node x[2]={0};x[0].next=1;x[1].next=255;x[0].ticks_periodic=x[1].ticks_periodic=245;x[0].ticks_slot=x[1].ticks_slot=150;
 x[0].force=1;x[1].lazy_current=1;assert(legacy_resolve(x,x)==1&&ticker_resolve_collision(x,x)==0);
 x[0].force=0;x[1].force=1;x[0].lazy_current=1;x[1].lazy_current=0;assert(legacy_resolve(x,x)==1&&ticker_resolve_collision(x,x)==1);
 printf("{\"pair_cases\":%u,\"changed\":%u,\"reciprocal_force_age\":\"PASS\"}\n",cases,changed);
}

#endif
int main(void){
#ifndef LEGACY_POLICY
 pair_controls();
#endif
 unsigned failed=0;
 /* 7492 = legal 7500us interval minus ceil(1000ppm widening). Two DLE
  * reservations (2*150 ticks) exceed period245; test progress, not capacity.
  * Initial phases are distinct; no full-period synthetic reservation. */
 for(unsigned dle=0;dle<2;dle++)for(unsigned wrap=0;wrap<2;wrap++)
  failed+=producer_trial(2,7492,7492,dle,0,0,0,wrap);
 printf("six_second_starvation_cases=%u/4\n",failed);
 return failed?1:0;
}
