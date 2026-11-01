/* MIT. Copyright (c) 2026 Jonathan Yantis. Synthetic boundaries only. */
#define main fast_regression_main
#include "lora_passive_service_test.c"
#undef main
#include "../protocol/lora_gm_admission.h"

static void core_checks(unsigned mode){
 struct lp_tx t={0};struct lp_checkpoint s={.seed=17,.schedule=LP_SLOW,.next_slot_us=30000000};struct lp_reservation r;
 struct lp_store store={.ready=LP_STORE_ACTIVE};
 assert(sizeof(struct lp_checkpoint)==536);
 if(mode==101){s.schedule=0;s.buckets[0]=269431859;
  assert(lp_checkpoint_carry(&s,1000000,30000000,1000));
  assert(lp_charged(&s,LP_HOUR_US)==269431859&&s.schedule==LP_SLOW);
  assert(lp_tx_restore(&t,&s,lp_store_save,&store,true));
  assert(!lp_tx_reserve(&t,30000000,&r));
  s.schedule=3;assert(!lp_tx_restore(&t,&s,lp_store_save,&store,true));
  s.schedule=LP_SLOW;s.owed=4;assert(!lp_tx_restore(&t,&s,lp_store_save,&store,true));return;
 }
 assert(lp_tx_restore(&t,&s,lp_store_save,&store,true));
 assert(lp_tx_reserve(&t,30000000,&r)&&r.has_pair&&r.control==LP_META&&t.durable.owed==3);
 uint64_t charged=lp_charged(&t.durable,30000000);
 lp_tx_control_complete(&t);assert(t.durable.owed==LP_OWED_PAIR);
 lp_tx_cancel(&t,true);assert(!t.durable.fh_disabled&&!t.durable.pending&&t.durable.owed==LP_OWED_PAIR);
 assert(lp_tx_reserve(&t,40000000,&r)&&r.control==LP_META&&r.serial==2&&t.durable.slot==1);
 assert(t.durable.next_slot_us==100000000&&lp_charged(&t.durable,40000000)>charged);
 uint64_t end=r.slot_us+LP_SYNC_OFFSET_US+lp_duration(LP_SYNC)-10000;
 assert(lp_tx_capture_sync(&t,end));
 if(mode==102){lp_tx_cancel(&t,true);assert(t.durable.fh_disabled&&!t.durable.pending&&t.durable.owed);return;}
 assert(lp_tx_capture_follow(&t,end+260000+lp_duration(LP_FOLLOW)-10000));
 assert(!t.durable.owed&&!t.durable.pending&&lp_tx_flush(&t));
 assert(!lp_tx_reserve(&t,99999999,&r));
 assert(lp_tx_reserve(&t,100000000,&r)&&r.control==LP_DISC&&t.durable.slot==2);
}

static void receiver_checks(void){
 struct lp_rx r={0};struct lp_scope scope={.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=17};
 struct lp_packet p={.kind=LP_META,.schedule=LP_SLOW,.scope=scope,.serial=1,.utc_ns=UINT64_C(1000000000000),.time_valid=true,.has_pair=true};
 assert(lp_rx_meta(&r,&p,&test_group,NULL,1000000,1000001));
 for(unsigned i=0;i<5;i++){
  uint64_t event=4000000+i*LP_BEAT_US;
  r.hint=true;r.hint_cursor=2*i;r.hint_us=event;
  assert(lp_rx_sync(&r,2*i,event*16,7,event,event));
  struct lp_packet f={.kind=LP_FOLLOW,.schedule=LP_SLOW,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000};
  assert(lp_rx_follow(&r,&f,NULL,7,event+500000));
 }
 assert(r.trained&&r.fit_count==5);
 lp_rx_expire(&r,r.physical_deadline_us);assert(!r.have_pair&&!r.trained&&r.fit_count==5);
 uint64_t event=r.last_us+LP_HOLD_US+100;
 r.hint=true;r.hint_cursor=r.highest;r.hint_us=event;
 assert(lp_rx_sync(&r,r.highest,event*16,7,event,event));
 struct lp_packet f={.kind=LP_FOLLOW,.schedule=LP_SLOW,.cursor=r.highest,.utc_ns=r.last_tx_ns};
 assert(!lp_rx_follow(&r,&f,NULL,7,event+500000));
 r.hint=true;r.hint_cursor=r.highest+2;r.hint_us=event;
 assert(lp_rx_sync(&r,r.highest+2,event*16,8,event,event));
 f.cursor=r.highest+2;f.utc_ns+=LP_HOLD_US*1000+100000;
 assert(lp_rx_follow(&r,&f,NULL,8,event+500000));assert(r.fit_count==1&&!r.trained);
 p.serial=2;p.schedule=LP_FAST;assert(!lp_rx_meta(&r,&p,&test_group,NULL,event,event));
}

