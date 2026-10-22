/* Copyright (c) 2026 Jonathan Yantis. MIT.
 * Actual generated enqueue/failure helper, real MFIFO, real scratch writer.
 * Sequential IRQ boundary controls; no LLL producer/preemption integration. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "controller_diag.h"
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch;
static unsigned depth,locks,notes,iterations;
static uint32_t ms=1234;
static int phase;
static bool drain_on_lock;
static void on_outer_lock(void);
unsigned irq_lock(void){unsigned old=depth;++locks;if(!old)on_outer_lock();depth++;return old;}
void irq_unlock(unsigned old){assert(depth==old+1);depth=old;}
uint32_t k_uptime_get_32(void){assert(depth);return ms;}
int r2_diag_state(void){return phase;}
bool r2_controller_diag_ready_locked(void){assert(depth);return phase>=1;}
int r2_console_note(const char *line,size_t n,uint64_t until){assert(!depth&&n<320&&until==ms+150);assert(strstr(line,"0000000000000009"));notes++;return -ENOBUFS;}
typedef void (*lll_prepare_cb_t)(void);typedef void (*lll_abort_cb_t)(void);typedef void (*lll_is_abort_cb_t)(void);
struct lll_prepare_param {void *param;};
struct lll_event {struct lll_prepare_param prepare_param;lll_prepare_cb_t prepare_cb;lll_abort_cb_t abort_cb;lll_is_abort_cb_t is_abort_cb;unsigned is_resume,is_aborted;};
#define MROUND(x) (((x)+7)&~7)
#define MALIGN(x) __attribute__((aligned(8)))
#define __noinline __attribute__((noinline))
static void cpu_dmb(void){}
#define mfifo_dequeue_iter_get raw_mfifo_dequeue_iter_get
#include "mfifo.h"
#undef mfifo_dequeue_iter_get
static void *mfifo_dequeue_iter_get(uint8_t *fifo,uint8_t size,uint8_t count,uint8_t first,uint8_t last,uint8_t *idx){
 assert(depth&&first<count&&last<count&&(*idx==UINT8_MAX||*idx<count));++iterations;
 return raw_mfifo_dequeue_iter_get(fifo,size,count,first,last,idx);
}
MFIFO_DEFINE(prep,sizeof(struct lll_event),7);
static void on_outer_lock(void){if(drain_on_lock){drain_on_lock=false;(void)MFIFO_DEQUEUE(prep);}}
#include "prepare_enqueue.inc"
static void fill(unsigned n){
 for(unsigned i=0;i<n;i++){
  struct lll_prepare_param p={(void *)(uintptr_t)(0x100+i)};
  unsigned before=locks;
  struct lll_event *e=ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)(uintptr_t)(0x200+i),i&1);
  assert(e);e->is_aborted=(i>>1)&1;
#ifndef R2_CONTROLLER_DIAGNOSTICS
  assert(locks==before); /* no healthy observer call */
#else
  (void)before;
#endif
 }
}
int main(int argc,char **argv){
 assert(argc==2&&sizeof r2_diag_scratch==512&&mfifo_prep.n==8);
 memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);
 union r2_diag_scratch untrusted=r2_diag_scratch;
 uint32_t w[23];
 for(int p=-3;p<=0;p++){
  phase=p;r2_controller_diag_snapshot(w);assert(w[22]&R2_CONTROLLER_DIAG_UNKNOWN);
  assert(!w[6]&&!r2_controller_diag_failure(1,2));assert(!memcmp(&untrusted,&r2_diag_scratch,sizeof untrusted));
 }
 phase=1;struct r2_lora_watchdog_state prefix=r2_diag_scratch.lora;
 for(unsigned wrap=0;wrap<8;wrap++){
  r2_controller_diag_init();MFIFO_INIT(prep);iterations=0;
  fill(wrap);for(unsigned i=0;i<wrap;i++)(void)MFIFO_DEQUEUE(prep);
  fill(7);assert(mfifo_fifo_prep.f==wrap&&mfifo_avail_count_get(8,mfifo_fifo_prep.f,mfifo_fifo_prep.l)==7);
  unsigned char queue[sizeof mfifo_fifo_prep];memcpy(queue,&mfifo_fifo_prep,sizeof queue);
  struct lll_prepare_param p={(void *)0x999};
  assert(!ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)0x888,0));
  assert(!memcmp(queue,&mfifo_fifo_prep,sizeof queue)&&iterations==7);
  r2_controller_diag_snapshot(w);assert(w[4]==0x888&&w[5]==0x999&&w[6]==8&&w[21]==ms&&!(w[22]&R2_CONTROLLER_DIAG_UNKNOWN));
