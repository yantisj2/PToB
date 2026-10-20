/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual service body; fake only kernel/capture/hardware boundaries. */
#include "lora_service.h"
#include "lora_hw.h"
#include "capture.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef long atomic_t;typedef long atomic_val_t;
static atomic_val_t atomic_get(const atomic_t *p){return *p;}
static void atomic_set(atomic_t *p,long v){*p=v;}
static bool atomic_cas(atomic_t *p,long a,long b){if(*p!=a)return false;*p=b;return true;}
static long atomic_and(atomic_t *p,long v){long old=*p;*p&=v;return old;}
static void atomic_or(atomic_t *p,long v){*p|=v;}
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *p){(void)p;return 0;}
static void k_spin_unlock(struct k_spinlock *p,int k){(void)p;(void)k;}
struct k_thread{int unused;};
#define K_THREAD_STACK_DEFINE(n,s) unsigned char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_MSEC(n) n
static void k_thread_create(struct k_thread *t,void *s,size_t n,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int p,int q,int r){(void)t;(void)s;(void)n;(void)fn;(void)a;(void)b;(void)c;(void)p;(void)q;(void)r;}
static void k_thread_name_set(struct k_thread *t,const char *n){(void)t;(void)n;}
static uint64_t now_us=1000000;
static uint64_t k_uptime_ticks(void){return now_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
static void k_sleep(int n){now_us+=(unsigned)n*1000;}
static unsigned starts,txs,rearms,stops;static bool have_event,capture_valid=true,cancel_start,cancel_tx,cancel_take,hold_ack;
static bool late_diagnostic,quality_during_spi,change_during_spi,withdraw_on_capture;static unsigned capture_calls;
static void spi_quality_update(void);
static void spi_context_update(void);
static int start_result,take_result;static struct r2_lora_hw_event queued;
static uint8_t sent[128];static size_t sent_len;static uint32_t sent_generation;
bool r2_capture_now(struct r2_capture *c){if(withdraw_on_capture&&++capture_calls==2){withdraw_on_capture=false;r2_lora_source_withdraw();}*c=(struct r2_capture){now_us*16,7,R2_CAPTURE_NOW};return capture_valid;}
bool r2_capture_now_at(struct r2_capture *c,uint64_t *us){*us=now_us;return r2_capture_now(c);}
int r2_lora_hw_start(uint32_t g,bool receive){(void)g;(void)receive;starts++;if(cancel_start){cancel_start=false;r2_lora_request(false);return -ECANCELED;}return start_result;}
uint32_t r2_lora_hw_budget_us(size_t n){return n==80?360000:n==128?450000:0;}
int r2_lora_hw_tx_before(const uint8_t *p,size_t n,uint32_t g,uint64_t before){assert(before==UINT64_MAX||now_us+r2_lora_hw_budget_us(n)<=before);if(late_diagnostic&&n==128)return -EAGAIN;if(quality_during_spi&&n==128)spi_quality_update();if(change_during_spi&&n==128){change_during_spi=false;spi_context_update();}txs++;sent_len=n;memcpy(sent,p,n);sent_generation=g;if(cancel_tx){cancel_tx=false;r2_lora_request(false);return -ECANCELED;}return 0;}
int r2_lora_hw_take(struct r2_lora_hw_event *e){if(cancel_take){cancel_take=false;r2_lora_request(false);return -ECANCELED;}if(take_result)return take_result;if(!have_event)return 0;*e=queued;have_event=false;return 1;}
int r2_lora_hw_rx_rearm(void){rearms++;return 0;}
void r2_lora_hw_stop_begin(uint64_t deadline){assert(deadline>0);stops++;}
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *a){*a=(struct r2_lora_hw_ack){!hold_ack,true,true,true,false};}
void r2_lora_hw_diag(struct r2_lora_hw_diag *d){memset(d,0,sizeof *d);}
#include R2_LORA_SERVICE_BODY
static void spi_quality_update(void){now_us+=2000;struct rc8_quality q=published.quality;r2_lora_quality_publish(&published.scope,&q);}
static const struct r2_group_config test_group={.id=11,.epoch=1,.psk={1,2,3}};
static void step(void){service_step();now_us+=2000;}
static void init(bool tx){r2_lora_init(tx?1:2,22,1,&test_group);r2_lora_control(false,true,false);}
static void done(void){queued=(struct r2_lora_hw_event){.tick=now_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE};have_event=true;step();}
static struct rc8_export source(uint64_t from,uint64_t gen){return(struct rc8_export){.scope={.emitter=1,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE},.from_tick=from,.until_tick=from+24000000,.anchor_tick=from,.anchor_ns=10000000000ULL+from*125/2,.capture_epoch=7,.created_ns=0,.expires_ns=100000000000ULL,.map_generation=(uint32_t)gen,.upstream_bound_ns=RC8_UNKNOWN,.authenticated=true,.valid=true};}
static void spi_context_update(void){now_us+=2000;struct rc8_export a=source(now_us*16,2);a.scope.root=4;r2_lora_source_publish(&a);}
static void packet(const uint8_t *wire,size_t n,uint64_t at){now_us=at;queued=(struct r2_lora_hw_event){.tick=at*16,.capture_epoch=7,.local_us=at+700,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)n};memcpy(queued.frame,wire,n);have_event=true;step();}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 if(mode==1){init(false);start_result=-EAGAIN;step();assert(!atomic_get(&faulted)&&atomic_get(&closed));start_result=0;step();assert(owner.active&&starts==2);}
 else if(mode==2){init(false);cancel_start=true;step();assert(owner.stopping&&!role.rx.enabled&&!atomic_get(&faulted));step();assert(!r2_lora_close_result());r2_lora_request(true);step();assert(owner.active&&role.rx.enabled&&starts==2);}
 else if(mode==3){init(true);struct rc8_export m=source(now_us*16,1);r2_lora_source_publish(&m);step();cancel_tx=true;step();assert(owner.stopping&&!atomic_get(&faulted));step();r2_lora_request(true);step();assert(owner.active&&!atomic_get(&faulted));}
 else if(mode==4){init(false);step();take_result=-EIO;step();assert(atomic_get(&faulted)&&!role.rx.enabled);step();assert(atomic_get(&closed));r2_lora_control(false,true,true);step();struct r2_lora_view v;r2_lora_view(&v);assert(v.choice==RC8_SELECT_BLE&&v.fault);}
 else if(mode==5){init(true);r2_lora_control(true,true,false);step();assert(!starts&&!txs);r2_lora_control(false,true,false);step();assert(starts==1);step();assert(txs==1&&sent_len==128);}
 else if(mode==6){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();assert(sent_len==80);done();assert(sent_len==128);done();assert(published.correction.map_generation==1&&published.corrected_sequence==1);}
 else if(mode==7){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();r2_lora_source_withdraw();done();assert(published.missing_map==1&&!role.tx.previous_valid&&sent_len==128);struct rc8_companion p;uint8_t id[16];assert(!rc8_companion_decode(&p,id,sent,sent_len,&test_group)&&!p.time_valid&&(p.events&RC8_D_LOSS));}
 else if(mode==8){init(false);step();uint8_t w[128];struct rc8_scope s={.emitter=1,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};struct rc8_packet p={.sequence=1,.root=s.root,.source_epoch=s.source_epoch,.scale=s.scale,.kind=s.kind};assert(!rc8_context_id(p.context,&s,&test_group));assert(!rc8_encode(w,&p,&test_group));packet(w,80,1200000);p.sequence=2;p.corrected=true;p.correction=(struct rc8_correction){.sequence=1,.event_ns=9000000000ULL,.expires_ns=9100000000ULL,.map_generation=9,.upstream_bound_ns=RC8_UNKNOWN};assert(!rc8_encode(w,&p,&test_group));packet(w,80,5200000);struct r2_lora_view v;r2_lora_view(&v);assert(v.choice==RC8_SELECT_LORA&&v.assurance==RC8_FRESHNESS_UNKNOWN&&v.utc_us&&published.event.local_us==1200000&&!role.rx.metadata_known&&!role.rx.scope.root_boot);assert(!rc8_metadata_encode(w,&s,&test_group));packet(w,128,5300000);assert(role.rx.metadata_known&&role.rx.scope.root_boot==44&&role.rx.highest_sequence==2&&role.rx.sample.rx.local_us==1200000);assert(!txs&&rearms==3);r2_lora_control(false,true,true);step();r2_lora_view(&v);assert(v.choice==RC8_SELECT_BLE);r2_lora_request(false);r2_lora_view(&v);assert(!v.utc_us);step();assert(!role.rx.enabled);step();r2_lora_request(true);step();assert(!role.rx.sample.valid);}
 else if(mode==9){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();uint64_t stream=role.tx.stream;a.scope.root=4;a.from_tick=now_us*16;a.anchor_tick=a.from_tick;r2_lora_source_publish(&a);assert(role.tx.stream==stream+1&&!role.tx.previous_valid);done();assert(published.missing_map==1);}
 else if(mode==10){init(false);step();r2_lora_request(false);uint64_t deadline=stop_deadline;r2_lora_stop();assert(stop_deadline==deadline);step();assert(!r2_lora_close_result());assert(!txs);}
 else if(mode==11){init(false);step();r2_lora_request(false);hold_ack=true;step();now_us=stop_deadline;step();assert(r2_lora_close_result()==-EIO&&(atomic_get(&faulted)&2));hold_ack=false;step();assert(atomic_get(&closed)&&r2_lora_close_result()==-EIO);}
 else if(mode==12){init(false);step();cancel_take=true;step();assert(owner.stopping&&!role.rx.enabled&&!atomic_get(&faulted));step();r2_lora_request(true);step();assert(owner.active&&!atomic_get(&faulted));}
 else if(mode==13){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();done();role.tx.sequence=UINT32_MAX;role.tx.next_us=0;done();assert(atomic_get(&faulted)&&owner.stopping);}
 else if(mode==14){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);a.scope.source_epoch=4;r2_lora_source_publish(&a);assert(atomic_get(&faulted));}
 else if(mode==15){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);role.tx.stream=UINT64_MAX;a.scope.root=4;r2_lora_source_publish(&a);assert(atomic_get(&faulted));}
 else if(mode==16){init(true);struct rc8_export a=source(now_us*16-1600,1);r2_lora_source_publish(&a);assert(role.tx.maps[0].from_tick==now_us*16);assert(role.tx.maps[0].anchor_tick==now_us*16-1600);struct rc8_export b;assert(!current_map(&b,now_us*16-1,7,0));assert(current_map(&b,now_us*16,7,0));uint64_t expiry=role.tx.maps[0].expires_ns;now_us+=1000;a.expires_ns+=1000000000;r2_lora_source_publish(&a);assert(role.tx.maps[0].expires_ns==expiry);}
 else if(mode==17){r2_lora_init(0,22,1,&test_group);r2_lora_control(false,true,false);r2_lora_request(true);step();assert(!starts&&!txs);}
 else if(mode==18){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();done();done();role.tx.meta_us=now_us;role.tx.diag_not_before=0;role.tx.next_us=now_us+100000;unsigned before=txs;step();assert(txs==before);role.tx.next_us=now_us;step();assert(txs==before+1&&sent_len==80);}
 else if(mode==19){init(false);r2_lora_control(false,true,RC8_SELECT_LOCAL_GM);step();struct r2_lora_view v;r2_lora_view(&v);assert(v.choice==RC8_SELECT_LOCAL_GM&&!txs);}
 else if(mode==20){init(true);struct rc8_export a=source(now_us*16,1);a.expires_ns=a.anchor_ns;r2_lora_source_publish(&a);step();step();assert(txs==1&&sent_len==128&&published.view.state==RC8_ACQUIRING);struct rc8_companion p;uint8_t id[16];assert(!rc8_companion_decode(&p,id,sent,sent_len,&test_group)&&!p.time_valid);}
 else if(mode==21){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();late_diagnostic=true;done();uint32_t seq=role.tx.sequence;uint64_t next=role.tx.next_us;assert(!role.tx.pending&&role.tx.previous_valid&&published.metadata==0);step();assert(role.tx.sequence==seq&&role.tx.next_us==next&&!atomic_get(&faulted));late_diagnostic=false;step();assert(role.tx.pending&&sent_len==128&&published.metadata==1);}
 else if(mode==22){init(true);struct rc8_export a=source(now_us*16,1);a.scope.parent=3;a.scope.parent_boot=44;r2_lora_source_publish(&a);struct rc8_quality q={.known=RC8_Q_LOCK|RC8_Q_ROLE,.values=RC8_Q_LOCK,.role=2};r2_lora_quality_publish(&a.scope,&q);r2_lora_parent_quality(4,44,6,1,now_us);assert(!(published.quality.known&RC8_Q_PARENT_CLASS));r2_lora_parent_quality(3,44,6,1,now_us);assert(published.quality.parent_class==6);uint64_t stream=role.tx.stream;uint16_t events=published.diagnostic_events;r2_lora_quality_publish(&a.scope,&q);assert(role.tx.stream==stream&&published.diagnostic_events==events);step();step();done();struct rc8_companion d;uint8_t id[16];assert(!rc8_companion_decode(&d,id,sent,sent_len,&test_group));assert(d.quality.parent_class==6&&!(d.quality.known&(RC8_Q_PARENT_PPS|RC8_Q_PARENT_GNSS)));done();r2_lora_request(false);step();unsigned before=txs;r2_lora_source_withdraw();step();assert(txs==before);}
 else if(mode==23){init(true);struct rc8_export a=source(now_us*16,1);a.scope.parent=3;a.scope.parent_boot=44;r2_lora_source_publish(&a);step();step();done();done();assert(role.tx.previous_valid);uint64_t stream=role.tx.stream;a.scope.parent_boot++;a.from_tick=now_us*16;a.anchor_tick=a.from_tick;r2_lora_source_publish(&a);assert(role.tx.stream==stream+1&&!role.tx.previous_valid&&(published.diagnostic_events&RC8_D_PARENT));}
 else if(mode==24){init(false);step();uint8_t w[80];struct rc8_packet p={.sequence=1,.root=3,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};struct rc8_scope c=source(now_us*16,1).scope;assert(!rc8_context_id(p.context,&c,&test_group));assert(!rc8_encode(w,&p,&test_group));now_us=9000000;queued=(struct r2_lora_hw_event){.tick=3000000*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=80};memcpy(queued.frame,w,80);have_event=true;step();assert(role.rx.highest_sequence==1&&role.rx.history[0].rx.local_us==3000000&&rearms==1&&!atomic_get(&faulted));now_us=18000000;queued.tick=9000000*16;have_event=true;step();assert(role.rx.highest_sequence==1&&rearms==2&&role.rx.rejected==1&&!atomic_get(&faulted));queued.tick=0;queued.capture_epoch=0;have_event=true;step();assert(rearms==3&&role.rx.rejected==2&&!atomic_get(&faulted));}
 else if(mode==25){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);struct rc8_quality q={.known=RC8_Q_LOCK|RC8_Q_ROLE,.values=RC8_Q_LOCK,.role=2};r2_lora_quality_publish(&a.scope,&q);step();step();quality_during_spi=true;done();assert(sent_len==128&&published.diagnostic_events==0&&published.metadata==1);done();role.tx.diag_not_before=0;unsigned before=txs;step();assert(txs==before&&role.tx.meta_us>now_us+100000000);}
 else if(mode==26){init(true);struct rc8_export a=source(now_us*16,1);a.scope.kind=RC8_OWN_GM;a.scope.root=1;r2_lora_source_publish(&a);struct rc8_quality q={.known=RC8_Q_ROLE|RC8_Q_HOP,.values=RC8_Q_LOCK,.role=1};r2_lora_quality_publish(&a.scope,&q);assert(!published.quality.values);step();step();done();assert(sent_len==128&&!atomic_get(&faulted));struct rc8_companion d;uint8_t id[16];assert(!rc8_companion_decode(&d,id,sent,sent_len,&test_group)&&d.quality.role==1&&!d.quality.values);struct rc8_scope wrong=a.scope;wrong.root_boot++;q.role=2;r2_lora_quality_publish(&wrong,&q);assert(published.quality.role==1);wrong=a.scope;wrong.source_epoch++;r2_lora_quality_publish(&wrong,&q);assert(published.quality.role==1);}
 else if(mode==27){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();done();done();assert(role.tx.repeat_us&&published.metadata==1);now_us=role.tx.repeat_us;a=source(now_us*16,2);r2_lora_source_publish(&a);step();assert(sent_len==80);done();assert(sent_len==128&&published.metadata==2&&!role.tx.repeat_us);done();unsigned before=txs;role.tx.next_us=now_us+1000000;step();assert(txs==before&&published.metadata==2);}
 else if(mode==28){init(true);step();step();assert(txs==1&&sent_len==128);struct rc8_companion d;uint8_t id[16];assert(!rc8_companion_decode(&d,id,sent,sent_len,&test_group));assert(d.scope.emitter==1&&d.scope.emitter_boot==22&&d.scope.stream==1&&!d.scope.root&&!d.time_valid&&d.scope.kind==RC8_NO_SOURCE);done();assert(!role.tx.previous_valid);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);assert(role.tx.stream==2&&published.scope.stream==2);}
 else if(mode==29){init(true);struct rc8_export a=source(now_us*16,1);a.scope.parent=3;a.scope.parent_boot=44;r2_lora_source_publish(&a);uint64_t seen=now_us;r2_lora_parent_quality(3,44,6,1,seen);struct rc8_quality q={.known=RC8_Q_LOCK|RC8_Q_ROLE,.values=RC8_Q_LOCK,.role=2};for(unsigned i=0;i<3;i++){now_us+=1000000;r2_lora_quality_publish(&a.scope,&q);r2_lora_parent_quality(3,44,6,1,seen);}step();step();struct rc8_companion d;uint8_t id[16];assert(!rc8_companion_decode(&d,id,sent,sent_len,&test_group));assert(sent_len==128&&d.snapshot_age_ms>=3000&&(d.quality.known&RC8_Q_PARENT_CLASS));done();now_us=seen+10000001;r2_lora_quality_publish(&a.scope,&q);step();assert(!(published.quality.known&RC8_Q_PARENT_CLASS));r2_lora_parent_quality(3,44,6,1,seen);assert(!(published.quality.known&RC8_Q_PARENT_CLASS));}
 else if(mode==30){init(true);struct rc8_export a=source(now_us*16,1);a.scope.parent=3;a.scope.parent_boot=44;r2_lora_source_publish(&a);r2_lora_parent_quality(3,44,6,1,now_us);report(now_us-2000,true);assert(published.quality.known&RC8_Q_PARENT_CLASS);}
 else if(mode==31){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();capture_calls=0;withdraw_on_capture=true;step();assert(!txs&&!role.tx.sequence&&!role.tx.next_us&&!role.tx.pending&&!atomic_get(&faulted));step();assert(txs==1&&sent_len==128);}
 else if(mode==32){init(true);struct rc8_export a=source(now_us*16,1);r2_lora_source_publish(&a);step();step();done();done();now_us=role.tx.repeat_us;a=source(now_us*16,2);r2_lora_source_publish(&a);step();change_during_spi=true;done();assert(sent_len==128&&role.tx.repeat_us>now_us&&published.diagnostic_events&&(published.diagnostic_events&RC8_D_ROOT));}
 else assert(false);
 char json[3072];int n=r2_lora_json(json,sizeof json,0);assert(n>0&&n<(int)sizeof json);printf("PASS runtime mode%u role=%zu snapshot=%zu worker=%zu json=%d\n",mode,sizeof role,sizeof published,sizeof worker_stack,n);
}
