
/* MIT harness, Jonathan Yantis. Selected included Zephyr bodies Apache-2.0. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
static jmp_buf trapped;static const char *failed_expr;
#define LL_ASSERT(x) do {if(!(x)){failed_expr=#x;longjmp(trapped,1);}}while(0)
#define IS_ENABLED(x) 0
#define BIT(n) (1U<<(n))
#define ARG_UNUSED(x) ((void)(x))
#define DEBUG_RADIO_CLOSE(x) ((void)(x))
#define HAL_TICKER_CNTR_MSBIT 23
#define HAL_TICKER_CNTR_MASK 0xffffff
#define EVENT_PIPELINE_MAX 7
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_BUSY 2
#define TICKER_USER_ID_LLL 0
#define TICKER_USER_ID_ULL_LOW 2
#define CONFIG_BT_CTLR_LLL_PRIO 0
#define CONFIG_BT_CTLR_ULL_LOW_PRIO 1
#define MROUND(x) (((x)+7)&~7)
#define MALIGN(x) __attribute__((aligned(8)))
#define __noinline __attribute__((noinline))
static void cpu_dmb(void){}
#include "mfifo.h"
struct lll_prepare_param {void *param;uint32_t ticks_at_expire;uint32_t remainder;uint16_t lazy;uint8_t force;};
typedef int (*lll_prepare_cb_t)(struct lll_prepare_param*);
typedef int (*lll_is_abort_cb_t)(void*,void*,lll_prepare_cb_t*);
typedef void (*lll_abort_cb_t)(struct lll_prepare_param*,void*);
struct lll_event {struct lll_prepare_param prepare_param;lll_prepare_cb_t prepare_cb;lll_abort_cb_t abort_cb;lll_is_abort_cb_t is_abort_cb;uint8_t is_resume,is_aborted;};
struct ull_hdr {int placeholder;};
struct lll_scan{};struct lll_scan_aux{};
struct lll_conn {struct ull_hdr hdr;uint8_t forced,role;uint16_t lazy_prepare,latency_prepare;struct {uint32_t window_widening_prepare_us,window_widening_periodic_us,window_widening_max_us,window_widening_event_us,window_size_event_us;}periph;};
static struct lll_conn objects[4];
#define HDR_LLL2ULL(p) (&((struct lll_conn*)(p))->hdr)
static struct {struct {void *param;lll_is_abort_cb_t is_abort_cb;lll_abort_cb_t abort_cb;}curr;}event;
struct mayfly {unsigned a,b;void *link,*param;void(*fp)(void*);};typedef int memq_link_t;
static unsigned preempt_starts,done_notifications,aborts,prepared;
static void *pending_preempt;
static uint32_t ticker_ticks_diff_get(uint32_t a,uint32_t b){return(a-b)&0xffffff;}
static uint32_t preempt_ticker_start(struct lll_event *f,struct lll_event *p,struct lll_event *n){(void)p;(void)n;pending_preempt=f->prepare_param.param;++preempt_starts;return 0;}
static void *ull_scan_lll_is_valid_get(void *p){(void)p;return NULL;}
static void *ull_scan_aux_lll_is_valid_get(void *p){(void)p;return NULL;}
static bool r2_precision_radio(void *p){return p==&objects[3];}
static struct lll_event *r2_prepare_failure(lll_prepare_cb_t cb,struct lll_prepare_param *p){(void)cb;(void)p;return NULL;}
static void *ull_event_done(struct ull_hdr *p){(void)p;++done_notifications;return &done_notifications;}
static int mayfly_enqueue(unsigned a,unsigned b,unsigned c,struct mayfly *m){(void)a;(void)b;(void)c;m->fp(m->param);return 0;}
static void mayfly_enable(unsigned a,unsigned b,unsigned c){(void)a;(void)b;(void)c;LL_ASSERT(false);}
static int lll_done(void*);
static void *ull_prepare_dequeue_get(void);
static void *ull_prepare_dequeue_iter(uint8_t*);
static void ull_prepare_dequeue(uint8_t);
static void lll_resume(void*);

static struct lll_event *resume_enqueue(lll_prepare_cb_t);
static struct lll_event *prepare_dequeue_iter_ready_get(uint8_t*);
static int lll_prepare_resolve(lll_is_abort_cb_t,lll_abort_cb_t,lll_prepare_cb_t,struct lll_prepare_param*,uint8_t,uint8_t);
MFIFO_DEFINE(prep,sizeof(struct lll_event),7);
#define CONFIG_BT_CENTRAL 1
#define CONFIG_BT_PERIPHERAL 1
#define CONFIG_BT_CTLR_LE_ENC 1
#define BT_HCI_ROLE_PERIPHERAL 1
#define LLL_CONN_MIC_NONE 0
#define EVENT_DONE_EXTRA_TYPE_CONN 1
static unsigned trx_cnt,tx_cnt,crc_valid,mic_state,is_aborted;
struct event_done_extra {unsigned type,trx_cnt,crc_valid,is_aborted,mic_state;struct {uint32_t start_to_address_actual_us,window_widening_event_us,preamble_to_addr_us;}drift;};
static struct event_done_extra extra;
static struct event_done_extra *ull_event_done_extra_get(void){return &extra;}
static unsigned disable_calls,queued_clock_releases,irq_completions;
static void (*pending_isr)(void *);static void *pending_owner;
static bool radio_idle;
static unsigned timer_stops,radio_stops,status_resets;
static void isr_done(void *p);
static void lll_isr_cleanup(void *p);
static void isr_race(void *p){(void)p;LL_ASSERT(false);}
static void radio_isr_set(void (*cb)(void *),void *p){
 LL_ASSERT(cb==isr_done||cb==isr_race);
 if(cb==isr_done){pending_isr=cb;pending_owner=p;}else{pending_isr=NULL;pending_owner=NULL;}
}
static void radio_disable(void){++disable_calls;LL_ASSERT(pending_isr);}
static bool radio_is_idle(void){return radio_idle;}
static void radio_tmr_stop(void){++timer_stops;}
static void radio_stop(void){++radio_stops;}
static void lll_isr_status_reset(void){++status_resets;}
static uint32_t radio_tmr_aa_restore(void){return 123;}
static uint32_t radio_tmr_ready_get(void){return 23;}
static uint32_t addr_us_get(unsigned phy){(void)phy;return 40;}
static int lll_hfclock_off(void){++queued_clock_releases;return 0;}
#include "actual_lifecycle.inc"

static int fake_prepare(struct lll_prepare_param *p){(void)p;++prepared;return 0;}
static unsigned occupancy(void){return mfifo_avail_count_get(mfifo_prep.n,mfifo_fifo_prep.f,mfifo_fifo_prep.l);}
static lll_is_abort_cb_t predicate(unsigned role){return role?lll_conn_peripheral_is_abort_cb:lll_conn_central_is_abort_cb;}
static void reset(unsigned role){
 memset(&event,0,sizeof event);memset(objects,0,sizeof objects);MFIFO_INIT(prep);
 preempt_starts=done_notifications=aborts=prepared=disable_calls=queued_clock_releases=irq_completions=0;
 timer_stops=radio_stops=status_resets=0;radio_idle=false;memset(&extra,0,sizeof extra);
 pending_preempt=pending_owner=NULL;pending_isr=NULL;failed_expr=NULL;trx_cnt=tx_cnt=crc_valid=mic_state=is_aborted=0;
 objects[0].role=objects[1].role=role;objects[0].periph.window_widening_event_us=11;objects[0].periph.window_size_event_us=12;
 event.curr.param=&objects[0];event.curr.abort_cb=lll_conn_abort_cb;event.curr.is_abort_cb=predicate(role);
}
static void arrival(unsigned role,unsigned owner,unsigned t){
 struct lll_prepare_param p={.param=&objects[owner],.ticks_at_expire=t,.lazy=2};
 LL_ASSERT(lll_prepare_resolve(predicate(role),lll_conn_abort_cb,fake_prepare,&p,0,0)==-EINPROGRESS);
}
static void deliver_fake_disabled(void){
 LL_ASSERT(pending_isr);void (*cb)(void*)=pending_isr;void *p=pending_owner;pending_isr=NULL;pending_owner=NULL;radio_idle=true;++irq_completions;cb(p);
}
static void controls(unsigned role){
 for(unsigned exchanged=0;exchanged<2;exchanged++)for(unsigned foreign=0;foreign<2;foreign++){
  reset(role);trx_cnt=tx_cnt=exchanged;arrival(role,foreign,1);preempt(pending_preempt);
  bool request=exchanged||foreign;
#ifdef FIXED
  request=true;
#endif
  LL_ASSERT(occupancy()==1&&event.curr.param==&objects[0]&&!prepared&&!irq_completions&&!queued_clock_releases);
  LL_ASSERT(disable_calls==(unsigned)request&&!!pending_isr==request);
  if(request){deliver_fake_disabled();LL_ASSERT(extra.type==EVENT_DONE_EXTRA_TYPE_CONN&&extra.trx_cnt==trx_cnt&&extra.is_aborted==is_aborted);if(role&&trx_cnt){LL_ASSERT(extra.drift.start_to_address_actual_us==100&&extra.drift.window_widening_event_us==11&&extra.drift.preamble_to_addr_us==40&&!objects[0].periph.window_widening_event_us&&!objects[0].periph.window_size_event_us);}LL_ASSERT(!occupancy()&&prepared==1&&irq_completions==1&&done_notifications==1&&queued_clock_releases==1&&timer_stops==1&&radio_stops==1&&status_resets==1);}
 }
 reset(role);objects[0].forced=1;arrival(role,0,1);preempt(pending_preempt);
 LL_ASSERT(!disable_calls&&!pending_isr&&event.curr.param==&objects[0]&&!occupancy()&&queued_clock_releases==1);
 LL_ASSERT(extra.is_aborted==1); /* Existing queued-cancel path, not active abort. */
}
int main(void){
 if(setjmp(trapped)){fprintf(stderr,"unexpected control assertion %s\n",failed_expr);return 1;}
 for(unsigned role=0;role<2;role++){
  controls(role);
#ifdef FIXED
  reset(role);for(unsigned i=0;i<100;i++){
   arrival(role,0,i+1);preempt(pending_preempt);
   LL_ASSERT(occupancy()==1&&event.curr.param==&objects[0]&&irq_completions==i&&queued_clock_releases==i);
   if(role)LL_ASSERT(is_aborted==1); /* Original zero-TX peripheral side effect. */
   deliver_fake_disabled();LL_ASSERT(!occupancy()&&prepared==i+1&&done_notifications==i+1);
  }
  printf("role=%u repeated_zero_exchange requests=100 explicit_fake_DISABLED=100 real_MFIFO_retire=100 PASS\n",role);
#endif
  /* No completion is invented: even patched cancellation cannot retire if
   * the separately modelled peripheral IRQ never arrives. Bound at8. */
  reset(role);volatile unsigned attempted=0;
  if(!setjmp(trapped)){for(unsigned i=0;i<8;i++){attempted=i+1;arrival(role,0,i+1);preempt(pending_preempt);}}
  LL_ASSERT(attempted==8&&occupancy()==7&&failed_expr&&!strcmp(failed_expr,"next"));
#ifdef FIXED
  LL_ASSERT(disable_calls==7&&pending_isr&&irq_completions==0);
#else
  LL_ASSERT(!disable_calls&&!pending_isr&&irq_completions==0);
#endif
  printf("role=%u missing_completion bounded8th_assert=next queue=7 requests=%u PASS_CONDITIONAL\n",role,disable_calls);
 }
 puts("LIMIT actual predicates/active+queued abort/preempt/lll_done/MFIFO; actual connection isr_done/lll_isr_cleanup; fake mayfly, prepare,ticker,HAL/explicit DISABLED IRQ delivery and LLCP/HCI. No field initiating-race or physical RF correctness proof.");
 return 0;
}
