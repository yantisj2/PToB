/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production owner with synthetic time/capture/storage/radio boundaries. */
#include "lora_service.h"
#include "lora_passive_core.h"
#include "lora_hw.h"
#include "lora_watchdog.h"
#include "capture.h"
#include "rc_shadow.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
union r2_diag_scratch r2_diag_scratch;
typedef long atomic_t;typedef long atomic_val_t;
static long atomic_get(const atomic_t *p){return *p;}
static void atomic_set(atomic_t *p,long v){*p=v;}
static long atomic_and(atomic_t *p,long v){long a=*p;*p&=v;return a;}
static void atomic_or(atomic_t *p,long v){*p|=v;}
static void atomic_inc(atomic_t *p){++*p;}
static bool atomic_cas(atomic_t *p,long a,long b){if(*p!=a)return false;*p=b;return true;}
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static unsigned lock_depth;
static void (*unlock_hook)(void);
static int k_spin_lock(struct k_spinlock *p){(void)p;++lock_depth;return 0;}
static void k_spin_unlock(struct k_spinlock *p,int k){(void)p;(void)k;assert(lock_depth);--lock_depth;
 if(!lock_depth&&unlock_hook){void (*hook)(void)=unlock_hook;unlock_hook=NULL;hook();}}
struct k_thread{int priority;};
static void k_thread_priority_set(struct k_thread *t,int p){assert(p>9);t->priority=p;}
static int k_thread_priority_get(struct k_thread *t){return t->priority;}
static void k_wakeup(struct k_thread *t){(void)t;}
#define K_THREAD_STACK_DEFINE(n,s) unsigned char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_MSEC(n) n
static void k_thread_create(struct k_thread *t,void *s,size_t n,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int p,int q,int r){assert(p>9);t->priority=p;(void)s;(void)n;(void)fn;(void)a;(void)b;(void)c;(void)q;(void)r;}
static void k_thread_name_set(struct k_thread *t,const char *n){(void)t;(void)n;}
static uint64_t now_us=1000000;
static uint64_t k_uptime_ticks(void){return now_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
static uint32_t k_uptime_get_32(void){return (uint32_t)(now_us/1000);}
static void k_sleep(int n){now_us+=(uint64_t)n*1000;}
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned k){(void)k;}
static int r2_diag_state(void){return 1;}
static int r2_diag_lora(const uint32_t p[16],bool t,uint64_t *token){(void)p;(void)t;(void)token;return -EAGAIN;}
static int r2_diag_record_result(uint64_t t){(void)t;return 0;}
static bool r2_diag_busy(void){return false;}
static void r2_watchdog_lora(int e,uint32_t s,uint32_t g,uint32_t c,int j){(void)e;(void)s;(void)g;(void)c;(void)j;}
static bool witness_enabled;static unsigned witness_published,witness_withdrawn;static struct rc_shadow_map witness;
bool r2_shadow_enabled(void){return witness_enabled;}
void r2_shadow_normal_map(const struct rc_shadow_map *m){witness=*m;++witness_published;}
void r2_shadow_lora_withdraw(void){++witness_withdrawn;}
/* Any persistence call is a test failure; no flash implementation is linked. */
static uint32_t random_seed=0x51a7c002;
static uint32_t sys_rand32_get(void){return ++random_seed;}
static uint64_t sent_at_us;static unsigned txs,rearms,stops;static unsigned stop_ack_missing;static uint8_t sent[128];static size_t sent_len;static uint32_t sent_generation;
static bool fake_tx_capture_live;static unsigned fake_done_consumed;
static bool have_event,reject_next_tx;static struct r2_lora_hw_event queued;
static uint64_t fake_capture_epoch=7;
static void (*capture_now_hook)(void);
static bool capture_valid=true;static unsigned tuning_phy,tuning_channel,cancel_on;
bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){.tick=now_us*16,.epoch=fake_capture_epoch,.kind=R2_CAPTURE_NOW};if(capture_now_hook){void (*hook)(void)=capture_now_hook;capture_now_hook=NULL;hook();}return capture_valid;}
bool r2_capture_now_at(struct r2_capture *c,uint64_t *us){*us=now_us;return r2_capture_now(c);}
int r2_lora_hw_start(uint32_t g,bool rx){fake_tx_capture_live=false;(void)g;assert(!rx);if(cancel_on==1){cancel_on=0;r2_lora_source_withdraw();return -ECANCELED;}return 0;}
uint32_t r2_lora_hw_budget_us(size_t n){for(unsigned k=LP_DISC;k<=LP_FOLLOW;k++)if(lp_size(k)==n)return lp_duration(k);return 0;}
int r2_lora_hw_tx_before(const uint8_t *p,size_t n,uint32_t g,uint64_t before){assert(now_us+r2_lora_hw_budget_us(n)<=before);++txs;fake_tx_capture_live=true;sent_at_us=now_us;sent_len=n;memcpy(sent,p,n);sent_generation=g;return 0;}
/* Use the real capture-only admission/diagnostic implementation extracted
 * from lora_hw.c. Only SPI duration and physical completion are synthetic. */
