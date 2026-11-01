/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production owner with synthetic time/capture/storage/radio boundaries. */
#include "lora_service.h"
#include "lora_passive_core.h"
#include "lora_passive_store.h"
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
static int k_spin_lock(struct k_spinlock *p){(void)p;++lock_depth;return 0;}
static void k_spin_unlock(struct k_spinlock *p,int k){(void)p;(void)k;assert(lock_depth);--lock_depth;}
struct k_thread{int unused;};
static void k_thread_priority_set(struct k_thread *t,int p){(void)t;assert(p==9||p==11);}
static void k_wakeup(struct k_thread *t){(void)t;}
#define K_THREAD_STACK_DEFINE(n,s) unsigned char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_MSEC(n) n
static void k_thread_create(struct k_thread *t,void *s,size_t n,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int p,int q,int r){(void)t;(void)s;(void)n;(void)fn;(void)a;(void)b;(void)c;(void)p;(void)q;(void)r;}
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
static struct lp_checkpoint stored;static unsigned saves;static uint64_t save_delay_us;static bool storage_valid=true,save_fails,storage_qualified;
bool lp_store_load(struct lp_store *s,uint64_t f,struct lp_checkpoint *out){if(!storage_valid)return false;*s=(struct lp_store){.factory=f,.sequence=1,.ready=storage_qualified?(LP_STORE_ACTIVE|LP_STORE_INITIAL_QUIET):LP_STORE_ACTIVE};*out=stored;return true;}
bool lp_store_save(void *p,const struct lp_checkpoint *s){struct lp_store *store=p;if(save_fails)return false;stored=*s;++saves;++store->sequence;store->ready=LP_STORE_ACTIVE;now_us+=save_delay_us;return true;}
bool lp_store_prepare(struct lp_store *s){return s->ready;}
static unsigned txs,rearms,stops;static uint8_t sent[128];static size_t sent_len;static uint32_t sent_generation;
static bool have_event;static struct r2_lora_hw_event queued;
static bool capture_valid=true;static unsigned tuning_phy,tuning_channel,cancel_on;
bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){.tick=now_us*16,.epoch=7,.kind=R2_CAPTURE_NOW};return capture_valid;}
bool r2_capture_now_at(struct r2_capture *c,uint64_t *us){*us=now_us;return r2_capture_now(c);}
int r2_lora_hw_start(uint32_t g,bool rx){(void)g;assert(!rx);if(cancel_on==1){cancel_on=0;r2_lora_source_withdraw();return -ECANCELED;}return 0;}
uint32_t r2_lora_hw_budget_us(size_t n){for(unsigned k=LP_DISC;k<=LP_FOLLOW;k++)if(lp_size(k)==n)return lp_duration(k);return 0;}
int r2_lora_hw_tx_before(const uint8_t *p,size_t n,uint32_t g,uint64_t before){assert(now_us+r2_lora_hw_budget_us(n)<=before);++txs;sent_len=n;memcpy(sent,p,n);sent_generation=g;return 0;}
int r2_lora_hw_take(struct r2_lora_hw_event *e){if(!have_event)return 0;*e=queued;have_event=false;return 1;}
bool r2_lora_hw_tx_edge(struct r2_lora_hw_edge *e){if(!have_event||queued.kind!=R2_LORA_HW_TX_DONE)return false;*e=(struct r2_lora_hw_edge){queued.tick,queued.capture_epoch,queued.generation};return true;}
int r2_lora_hw_idle(void){return 0;}
int r2_lora_hw_passive_configure(enum r2_lora_passive_phy p,unsigned c){tuning_phy=p;tuning_channel=c;if(cancel_on==2){cancel_on=0;r2_lora_source_withdraw();return -ECANCELED;}return 0;}
int r2_lora_hw_rx_rearm(void){++rearms;return 0;}
void r2_lora_hw_stop_begin(uint64_t d){assert(d);++stops;}
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *a){*a=(struct r2_lora_hw_ack){true,true,true,true,false};}
void r2_lora_hw_diag(struct r2_lora_hw_diag *h){memset(h,0,sizeof *h);}
void r2_lora_hw_watchdog_trip(int e){(void)e;r2_diag_scratch.lora.terminal=1;}
static uint64_t charged_unlocked(const struct lp_checkpoint *s,uint64_t now){assert(!lock_depth);return lp_charged(s,now);}
#define lp_charged charged_unlocked
#include R2_PASSIVE_SERVICE_BODY
#undef lp_charged
static const struct r2_group_config test_group={.id=11,.epoch=1,.psk={1,2,3}};
static void step(void){service_step();now_us+=2000;}
static struct rc8_export source(void){return(struct rc8_export){.scope={.emitter=1,.emitter_boot=22,.stream=1,.root=1,.root_boot=22,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_OWN_GM},.from_tick=0,.until_tick=UINT64_MAX,.anchor_tick=0,.anchor_ns=UINT64_C(1000000000000),.capture_epoch=7,.created_ns=0,.expires_ns=UINT64_MAX,.map_generation=1,.rate_ppb=0,.mode=RC8_MAP_LINEAR,.authenticated=true,.valid=true};}
static void init(bool tx){memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);stored=(struct lp_checkpoint){.seed=0x51a7c003};r2_lora_init(tx?1:2,22,1,&test_group);assert(!r2_diag_scratch.lora.drain_late&&!r2_diag_scratch.lora.origin.active&&!r2_diag_scratch.lora.monitor);r2_lora_control(false,true,RC8_SELECT_NONE);}
static void done(uint64_t start,unsigned kind){now_us=start+lp_duration(kind)-10000;queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;step();}
static void packet(const struct lp_packet *p,const uint8_t ctx[16],uint64_t event,uint64_t completion){
 now_us=completion;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,ctx,&test_group));have_event=true;step();
}
static void tx_case(unsigned mode){
 if(mode==2)storage_valid=false;if(mode==16)storage_qualified=true;
 init(true);step();assert(!owner.active&&!txs);r2_lora_request(true);step();assert(!owner.active&&!txs);
 struct rc8_export m=source();m.scope.root_boot=44;r2_lora_source_publish(&m);step();assert(owner.active);
 if(mode==16){assert(restore()&&owner.quiet_until_us<=now_us&&!(role.tx.store.ready&LP_STORE_INITIAL_QUIET));return;}
 if(mode==1){now_us=owner.quiet_until_us-10000;transmit(now_us);assert(!txs);return;}
 now_us=owner.quiet_until_us+10000;r2_lora_request(true);
 if(mode==2){step();assert(!txs&&owner.storage_fail);return;}
 if(mode==3){m.scope.kind=RC8_SELECTED_BLE;m.scope.parent=3;r2_lora_source_publish(&m);step();assert(!txs);return;}
 assert(restore());
 if(mode==8){save_delay_us=150000;now_us=role.tx.core.durable.next_slot_us-250000;step();assert(!txs&&owner.stage==3&&stored.pending==1);now_us=role.tx.reservation.slot_us;}
 else now_us=role.tx.core.durable.next_slot_us;
 step();assert(txs==1&&sent_len==LP_META_BYTES);
 uint64_t slot=role.tx.reservation.slot_us;assert(stored.pending==1&&saves>=2);
 done(slot,LP_META);assert(owner.stage==1);
 if(mode==4||mode==9){if(mode==9)save_delay_us=500000;r2_lora_source_withdraw();step();assert(stored.fh_disabled&&!stored.pending&&txs==1&&stops&&!atomic_get(&faulted)&&r2_diag_scratch.lora.drain_ack_us<r2_diag_scratch.lora.drain_deadline_us);return;}
 now_us=slot+2000000;step();assert(sent_len==LP_SYNC_BYTES&&tuning_phy==R2_LORA_FHSS);
 done(slot+2000000,LP_SYNC);assert(owner.stage==2&&stored.pending==2&&stored.cursor==1);
 if(mode==5){now_us=owner.last_sync_us+301000;step();assert(stored.fh_disabled&&!stored.pending&&txs==2);return;}
 now_us=owner.last_sync_us+260000;uint64_t follow=now_us;step();assert(sent_len==LP_FOLLOW_BYTES);
 done(follow,LP_FOLLOW);assert(!stored.pending&&stored.cursor==2&&!stored.fh_disabled&&txs==3);
 if(mode==6){r2_lora_request(false);step();step();assert(atomic_get(&closed)&&!owner.active&&stops);return;}
 if(mode==8){now_us=slot+3750000;step();now_us=slot+4000000;}else now_us=slot+4000000;step();assert(sent_len==LP_DISC_BYTES&&role.tx.reservation.control==LP_DISC);
}
static void rx_case(unsigned mode){
 init(false);r2_lora_request(true);step();assert(owner.active&&owner.listening&&tuning_phy==R2_LORA_DTS&&!txs);
 struct lp_scope scope={.emitter=1,.boot=99,.root=1,.root_boot=101,.epoch=5,.stream=1,.seed=0x51a7c003};uint8_t context[16];assert(!lp_context(context,&scope,LP_FAST,&test_group));
 uint64_t base=now_us+1000000;
 for(unsigned i=0;i<65;i++){
  uint64_t slot=base+i*LP_BEAT_US;struct lp_packet p={.kind=i%3?LP_DISC:LP_META,.schedule=LP_FAST,.scope=scope,.serial=i/3+1,.cursor=2*i,.time_valid=true,.has_pair=true,.utc_ns=UINT64_C(1000000000000)+slot*1000,.tz=r2_tz_unknown()};
  p.delay_10ms=(uint16_t)((2000000-(lp_duration(p.kind)-10000)+9999)/10000);
  now_us=slot-150000;step();assert(tuning_phy==R2_LORA_DTS);
  packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);assert(role.rx.hint);
  now_us=slot+1900000;step();assert(tuning_phy==R2_LORA_FHSS);
  p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_FAST,.cursor=2*i};
  packet(&p,context,slot+2075000,slot+2223000);assert(role.rx.pending);
  assert(tuning_channel==lp_channel(role.rx.order,2*i+1));
  p=(struct lp_packet){.kind=LP_FOLLOW,.schedule=LP_FAST,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+(slot+2000000)*1000};
  packet(&p,context,slot+2560000,slot+2745000);assert(role.rx.accepted==i+1&&tuning_phy==R2_LORA_DTS&&!txs);
 }
 assert(role.rx.trained&&role.rx.fit_count<=16&&role.rx.accepted==65&&!role.rx.bounded);
 report(now_us);assert(published.choice==RC8_SELECT_LORA&&published.assurance==RC8_FRESHNESS_UNKNOWN);
 if(mode==11){witness_enabled=true;r2_lora_shadow_publish();assert(witness_published==1&&witness.lora&&witness.root_boot==101&&witness.parent_boot==99);r2_lora_shadow_publish();assert(witness_published==1);return;}
 if(mode==12){now_us=role.rx.physical_deadline_us;step();assert(!role.rx.have_pair&&!published.time_valid&&!published.utc_us);return;}
 if(mode==13){now_us=role.rx.observed_deadline_us;step();assert(!role.rx.observed_deadline_us||!role.rx.have_pair);return;}
 if(mode==14){r2_lora_control(false,true,RC8_SELECT_BLE);step();assert(published.choice!=RC8_SELECT_LORA&&!txs);return;}
 r2_lora_request(false);step();step();assert(atomic_get(&closed)&&!published.utc_us&&!txs);
}
static void startup_cancel_case(unsigned mode){
 init(true);r2_lora_request(true);struct rc8_export m=source();r2_lora_source_publish(&m);
 cancel_on=mode==17?1:2;step();assert(owner.stopping&&!atomic_get(&faulted)&&stops&&!txs);
 step();assert(atomic_get(&closed)&&!owner.active&&!r2_diag_scratch.lora.terminal);
 r2_lora_source_publish(&m);step();assert(owner.active&&!owner.stopping&&!atomic_get(&faulted)&&!txs);
}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);if(mode==17||mode==18)startup_cancel_case(mode);else if(mode<10||mode==16)tx_case(mode);else rx_case(mode);
 char json[3072];int n=r2_lora_json(json,sizeof json,0);assert(n>0&&n<2976);printf("PASS owner %u\nJSON %s\n",mode,json);return 0;}