static void gm_checks(void){
 struct gm7_owner o={.identity=2,.boot=22,.armed=true};
 o.selected=(struct gm7_control){.sender=1,.active=1,.boot=11,.active_boot=11,.hot=2,.hot_boot=22,.sequence=10,.term=4,.birth_us=2,.founder=1};
 assert(!lp_gm_handoff(&o,1,100,100));
 o.withdrawn=true;assert(!lp_gm_handoff(&o,1,100,100));
 o.peer[0]=(struct gm7_peer){.used=true,.identity=1,.boot=11,.kind=GM7_WITHDRAW,.heard_at=90};
 o.peer[0].received.control=o.selected;o.peer[0].received.control_until=101;
 assert(lp_gm_handoff(&o,1,100,100));assert(!lp_gm_handoff(&o,1,102,102));
 o.withdrawn=false;o.selected.yield_utc_us=1;o.remote_yield_sequence=10;o.remote_yield_after=100;
 assert(lp_gm_handoff(&o,1,100,100));assert(!lp_gm_handoff(&o,1,99,99));
 assert(!lp_gm_readmitted(&o,1,100));o.peer[0].heard=true;o.peer[0].reported.known=1;o.peer[0].reported.until=101;
 assert(!lp_gm_readmitted(&o,1,100));o.scope=(struct gm7_scope){.birth=2,.founder=1,.term=5,.active=2,.boot=22};
 o.peer[0].observed=(struct gm7_lineage){2,1,5,2,22};
 assert(lp_gm_readmitted(&o,1,100));o.peer[0].reported.role=GM7_W_ACTIVE;assert(!lp_gm_readmitted(&o,1,100));
}