static uint64_t fake_hw_now(void){return now_us;}
static void count(uint32_t *v){if(*v<UINT32_MAX)++*v;}
#define now_us() fake_hw_now()
#define active_epoch fake_capture_epoch
#include R2_HW_ADMISSION_BODY
#undef active_epoch
#undef now_us
static void (*fake_encode_hook)(void),(*fake_prepare_hook)(void);
static uint64_t fake_spi_delay_us,fake_encode_delay_us,fake_launch_delay_us,fake_config_delay_us;static bool unknown_tx_error,fake_scheduling_abort,fake_launch_canceled;
static void (*fake_launch_hook)(void);
static uint8_t prepared_bytes[128];static size_t prepared_size;static uint32_t prepared_generation;
int r2_lora_hw_prepare(const uint8_t *p,size_t n,uint32_t g){
 prepared_size=0;now_us+=fake_spi_delay_us;if(fake_prepare_hook)fake_prepare_hook();
 if(unknown_tx_error)return -EIO;
 assert(n<=sizeof prepared_bytes);memcpy(prepared_bytes,p,n);prepared_size=n;prepared_generation=g;return 0;
}
int r2_lora_hw_launch_before_source(uint32_t g,uint64_t before,uint64_t until,uint64_t epoch){
 assert(prepared_size&&g==prepared_generation);uint32_t budget=r2_lora_hw_budget_us(prepared_size);
 int rc=r2_lora_hw_source_preflight(budget,g,before,until,epoch,now_us);
 if(rc){if(rc==-EAGAIN)rejected.point=2;prepared_size=0;return rc;}
 now_us+=fake_launch_delay_us;if(fake_launch_hook)fake_launch_hook();
 if(fake_launch_canceled){fake_launch_canceled=false;no_rf_proof=false;prepared_size=0;return -ECANCELED;}
 /* Model native FEM callback checks at SetTx commit, after preemption. The
  * callback has no synchronous healthy-no-RF refund proof. */
 if(fake_scheduling_abort){fake_scheduling_abort=false;no_rf_proof=false;prepared_size=0;return -EINTR;}
 rc=r2_lora_hw_source_preflight(budget,g,before,until,epoch,now_us);
 if(rc){no_rf_proof=false;rejected.point=rejected.reason==R2_LORA_REJECT_SOURCE?4:5;prepared_size=0;return -EINTR;}
 if(unknown_tx_error){no_rf_proof=false;prepared_size=0;return -EIO;}
 if(reject_next_tx){reject_next_tx=false;reject_launch(R2_LORA_REJECT_FINISH,3,g,budget+1000,until,before,true);prepared_size=0;return -EAGAIN;}
 if(now_us>=before||(uint64_t)budget+1000>before-now_us){
  reject_launch(R2_LORA_REJECT_FINISH,5,g,budget+1000,until,before,false);prepared_size=0;return -EINTR;}
 rc=r2_lora_hw_tx_before(prepared_bytes,prepared_size,g,before);prepared_size=0;return rc;
}
int r2_lora_hw_tx_before_source(const uint8_t *p,size_t n,uint32_t g,uint64_t before,uint64_t until,uint64_t epoch){
 uint32_t budget=r2_lora_hw_budget_us(n);
 int rc=r2_lora_hw_source_preflight(budget,g,before,until,epoch,now_us);
 if(rc){if(rc==-EAGAIN)rejected.point=2;return rc;}
 now_us+=fake_spi_delay_us;
 if(unknown_tx_error){no_rf_proof=false;return -EIO;}
 if(reject_next_tx||now_us>=before||(uint64_t)budget+1000>before-now_us){
  reject_next_tx=false;reject_launch(R2_LORA_REJECT_FINISH,3,g,budget+1000,until,before,true);return -EAGAIN;}
 return r2_lora_hw_tx_before(p,n,g,before);
}
int r2_lora_hw_take(struct r2_lora_hw_event *e){if(!have_event)return 0;*e=queued;have_event=false;if(e->kind==R2_LORA_HW_TX_DONE){fake_tx_capture_live=false;++fake_done_consumed;}return 1;}
bool r2_lora_hw_tx_edge(struct r2_lora_hw_edge *e){
 if(have_event&&queued.kind==R2_LORA_HW_TX_DONE){*e=(struct r2_lora_hw_edge){queued.tick,queued.capture_epoch,queued.generation};return true;}
 if(!fake_tx_capture_live)return false;
 *e=(struct r2_lora_hw_edge){sent_at_us*16,fake_capture_epoch,sent_generation};return true;
}
int r2_lora_hw_idle(void){return 0;}
int r2_lora_hw_passive_configure(enum r2_lora_passive_phy p,unsigned c){now_us+=fake_config_delay_us;tuning_phy=p;tuning_channel=c;if(cancel_on==2){cancel_on=0;r2_lora_source_withdraw();return -ECANCELED;}return 0;}
int r2_lora_hw_rx_rearm(void){++rearms;return 0;}
void r2_lora_hw_stop_begin(uint64_t d){fake_tx_capture_live=false;assert(d);++stops;}
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *a){*a=(struct r2_lora_hw_ack){!(stop_ack_missing&1),!(stop_ack_missing&2),!(stop_ack_missing&4),!(stop_ack_missing&8),false};}
void r2_lora_hw_diag(struct r2_lora_hw_diag *h){memset(h,0,sizeof *h);}
void r2_lora_hw_watchdog_trip(int e){(void)e;r2_diag_scratch.lora.terminal=1;}
static uint64_t charged_unlocked(const struct lp_checkpoint *s,uint64_t now){assert(!lock_depth);return lp_charged(s,now);}
static void (*projection_hook)(void);
static bool event_unlocked(struct rc8_correction *c,const struct rc8_export *m,uint32_t sequence,struct rc8_event event){
 assert(!lock_depth);
 if(projection_hook){void (*hook)(void)=projection_hook;projection_hook=NULL;hook();}
 return rc8_export_event(c,m,sequence,event);
}
static int encode_delayed(uint8_t *w,size_t cap,const struct lp_packet *p,const uint8_t ctx[16],const struct r2_group_config *g){
 now_us+=fake_encode_delay_us;if(fake_encode_hook)fake_encode_hook();return lp_encode(w,cap,p,ctx,g);
}
#define lp_encode encode_delayed
#define lp_charged charged_unlocked
#define rc8_export_event event_unlocked
#include R2_PASSIVE_SERVICE_BODY
#undef rc8_export_event
#undef lp_charged
#undef lp_encode
static const struct r2_group_config test_group={.id=11,.epoch=1,.psk={1,2,3}};
static void step(void){service_step();now_us+=2000;}
static struct rc8_export source(void){return(struct rc8_export){.scope={.emitter=1,.emitter_boot=22,.stream=1,.root=1,.root_boot=22,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_OWN_GM},.from_tick=0,.until_tick=UINT64_MAX,.anchor_tick=0,.anchor_ns=UINT64_C(1000000000000),.capture_epoch=7,.created_ns=0,.expires_ns=UINT64_MAX,.map_generation=1,.rate_ppb=0,.mode=RC8_MAP_LINEAR,.authenticated=true,.valid=true};}
static void init(bool tx){memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);r2_lora_init(tx?1:2,22,1,&test_group);assert(!r2_diag_scratch.lora.drain_late&&!r2_diag_scratch.lora.origin.active&&!r2_diag_scratch.lora.monitor);r2_lora_control(false,true,RC8_SELECT_NONE);}
static void done(uint64_t start,unsigned kind){now_us=start+lp_duration(kind)-10000;queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;step();}
static void packet(const struct lp_packet *p,const uint8_t ctx[16],uint64_t event,uint64_t completion){
 now_us=completion;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,ctx,&test_group));have_event=true;step();
}