#ifndef R2_CONTROLLER_DIAGNOSTICS
  assert(!w[0]&&!w[1]&&!w[2]&&!w[3]&&(w[22]&R2_CONTROLLER_DIAG_FAILURE_ONLY));
#else
  assert(w[0]==wrap+7&&!(w[22]&R2_CONTROLLER_DIAG_FAILURE_ONLY));
#endif
  for(unsigned i=0;i<7;i++){assert(w[7+i*2]==0x200+i&&w[8+i*2]==0x100+i);assert(((w[22]>>(2*i))&3)==((i&1)|(((i>>1)&1)<<1)));}
  uint32_t frozen[23];memcpy(frozen,w,sizeof w);
  p.param=(void *)0x777;assert(!ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)0x666,1));assert(iterations==7);
  for(unsigned i=0;i<7;i++)(void)MFIFO_DEQUEUE(prep);assert(mfifo_fifo_prep.f==mfifo_fifo_prep.l);
#ifdef R2_CONTROLLER_DIAGNOSTICS
  for(unsigned i=0;i<4;i++)r2_controller_diag_count(i);
#endif
  r2_controller_diag_snapshot(w);assert(!memcmp(frozen,w,sizeof w));
  assert(!memcmp(&prefix,&r2_diag_scratch.lora,sizeof prefix));
 }
 FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(w,4,23,f)==23&&!fclose(f));
 /* Real original allocation can return NULL with corrupt indices; helper
  * must refuse iterator dereference, preserving the FIFO bytes. */
 const uint8_t corrupt[2][2]={{255,254},{0,255}};
 for(unsigned i=0;i<2;i++){
  r2_controller_diag_init();mfifo_fifo_prep.f=corrupt[i][0];mfifo_fifo_prep.l=corrupt[i][1];iterations=0;
  struct lll_prepare_param p={0};assert(!ull_prepare_enqueue(NULL,NULL,&p,NULL,0));
  r2_controller_diag_snapshot(w);assert(w[6]==1&&(w[22]&R2_CONTROLLER_DIAG_UNKNOWN)&&!iterations);
 }
 /* A drain before the outer mask produces an incomplete cut, never complete. */
 r2_controller_diag_init();MFIFO_INIT(prep);fill(7);drain_on_lock=true;iterations=0;
 struct lll_prepare_param p={0};assert(!ull_prepare_enqueue(NULL,NULL,&p,NULL,0));
 r2_controller_diag_snapshot(w);assert(w[6]==7&&(w[22]&R2_CONTROLLER_DIAG_UNKNOWN)&&iterations==7);
 union r2_diag_scratch saved=r2_diag_scratch;phase=-5;r2_controller_diag_snapshot(w);
 assert((w[22]&R2_CONTROLLER_DIAG_UNKNOWN)&&!w[6]&&!r2_controller_diag_failure(3,4)&&!memcmp(&saved,&r2_diag_scratch,sizeof saved));
#ifdef R2_CONTROLLER_DIAGNOSTICS
 phase=1;r2_controller_diag_console(9,ms);assert(notes==1);r2_controller_diag_console(9,ms);assert(notes==1);
#endif
 puts("PASS actual MFIFO all wraps/full/drain, flags, immutable failure, whole-copy IRQ, corrupt/partial UNKNOWN and phase/scratch isolation");
}
