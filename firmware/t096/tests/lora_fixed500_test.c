/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual owner/core with synthetic hardware boundaries; no hardware. */
#include "lora_ram_fixture.h"
static void rx(const struct lp_packet *p,uint64_t event,uint64_t completed){
 now_us=completed;
 queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,
  .kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,role.rx.context,&test_group));have_event=true;step();
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 assert(LP_SCHEDULE==8&&LP_CAP_US==90000000&&LP_DISC_BEATS==0);
 if(mode==9){
  struct lp_progress_watch w={0};uint32_t at=UINT32_MAX-100000;
  assert(!lp_progress_poll(&w,at,true,false,false,0)); /* RX/source silence is not a fault. */
  assert(!lp_progress_poll(&w,at,true,true,false,0));
  assert(!lp_progress_poll(&w,at+299999,true,true,false,0));
  assert(lp_progress_poll(&w,at+300000,true,true,false,0));
  assert(!lp_progress_poll(&w,at+300001,false,true,false,0)); /* Maintenance wins. */
  assert(!lp_progress_poll(&w,1000,true,false,true,5));
  assert(!lp_progress_poll(&w,200000,true,false,true,6)); /* Real TX progress restarts the timer. */
  assert(!lp_progress_poll(&w,499999,true,false,true,6));
  assert(lp_progress_poll(&w,500000,true,false,true,6));
  assert(!lp_progress_poll(&w,500001,true,false,false,6)); /* Source/cap loss disables TX timer. */
  puts("PASS deferred300s declaredfault/eligibleTX timer, progress/source/maintenance and uptimewrap");return 0;
 }
 if(mode==1){
  const uint64_t phases[]={0,1,49999,50000,14999999,15000000,29999999,45000000,59999999};
  for(unsigned phase=0;phase<sizeof phases/sizeof phases[0];phase++){
   uint64_t first=UINT64_C(3600000000)+phases[phase],maximum=0;
   struct lp_tx t;struct lp_reservation r;assert(lp_tx_start(&t,17,first,first,(uint32_t)(first/1000)));
   for(unsigned i=0;i<5760;i++){
    uint64_t at=first+(uint64_t)i*LP_BEAT_US;
    assert(lp_tx_reserve(&t,at,&r)&&r.has_pair&&r.control==(i%10?0:LP_META));
    assert(lp_channel(t.order,r.cursor)==50&&lp_channel(t.order,r.cursor+1)==50);
    uint64_t charge=lp_charged(&t.durable,at);assert(charge<=LP_CAP_US);if(charge>maximum)maximum=charge;
    if(r.control)lp_tx_control_complete(&t);
    uint64_t sync=at+LP_SYNC_OFFSET_US+263168;
    assert(lp_tx_capture_sync(&t,sync)&&lp_tx_capture_follow(&t,sync+LP_FOLLOW_OFFSET_US+295936));
    if(i==119)assert(charge==79061952);
   }
   printf("PASS48h phase%llu maximum%llu\n",(unsigned long long)phases[phase],(unsigned long long)maximum);
  }
  return 0;
 }
 if(mode==2){
  struct lp_tx t;struct lp_reservation r;assert(lp_tx_start(&t,17,1000000,1000000,1000));
  assert(lp_tx_reserve_control(&t,1000000,&r,true)&&r.control==LP_META);lp_tx_cancel(&t,true);
  assert(!lp_tx_reserve_control(&t,4000000,&r,true));
  assert(!lp_tx_reserve_utc(&t,4000000,&r,true));
  assert(!lp_tx_reserve_control(&t,31000000,&r,true));
  assert(lp_tx_reserve_control(&t,31000000,&r,false)&&!r.control);
  lp_tx_cancel(&t,true);
  assert(lp_tx_reserve_control(&t,301000000,&r,true)&&r.control==LP_META);
  struct lp_tx capped;assert(lp_tx_start(&capped,17,1000000,1000000,1000));
  capped.durable.buckets[0]=LP_CAP_US-lp_duration(LP_META);
  assert(lp_tx_reserve(&capped,1000000,&r)&&r.control==LP_META&&!r.has_pair);
  assert(!lp_tx_rearm(&capped,2000000,5000000)); /* Pending work cannot be rearmed. */
  lp_tx_cancel(&capped,true);struct lp_checkpoint debt=capped.durable;
  assert(lp_tx_rearm(&capped,2000000,5000000));
  assert(capped.durable.minute==debt.minute&&capped.durable.seed==debt.seed&&
   capped.durable.cursor==debt.cursor&&capped.durable.serial==debt.serial&&
   !memcmp(capped.durable.buckets,debt.buckets,sizeof debt.buckets)&&
   !memcmp(capped.durable.channel_end_ms,debt.channel_end_ms,sizeof debt.channel_end_ms));
  assert(!lp_tx_reserve(&capped,5000000,&r)); /* Rearm does not mint cap credit. */
  puts("PASS strictMETA300 no offgrid retry/capoverride; skipped slots retain original phase");return 0;
 }
 if(mode==3||mode==5||mode==6||mode==7){
  init(true);uint64_t next=1337000,finish=now_us+340000000;unsigned publications=0;
  bool withdrew=false,delayed=false,rearmed=false;uint64_t rearmed_at=0,old_charge=0;
  uint32_t old_seed=0,old_serial=0,old_cursor=0;
  while(now_us<finish){
   if(mode==5&&now_us>=120000000&&now_us<122000000){
    if(!withdrew){r2_lora_source_withdraw();withdrew=true;}
   }else
   if(now_us>=next){struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
    if(mode==6&&now_us>=200000000)m.scope.parent_boot=45; /* Same coarse root, different authenticated parent. */
    uint64_t room=1100000;m.from_tick=m.anchor_tick=next*16;m.until_tick=(next+room)*16;
    m.anchor_ns+=next*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;m.mode=RC8_MAP_DELIVERED;m.map_generation=++publications;r2_lora_source_publish(&m);next+=1000000;}
   if(owner.tx_kind&&!have_event){uint32_t duration=r2_lora_hw_budget_us(lp_size(owner.tx_kind));
    if(now_us>=sent_at_us+duration-10000){queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+duration-10000};have_event=true;}}
   if(mode==5&&!delayed&&owner.prepared_kind==LP_META){
    now_us=role.tx.reservation.slot_us+1800000;next=now_us;delayed=true;
   }
   if(mode==7&&!rearmed&&now_us>=120000000&&!owner.tx_kind&&!role.tx.core.durable.pending){
    old_charge=lp_charged(&role.tx.core.durable,now_us);old_seed=role.tx.core.durable.seed;
    old_serial=role.tx.core.durable.serial;old_cursor=role.tx.core.durable.cursor;
    r2_lora_request(false);while(!atomic_get(&closed))step();r2_lora_request(true);rearmed_at=now_us;rearmed=true;
   }
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
   if(mode==7&&rearmed&&owner.sent[1]>=2){
    assert(now_us-rearmed_at<20000000&&role.tx.core.durable.seed==old_seed&&role.tx.core.durable.serial>old_serial&&
     role.tx.core.durable.cursor>=old_cursor&&lp_charged(&role.tx.core.durable,now_us)>=old_charge);break;
   }
  }
  printf("OWNER META%u SYNC%u FOLLOW%u DISC%u\n",owner.sent[1],owner.sent[2],owner.sent[3],owner.sent[0]);
  if(mode==7){assert(rearmed&&owner.sent[1]>=2);puts("PASS explicit OFF/ON freshMETA with seed/cursor/serial/debt retained");return 0;}
  assert(owner.sent[1]==2&&owner.sent[2]==owner.sent[3]&&!owner.sent[0]);
  if(mode==6)assert(role.tx.stream==2&&owner.sent[2]>=7);
  else assert(owner.sent[2]>=10&&role.tx.stream==1);
  if(mode==5)assert(withdrew&&delayed&&!owner.late);
  puts("PASS actual staged owner fixedPHY/noDISC/strictMETA300 and unchangedBLE grants plus explicitLABhold");return 0;
 }
 if(mode==10){
  init(false);step();
  struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=4,.cursor=40,
   .time_valid=true,.has_pair=true,.delay_10ms=600,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
   .scope={.emitter=1,.boot=2,.root=3,.root_boot=44,.parent=3,.parent_boot=44,
    .epoch=5,.stream=1,.seed=17,.kind=2}};
  rx(&m,10000000,10700000);assert(role.rx.cached);
  role.rx.highest=58;role.rx.accepted=53;replay[0].cursor=60;
  m.scope.root=1;m.scope.root_boot=2;m.scope.parent=m.scope.parent_boot=0;
  m.scope.epoch=1;m.scope.stream=3;m.scope.kind=1;
  m.serial=8;m.cursor=106;m.utc_ns+=UINT64_C(900000000000);
  uint64_t header=1000000000;rx(&m,header,header+1025444);
  assert(role.rx.scope.kind==1&&role.rx.scope.epoch==1&&role.rx.scope.root_boot==2&&role.rx.scope.stream==3);
  assert(role.rx.serial==8&&role.rx.highest==106&&replay[0].serial==8);
  assert(role.rx.observed_deadline_us==header-787456+LP_LEASE_US);
  assert(!role.rx.bounded&&!role.rx.meta_deadline_us&&!role.rx.tz_deadline_us);
  uint64_t deadline=role.rx.observed_deadline_us;rx(&m,header+2000000,header+2700000);
  assert(role.rx.observed_deadline_us==deadline&&role.rx.accepted==53); /* Duplicate cannot renew. */
  uint64_t event=header+5000000;
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=106};rx(&sync,event,event+200000);
  assert(role.rx.pending);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=106,.utc_ns=m.utc_ns+UINT64_C(5000000000)};
  rx(&follow,event+2800000,event+3000000);
  assert(role.rx.accepted==54&&!role.rx.trained&&role.rx.fit_count==1&&owner.listening);
  puts("PASS actual HeaderValid META processing1.025444s/newrootepoch/cached replay and backdated lease; freshpair resumes");return 0;
 }
 assert(mode==4||mode==8);init(false);step();assert(owner.listening&&tuning_phy==R2_LORA_FHSS&&tuning_channel==50);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,.has_pair=true,.delay_10ms=600,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
  .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 rx(&m,10000000,10700000);assert(role.rx.cached&&!role.rx.hint);
 assert(role.rx.observed_deadline_us==10000000-787456+LP_LEASE_US);
 for(unsigned i=0;i<10;i++){
  uint64_t event=UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US;
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};rx(&sync,event,event+200000);assert(role.rx.pending);
  if(i==2)continue; /*lostFOLLOW must not break acquisition of the next pair*/
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000};
  rx(&follow,event+2800000,event+3000000);assert(role.rx.accepted==i+1-(i>=2));
  assert(role.rx.last_tx_ns==follow.utc_ns+LP_FIXED_EVENT_DELAY_NS);
  assert(owner.listening&&tuning_phy==R2_LORA_FHSS&&tuning_channel==50);
 }
 if(mode==8){
  assert(role.rx.trained&&role.rx.display_valid);witness_enabled=true;r2_lora_shadow_publish();unsigned initial=witness_published;
  struct rc8_scope ble={.emitter=2,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.parent=3,.parent_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
  uint64_t first=now_us;
  for(unsigned i=0;i<=30;i++){
   now_us=first+(uint64_t)i*1000000;r2_lora_ble_observe(&ble,now_us+1100000,0,now_us);
   r2_lora_control(false,true,RC8_SELECT_BLE);step();
   assert(role.rx.display_valid&&role.rx.trained);
   assert(((atomic_get(&flags)&NORMAL)!=0)==(i==30));
   assert(r2_lora_ble_shadow_allowed()==(i==30));
  }
  r2_lora_ble_observe(NULL,0,0,now_us);r2_lora_control(false,true,RC8_SELECT_NONE);step();r2_lora_shadow_publish();
  assert(!(atomic_get(&flags)&NORMAL)&&role.rx.display_valid&&published.choice==RC8_SELECT_LORA&&witness_published>initial&&witness.lora);
  struct lp_ble_return guard={0};uint64_t at=1000000;
  for(unsigned i=0;i<=30;i++){lp_ble_observe(&guard,&ble,at+1100000,0,at);at+=1000000;}
  assert(lp_ble_stable(&guard,at-1000000));ble.parent_boot++;
  lp_ble_observe(&guard,&ble,at+1100000,0,at);assert(!lp_ble_stable(&guard,at));
  lp_ble_observe(&guard,&ble,at+1100000,1,at+1000);assert(guard.since_us==at+1000);
  assert(!lp_ble_stable(&guard,at+1100000));
  puts("PASS actual LoRa cache/clock/shadow retained across BLE blips;30s exact-scope/refusal/expiry dwell");return 0;
 }
 uint64_t accepted=role.rx.accepted;lp_rx_expire(&role.rx,role.rx.observed_deadline_us);
 assert(!lp_rx_sync(&role.rx,22,7000000000,7,440000000,440000000)&&role.rx.accepted==accepted);
 puts("PASS actualRX authenticatedkind dispatch, conservativeMETAlease, nohint loss reacquisition and context expiry");
}
