/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Extracted production context publication and worker loop, fake boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include "ui.h"
typedef int32_t atomic_t;
static atomic_t context_revision=1,context_flags,model_requested;
static struct r2_ui_context published_context;static bool context_seen;
static unsigned mutex_calls,mutex_depth,sets,steps,sleeps;
static int atomic_get(atomic_t *p){return *p;}
static int atomic_set(atomic_t *p,int32_t v){int old=*p;*p=v;return old;}
static unsigned ui_take(void){assert(!mutex_depth);++mutex_calls;++mutex_depth;return 0;}
static void ui_drop(unsigned key){(void)key;assert(mutex_depth==1);--mutex_depth;}
#include "context_under_test.h"
static struct k_thread{int priority;}worker_thread;
static unsigned desired;static jmp_buf exited;
static void service_step(void){++steps;}
static int worker_priority(void){return (int)desired;}
static int k_thread_priority_get(struct k_thread *t){return t->priority;}
static void k_thread_priority_set(struct k_thread *t,int p){++sets;t->priority=p;}
#define K_MSEC(n) n
static void k_sleep(int ms){assert(ms==2);++sleeps;longjmp(exited,1);}
#include "worker_under_test.h"
static void iteration(int current,unsigned target,unsigned expected){
 worker_thread.priority=current;desired=target;unsigned before=sets;
 if(!setjmp(exited))worker(NULL,NULL,NULL);
 assert(sets-before==expected&&worker_thread.priority==(int)target);
}
int main(void){
 struct r2_ui_context c={.incarnation=1,.root=4,.boot=73,.source_epoch=1,.train=1,.flags=R2_UI_CONTEXT_LIVE};
 r2_ui_context_update(&c);assert(mutex_calls==1&&context_seen&&context_revision==2);
 for(unsigned i=0;i<100000;i++)r2_ui_context_update(&c);
 assert(mutex_calls==1&&context_revision==2&&!mutex_depth);
 for(unsigned field=0;field<6;field++){
  uint32_t prior=(uint32_t)context_revision;unsigned before=mutex_calls;
  if(field==0)++c.incarnation;if(field==1)++c.root;if(field==2)++c.boot;
  if(field==3)++c.source_epoch;if(field==4)++c.train;if(field==5)c.flags|=R2_UI_CONTEXT_STOP;
  atomic_set(&model_requested,0);r2_ui_context_update(&c);
  assert(mutex_calls==before+1&&(uint32_t)context_revision==prior+1&&model_requested==1);
  assert(context_flags==(int32_t)c.flags&&!mutex_depth);
  r2_ui_context_update(&c);assert(mutex_calls==before+1);
 }
 atomic_set(&context_revision,-1);++c.boot;r2_ui_context_update(&c);
 assert(context_revision==-1&&published_context.boot==c.boot);
 iteration(11,11,0);iteration(11,10,1);iteration(10,10,0);iteration(10,11,1);
 assert(steps==4&&sleeps==4&&sets==2);
 puts("PASS100000unchanged context calls acquire0mutexes; eachchanged field publishes atomic revision; saturation retained");
 puts("PASS redundant priorities11/10 skipsetter; bothrealtransitions set; everyiteration sleeps2ms");
 return 0;
}
