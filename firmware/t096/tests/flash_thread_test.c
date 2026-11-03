/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <pthread.h>
#include <errno.h>
#include "ticker_fairness_fake.h"
#undef cpu_dmb
static void preempt_before_publish(void);
#define cpu_dmb() preempt_before_publish()
#define K_FOREVER -1
#define K_MUTEX_DEFINE(n) static pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
static pthread_mutex_t state=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static _Thread_local bool competing,held;
static bool inject,attempted,entered,finished,launched,acl;
static pthread_t other;
static struct ticker_instance _instance[1];
static struct ticker_user users[4];
static struct ticker_user_op operations[8];
static int flash_context;
static struct {uint32_t interval,slot;} _ticker_sync_context={5800,7500};
static uint32_t ticker_ticks_now_get(void){return 0;}
static uint32_t ticks(uint32_t x){return x;}
static uint32_t remainder(uint32_t x){return 0;}
static uint8_t caller(uint8_t x){return x;}
static void scheduled(uint8_t a,uint8_t b,uint8_t c,void*d){}
static void time_slot_callback_prepare(uint32_t a,uint32_t b,uint32_t c,uint16_t d,uint8_t e,void*f){}
static void k_mutex_lock(pthread_mutex_t*m,int wait){
 if(competing){pthread_mutex_lock(&state);attempted=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&state);}
 assert(!pthread_mutex_lock(m));assert(!held);held=true;
}
static void k_mutex_unlock(pthread_mutex_t*m){assert(held);held=false;assert(!pthread_mutex_unlock(m));}
#include "flash_thread_locks.inc"
#include "flash_thread_actual.inc"
#include "flash_start_old.inc"
#include "flash_stop_old.inc"
#include "flash_start_new.inc"
#include "flash_stop_new.inc"
static int hci_cmd_handle(void*buf,void**node){
 assert(held);pthread_mutex_lock(&state);entered=true;pthread_mutex_unlock(&state);
 return ticker_start(0,3,2,0,1,0,0,0,1,NULL,NULL,NULL,NULL);
}
static int hci_acl_handle(void*buf,void**evt){
 assert(held);pthread_mutex_lock(&state);entered=true;pthread_mutex_unlock(&state);
 return ticker_update(0,3,2,0,0,0,0,1,0,NULL,NULL);
}
static void *hci(void*arg){
 competing=true;void*buf=NULL,*node_rx=NULL;int evt;
 if(acl){void *evt=NULL;int err;
#include "flash_thread_acl.inc"
  assert(err==TICKER_STATUS_BUSY&&!held);
 }else{
#include "flash_thread_hci.inc"
  assert(evt==TICKER_STATUS_BUSY&&!held);
 }
 pthread_mutex_lock(&state);finished=true;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&state);return NULL;
}
static void preempt_before_publish(void){
 if(competing||!inject)return;inject=false;launched=true;assert(!pthread_create(&other,NULL,hci,NULL));
 pthread_mutex_lock(&state);while(!attempted)pthread_cond_wait(&changed,&state);
 if(held)assert(!entered);else while(!finished)pthread_cond_wait(&changed,&state);
 pthread_mutex_unlock(&state);
}
static unsigned contains(uint8_t id){
 struct ticker_user*u=&users[3];unsigned count=0;
 for(unsigned i=u->first;i!=u->last;i=(i+1)%u->count_user_op)if(u->user_op[i].id==id)++count;
 return count;
}
static void reset(unsigned at){
 memset(users,0,sizeof users);memset(operations,0,sizeof operations);memset(_instance,0,sizeof _instance);
 users[3]=(struct ticker_user){.user_op=operations,.count_user_op=8,.first=at,.last=at};_instance[0].users=users;_instance[0].caller_id_get_cb=caller;_instance[0].sched_cb=scheduled;
 attempted=entered=finished=launched=false;inject=true;
}
int main(void){
 for(unsigned n=0;n<400;n++)for(unsigned stop=0;stop<2;stop++)for(unsigned kind=0;kind<2;kind++){
  acl=kind!=0;
  reset(n%8);uint32_t rc=stop?flash_stop_old():flash_start_old();assert(rc==TICKER_STATUS_BUSY&&launched);assert(!pthread_join(other,NULL));
  assert(contains(2)==1&&contains(8)==0); /* Accepted flash op was overwritten: no published flash callback can complete. */
  reset(n%8);rc=stop?flash_stop_new():flash_start_new();assert(rc==TICKER_STATUS_BUSY&&launched&&!held);assert(!pthread_join(other,NULL));
  assert(contains(8)==1&&contains(2)==1); /* Exact same interleave queues both; no callback/timeout policy change. */
 }
 reset(0);users[3].first=1;inject=false;assert(flash_start_new()==TICKER_STATUS_FAILURE&&!held);assert(flash_stop_new()==TICKER_STATUS_FAILURE&&!held);
 puts("PASS actual ticker THREAD publication: 1600 unguarded accepted flash operations lost; 1600 guarded start/stop + command/ACL operations retained across every ring index; fullqueue errors unlock; ULL/wait unchanged");
}