static void owner_checks(unsigned mode){
 storage_qualified=true;init(true);step();assert(owner.active&&owner.listening&&!transmitter&&!txs);
 assert(r2_lora_transmitter_quiescent());
 if(mode==109)storage_valid=false;
 if(mode==110)stored.fh_disabled=1;
 r2_lora_gm_admission(true,UINT64_C(10000000000),true,5,7);step();step();assert(transmitter&&owner.active);
 struct rc8_export m=source();r2_lora_source_publish(&m);
 if(mode==109||mode==110){for(unsigned i=0;i<6;i++)step();assert(!transmitter&&owner.active&&owner.listening&&tx_blocked&&!atomic_get(&faulted));return;}
 now_us=owner.quiet_until_us+21000000;assert(restore(now_us+2000000));
 uint64_t first=role.tx.core.durable.next_slot_us;
 if(mode==106){
  // Idle RX cannot overwrite the TX checkpoint union.
  struct lp_checkpoint before=role.tx.core.durable;
  queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_RX_DONE,.tick=now_us*16,.capture_epoch=7,.generation=generation};have_event=true;step();
  assert(!memcmp(&before,&role.tx.core.durable,sizeof before));
  r2_lora_gm_admission(false,0,false,0,0);step();step();assert(!transmitter&&owner.listening);
  r2_lora_gm_admission(true,UINT64_C(10000000000),true,5,7);step();step();r2_lora_source_publish(&m);step();
  assert(transmitter&&role.tx.core.ready&&tx_rebased);return;
 }
 if(mode==107){r2_lora_request(false);step();step();assert(role.tx.core.ready&&role.tx.core.durable.seed==stored.seed);return;}
 if(mode==111){r2_lora_gm_admission(true,UINT64_C(9999999999),true,5,7);assert(!fenced());return;}
 now_us=first-1100000;step();assert(owner.stage==3);
 now_us=first;reject_next_tx=true;step();assert(!txs&&lp_tx_has_deferred(&role.tx.core)&&!stored.fh_disabled);
 for(unsigned i=0;i<4000&&!txs;i++)step();
 assert(txs==1&&sent_len==LP_META_BYTES&&role.tx.reservation.slot_us>first);
 uint64_t retry=role.tx.reservation.slot_us;
 if(mode==112){r2_lora_gm_admission(true,now_us+1,true,5,7);assert(fenced());step();assert(atomic_get(&closed)&&!stored.fh_disabled);return;}
 done(retry,LP_META);now_us=retry+LP_SYNC_OFFSET_US;reject_next_tx=true;step();assert(owner.stage==1&&!stored.fh_disabled);step();assert(txs==2);
 done(retry+LP_SYNC_OFFSET_US+2000,LP_SYNC);now_us=owner.last_sync_us+260000;reject_next_tx=true;step();assert(owner.stage==2&&!stored.fh_disabled);step();assert(txs==3);
 done(now_us-2000,LP_FOLLOW);assert(!role.tx.core.durable.pending&&!role.tx.core.durable.owed);
 assert(role.tx.core.durable.next_slot_us>=retry+LP_BEAT_US);
 char buf[2000];int n=r2_lora_diagnostics_json(buf,sizeof buf,0);assert(n>0&&n<1900);printf("DIAG %s\n",buf);
}
static void replay_after_off(void){
 init(false);step();struct lp_scope scope={.emitter=1,.boot=99,.root=1,.root_boot=101,.epoch=5,.stream=1,.seed=0x51a7c003};uint8_t ctx[16];assert(!lp_context(ctx,&scope,LP_SLOW,&test_group));
 struct lp_packet p={.kind=LP_META,.schedule=LP_SLOW,.scope=scope,.serial=1,.time_valid=true,.has_pair=true,.utc_ns=UINT64_C(1000000000000),.tz=r2_tz_unknown()};
 uint64_t slot=2000000;p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
 packet(&p,ctx,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);assert(role.rx.cached);
 // Unseen META2/cursor10 is followed by a pair received using cached context.
 slot=302000000;p.kind=LP_DISC;p.cursor=10;p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
 packet(&p,ctx,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
 now_us=slot+2900000;step();assert(tuning_phy==R2_LORA_FHSS);
 p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_SLOW,.cursor=10};
 packet(&p,ctx,slot+3075000,slot+3223000);
 p.kind=LP_FOLLOW;p.utc_ns=UINT64_C(1000000000000)+(slot+3000000)*1000;
 packet(&p,ctx,slot+3560000,slot+3745000);assert(role.rx.accepted==1&&replay[0].cursor==12);
 r2_lora_request(false);step();step();r2_lora_request(true);step();step();assert(!role.rx.cached);
 p=(struct lp_packet){.kind=LP_META,.schedule=LP_SLOW,.scope=scope,.serial=2,.cursor=10,.utc_ns=UINT64_C(1302000000000),.time_valid=true,.has_pair=true,.delay_10ms=187,.tz=r2_tz_unknown()};
 slot=310000000;packet(&p,ctx,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
 assert(!role.rx.cached&&!role.rx.accepted&&owner.bad);
}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 if(mode<=103)core_checks(mode);else if(mode==104)receiver_checks();else if(mode==105)gm_checks();else if(mode==114)replay_after_off();else owner_checks(mode);
 printf("PASS slow %u\n",mode);return 0;
}
