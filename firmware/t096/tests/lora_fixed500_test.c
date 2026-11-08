/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual owner/core with synthetic hardware boundaries; no hardware. */
#include "lora_ram_fixture.h"
static void rx_epoch(const struct lp_packet *p,uint64_t event,uint64_t completed,uint64_t epoch){
 now_us=completed;
 queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=epoch,.generation=generation,
  .kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,role.rx.context,&test_group));have_event=true;step();
}
static void rx(const struct lp_packet *p,uint64_t event,uint64_t completed){rx_epoch(p,event,completed,7);}
static void drive_to(uint64_t end){
 while(now_us<end){
  if(owner.tx_kind&&!have_event&&now_us>=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000){
   queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=fake_capture_epoch,.generation=sent_generation,
    .kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000};have_event=true;
  }
  step();
 }
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 assert(LP_SCHEDULE==8&&LP_CAP_US==90000000&&LP_DISC_BEATS==0);
 assert(LP_SYNC_OFFSET_US+LP_LAUNCH_PAD_US+lp_duration(LP_SYNC)+
        LP_FOLLOW_LATEST_US+lp_duration(LP_FOLLOW)<LP_FRAME_END_US);
 if(mode==36){
  init(true);struct rc8_export m=source();r2_lora_source_publish(&m);
  drive_to(25000000);assert(owner.sent[3]==1&&role.tx.context_ready);
  uint64_t at=role.tx.core.durable.next_slot_us+LP_SYNC_OFFSET_US;
  drive_to(at+20000);assert(owner.tx_kind==LP_SYNC);
  now_us=sent_at_us+2200000;
  queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,
   .kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000};
  uint64_t actual_end=queued.end_local_us;
  have_event=true;step();
  assert(owner.stage==2&&owner.last_sync_us==actual_end&&!owner.error&&!atomic_get(&faulted));
  drive_to(actual_end+LP_FOLLOW_LATEST_US+1000000);
  assert(owner.sent[3]==2&&sent_at_us>=actual_end+LP_FOLLOW_OFFSET_US&&sent_at_us<actual_end+LP_FOLLOW_LATEST_US);
  puts("PASS worker sees SYNC TxDone after 2s and transmits FOLLOW on its original physical window");return 0;
 }
 if(mode==35){
  init(true);struct rc8_export m=source();
  m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=2;m.scope.root_boot=m.scope.parent_boot=44;
  m.until_tick=(now_us+1000000)*16;m.expires_ns=m.anchor_ns+(now_us+1000000)*1000;
  r2_lora_source_publish(&m);
  assert(role.tx.maps[0].valid&&role.tx.maps[0].until_tick==m.until_tick+LAB_SOURCE_HOLD_US*16);
  struct rc8_export before;memcpy(&before,&role.tx.maps[0],sizeof before);
  struct r2_capture at={m.until_tick,7,R2_CAPTURE_NOW};now_us=at.tick/16;
  assert(r2_lora_source_hold(&m.scope,at));
  struct rc8_scope other=m.scope;++other.root_boot;assert(!r2_lora_source_hold(&other,at));
  other=m.scope;++other.parent_boot;assert(!r2_lora_source_hold(&other,at));
  other=m.scope;++other.source_epoch;assert(!r2_lora_source_hold(&other,at));
  assert(!r2_lora_source_hold(&m.scope,(struct r2_capture){at.tick,8,R2_CAPTURE_NOW}));
  assert(!r2_lora_source_hold(&m.scope,(struct r2_capture){before.until_tick,7,R2_CAPTURE_NOW}));
  assert(!memcmp(&before,&role.tx.maps[0],sizeof before));
  r2_lora_source_withdraw();assert(!r2_lora_source_hold(&m.scope,at));
  puts("PASS fixed500 holds immutable BLE map only within its admitted expiry and scope");return 0;
 }
 if(mode==33||mode==34){
  init(true);struct rc8_export m=source();r2_lora_source_publish(&m);
  drive_to(25000000);assert(owner.sent[3]==1&&role.tx.context_ready);
  uint64_t grid=role.tx.core.durable.next_slot_us;
  uint64_t at=grid+LP_SYNC_OFFSET_US;
  drive_to(at-3000000);
  assert(owner.stage==1&&owner.prepared_kind==LP_SYNC&&!owner.tx_kind);
  unsigned count=txs;
  uint64_t charged=lp_charged(&role.tx.core.durable,now_us);
  now_us=at+(mode==33?2470863:LP_LAUNCH_PAD_US+1);
  step();
  assert(!(atomic_get(&flags)&(RESTART_PENDING|RESTART_HELD))&&owner.active&&!owner.stopping);
  assert(lp_charged(&role.tx.core.durable,now_us)==charged);
  if(mode==33){
   assert(txs==count+1&&owner.tx_kind==LP_SYNC&&!owner.late);
   drive_to(now_us+6000000);
   assert(owner.sent[3]==2&&owner.last_pair_us<grid+LP_FRAME_END_US);
  }else{
   assert(txs==count&&!role.tx.core.durable.pending&&owner.late==1);
   drive_to(at+LP_BEAT_US+6000000);
   assert(owner.sent[3]==2&&owner.sent[1]==1);
  }
  puts("PASS fixed500 delayed SYNC/FOLLOW within the existing frame and next-grid recovery after a miss");return 0;
 }
 if(mode==25){
  init(false);step();report(now_us);struct r2_lora_view v;r2_lora_view(&v);
  assert(!v.activity_kind&&v.activity_age_ms==UINT32_MAX&&!v.context_known);
  now_us+=500000;report(now_us);r2_lora_view(&v);assert(!v.activity_kind&&v.activity_age_ms==UINT32_MAX);
  queued=(struct r2_lora_hw_event){.tick=now_us*16,.capture_epoch=7,.generation=generation+1,.kind=R2_LORA_HW_RX_DONE,.length=1};have_event=true;step();
  r2_lora_view(&v);assert(!v.activity_kind&&v.activity_age_ms==UINT32_MAX);
  queued.generation=generation;queued.tick=now_us*16;have_event=true;step();
  report(now_us);r2_lora_view(&v);assert(v.activity_kind==3&&v.activity_age_ms<10);
  uint64_t decoded=now_us;now_us+=500000;report(now_us);r2_lora_view(&v);assert(v.activity_age_ms>=500);
  now_us+=500000;report(now_us);r2_lora_view(&v);assert(v.activity_age_ms>=1000);
  now_us=decoded-10000;r2_lora_view(&v);assert(v.activity_age_ms==UINT32_MAX);
  now_us=((uint64_t)UINT32_MAX+50)*1000;queued.tick=now_us*16;have_event=true;step();
  report(now_us);now_us+=400000;r2_lora_view(&v);assert(v.activity_kind==3&&v.activity_age_ms>=400&&v.activity_age_ms<410);
  puts("PASS idle/invalid generation never pulse; decoded unknown pulses once, ages across reports, rollback and49day boundary");return 0;
 }
 if(mode==20||mode==21||mode==22){
  init(true);struct rc8_export m=source();r2_lora_source_publish(&m);drive_to(25000000);
  assert(owner.sent[3]==1&&role.tx.context_ready);unsigned count=txs;uint64_t debt=lp_charged(&role.tx.core.durable,now_us);
  if(mode==22){
   struct r2_lora_view v;report(now_us);r2_lora_view(&v);assert(v.state==RC8_AVAILABLE);
   atomic_or(&flags,RESTART_PENDING);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE);atomic_and(&flags,~RESTART_PENDING);
   atomic_or(&flags,RESTART_HELD);report(now_us);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE);atomic_and(&flags,~RESTART_HELD);
   atomic_and(&flags,~ALLOWED);report(now_us);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE);atomic_or(&flags,ALLOWED);
   ++role.tx.scope.root;report(now_us);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE);--role.tx.scope.root;
   owner.error=-EIO;report(now_us);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE);owner.error=0;
   role.tx.core.durable.buckets[role.tx.core.durable.minute%61]=LP_CAP_US;
   report(now_us);r2_lora_view(&v);assert(v.state!=RC8_AVAILABLE&&txs==count);
   puts("PASS TX blue requires current send permission, matching context and pair budget; restart events immediately turn it orange");return 0;
  }
  if(mode==21){
   now_us+=120000;report(now_us);struct r2_lora_view v;r2_lora_view(&v);
   assert(v.time_valid&&v.state==RC8_AVAILABLE); /* Worker lag is not source expiry. */
   role.tx.maps[0].until_tick=now_us*16+16000;report(now_us);now_us+=1000;r2_lora_view(&v);
   assert(!v.time_valid&&v.state!=RC8_AVAILABLE);
   puts("PASS120ms poll delay cannot flicker a valid train; original capture expiry still turns it orange");return 0;
  }
  r2_lora_source_withdraw();step();uint64_t first_due=retry_at_us;
  assert(atomic_get(&flags)&RESTART_HELD);assert(first_due>=now_us-2000+30000000);
  m.from_tick=m.anchor_tick=now_us*16;m.anchor_ns=UINT64_C(1000000000000)+now_us*1000;++m.map_generation;r2_lora_source_publish(&m);
  drive_to(first_due-10000);assert(txs==count&&!owner.active&&lp_charged(&role.tx.core.durable,now_us)==debt);
  r2_lora_source_withdraw();step();uint64_t last_due=retry_at_us;assert(last_due>first_due);
  m.from_tick=m.anchor_tick=now_us*16;m.anchor_ns=UINT64_C(1000000000000)+now_us*1000;++m.map_generation;r2_lora_source_publish(&m);
  drive_to(last_due-2000);assert(txs==count&&!owner.active);
  drive_to(last_due+LP_BEAT_US+20000000);
  assert(owner.sent[3]>=2&&role.tx.stream==1&&owner.sent[1]==1&&!atomic_get(&faulted));
  puts("PASS restart waits30s after LAST source event, emits no RF during hold, preserves debt and same-source train");return 0;
 }
 if(mode==18){
  init(true);step();r2_lora_console_poll(22,now_us/1000);
  assert(console_notes==1&&strstr(console_note,"state=NO_SOURCE")&&strstr(console_note,"pair_age_ms=4294967295"));
  now_us+=1000000;struct rc8_export m=source();r2_lora_source_publish(&m);step();step();
  r2_lora_console_poll(22,now_us/1000);assert(console_notes==2&&strstr(console_note,"state=META_WAIT"));
  for(unsigned i=0;i<40;i++)r2_lora_console_poll(22,now_us/1000);assert(console_notes==2);
  uint64_t finish=now_us+20000000;
  while(now_us<finish&&!owner.sent[3]){
   if(owner.tx_kind&&!have_event&&now_us>=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000){
    queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,
     .kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000};have_event=true;}
   step();
  }
  assert(owner.sent[3]);report(now_us);r2_lora_console_poll(22,now_us/1000);
  assert(console_notes==3&&strstr(console_note,"state=ACTIVE"));
  char json[4096];int n=r2_lora_diagnostics_json(json,sizeof json,0);
  assert(n>0&&(size_t)n<sizeof json&&strstr(json,"\"train_active\":true")&&strstr(json,"\"last_pair_us\":"));
  now_us=owner.last_pair_us+LP_CADENCE_BOUND_US;report(now_us);
  console_busy=true;r2_lora_console_poll(22,now_us/1000);assert(console_notes==3);
  console_busy=false;now_us+=1000000;report(now_us);r2_lora_console_poll(22,now_us/1000);
  assert(console_notes==4&&strstr(console_note,"state=TX_STALLED"));
  r2_lora_source_withdraw();now_us+=1000000;step();r2_lora_console_poll(22,now_us/1000);
  assert(console_notes==5&&strstr(console_note,"state=NO_SOURCE"));
  puts("PASS nonblocking console state transitions/throttle/retry, real FOLLOW progress and stale-train diagnostics");return 0;
 }
 if(mode>=11&&mode<=17){
  init(true);struct rc8_export m=source();r2_lora_source_publish(&m);
  uint64_t changed_at=65000000,finish=165000000,old_charge=0;
  uint32_t old_cursor=0,old_serial=0,old_seed=0;bool changed=false;
  while(now_us<finish){
   if(!changed&&now_us>=changed_at){
    assert(owner.sent[3]>=2&&role.tx.context_ready);
    old_charge=lp_charged(&role.tx.core.durable,now_us);
    old_cursor=role.tx.core.durable.cursor;old_serial=role.tx.core.durable.serial;old_seed=role.tx.core.durable.seed;
    if(mode==12){r2_lora_source_withdraw(); /* A fresh formula, same authenticated scope. */
     m.from_tick=m.anchor_tick=now_us*16;m.anchor_ns+=now_us*1000;++m.map_generation;}
    else if(mode==13){m.scope.root=3;m.scope.root_boot=44;m.scope.kind=RC8_SELECTED_BLE;m.scope.parent=3;m.scope.parent_boot=44;
     m.until_tick=(now_us+60000000)*16;m.expires_ns=m.anchor_ns+(now_us+60000000)*1000;}
    else if(mode==14){++fake_capture_epoch;m.capture_epoch=fake_capture_epoch;}
    else ++m.scope.source_epoch;
    if(mode==15){ /* A real cap block cannot mint credit through rearm. */
     memset(role.tx.core.durable.buckets,0,sizeof role.tx.core.durable.buckets);
     role.tx.core.durable.buckets[role.tx.core.durable.minute%61]=LP_CAP_US;
     old_charge=LP_CAP_US;
    }
    r2_lora_source_publish(&m);changed=true;
   }
   if(owner.tx_kind&&!have_event&&now_us>=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000){
    queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=fake_capture_epoch,.generation=sent_generation,
     .kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+r2_lora_hw_budget_us(sent_len)-10000};have_event=true;
   }
   if(mode==16&&changed&&owner.prepared_kind==LP_META){ /* Miss one recovery META launch. */
    now_us=role.tx.reservation.slot_us+LP_CONTROL_LAUNCH_LATE_US+2000;mode=17;
   }
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
   if(changed&&mode!=12&&mode!=14&&mode!=15&&owner.sent[1]>=2&&owner.sent[3]>=3){
    assert(now_us-changed_at>=30000000&&now_us-changed_at<(mode==17?60000000:50000000)&&role.tx.stream>1&&role.tx.core.durable.seed==old_seed);
    assert(role.tx.core.durable.cursor>=old_cursor&&role.tx.core.durable.serial>old_serial);
    assert(lp_charged(&role.tx.core.durable,now_us)>=old_charge);
    report(now_us);struct r2_lora_view v;r2_lora_view(&v);assert(v.time_valid&&v.state==RC8_AVAILABLE);
    uint64_t sample=now_us;now_us=owner.last_pair_us+LP_CADENCE_BOUND_US;report(now_us);r2_lora_view(&v);
    assert(v.state!=RC8_AVAILABLE);now_us=sample;
    puts("PASS context turnover starts fresh META after30s hold with debt/replay/capture fences retained");return 0;
   }
  }
  if(mode==12||mode==14){assert(owner.sent[1]==1&&role.tx.stream==1&&owner.sent[3]>=3);puts("PASS same authenticated source return/local capture rebind preserves train/grid without extra META");return 0;}
  if(mode==15){assert(owner.sent[1]==1&&lp_charged(&role.tx.core.durable,now_us)==LP_CAP_US);puts("PASS context recovery remains blocked by actual airtime debt");return 0;}
  assert(!"context META waited for old300s grid");
 }
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
    assert(lp_tx_reserve(&t,at,&r)&&r.has_pair&&r.control==(i%12?0:LP_META));
    assert(lp_channel(t.order,r.cursor)==50&&lp_channel(t.order,r.cursor+1)==50);
    uint64_t charge=lp_charged(&t.durable,at);assert(charge<=LP_CAP_US);if(charge>maximum)maximum=charge;
    if(r.control)lp_tx_control_complete(&t);
    uint64_t sync=at+LP_SYNC_OFFSET_US+263168;
    assert(lp_tx_capture_sync(&t,sync)&&lp_tx_capture_follow(&t,sync+LP_FOLLOW_OFFSET_US+295936));
    if(i==119)assert(charge==77467040);
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
  assert(lp_tx_reserve_control(&t,31000000,&r,true)&&r.control==LP_META);
  lp_tx_control_complete(&t);
  lp_tx_cancel(&t,true);
  assert(!lp_tx_reserve_control(&t,361000000,&r,true));
  assert(lp_tx_reserve(&t,361000000,&r)&&!r.control);
  lp_tx_cancel(&t,true);
  assert(lp_tx_reserve_control(&t,391000000,&r,true)&&r.control==LP_META);
  lp_tx_cancel(&t,true);
  /* A missed attempt and a skipped slot do not discharge the obligation. */
  assert(lp_tx_reserve(&t,451000000,&r)&&r.control==LP_META);
  lp_tx_control_complete(&t);lp_tx_cancel(&t,true);
  assert(lp_tx_reserve(&t,781000000,&r)&&!r.control);
  lp_tx_cancel(&t,true);
  assert(lp_tx_reserve(&t,811000000,&r)&&r.control==LP_META);
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
  puts("PASS META360 retries on grid until completion and retains original airtime cap");return 0;
 }
 if(mode==3||mode==5||mode==6||mode==7||mode==19){
  init(true);uint64_t next=1337000,finish=now_us+(mode==19?UINT64_C(1800000000):UINT64_C(410000000));unsigned publications=0;
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
  if(mode==19){assert(owner.sent[1]==5&&owner.sent[2]==60&&owner.sent[3]==60&&role.tx.stream==1);
   puts("PASS30min synthetic routine fresh BLE maps retain one train, exact30s pairs/META360 and airtime cap");return 0;}
  if(mode==7){assert(rearmed&&owner.sent[1]>=2);puts("PASS explicit OFF/ON freshMETA with seed/cursor/serial/debt retained");return 0;}
  assert(owner.sent[1]==2&&owner.sent[2]==owner.sent[3]&&!owner.sent[0]);
  if(mode==6)assert(role.tx.stream==2&&owner.sent[2]>=7);
  else assert(owner.sent[2]>=10&&role.tx.stream==1);
  if(mode==5)assert(withdrew&&delayed&&!owner.late);
  puts("PASS actual staged owner fixed PHY, on-grid META360 and unchanged BLE grants plus explicit lab hold");return 0;
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
 assert(mode==4||mode==8||mode==23||mode==24||(mode>=26&&mode<=32));init(false);
 if(mode==8)r2_lora_control(true,true,RC8_SELECT_NONE);
 if(mode==23||mode==24||mode==30)r2_lora_control(mode!=23,true,RC8_SELECT_BLE);
 step();assert(owner.listening&&tuning_phy==R2_LORA_FHSS&&tuning_channel==50);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,.has_pair=true,.delay_10ms=600,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
  .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 rx(&m,10000000,10700000);assert(role.rx.cached&&!role.rx.hint);
 report(now_us);assert(published.context_known&&published.activity_kind==1&&published.activity_age_ms<10&&published.rx_age_ms==UINT32_MAX);
 assert(role.rx.observed_deadline_us==10000000-787456+LP_LEASE_US);
 if(mode==31){
  for(unsigned i=0;i<3;i++){
   uint64_t event=UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US;
   struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};rx(&sync,event,event+200000);
   struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000+event*2700/1000000};
   rx(&follow,event+2800000,event+3000000);report(now_us);
   assert(published.observer_available&&published.utc_us&&published.display_acquiring&&!published.time_valid&&!published.quality_known);
   if(!i)assert(!published.prediction_n&&published.prediction_rms_ns==UINT32_MAX);
   if(i==1)assert(published.prediction_n==1&&published.prediction_rms_ns>80000&&published.prediction_rms_ns<82000);
   if(i==2)assert(published.prediction_n==2&&role.watch.last_error>-200&&role.watch.last_error<200);
  }
  puts("PASS real MAC owner first-pair human UTC, pair2 causal error and early rate learning without trained permissions");return 0;
 }
 for(unsigned i=0;i<10;i++){
  uint64_t event=UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US;
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};rx(&sync,event,event+200000);assert(role.rx.pending);
  if(i==2)continue; /*lostFOLLOW must not break acquisition of the next pair*/
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000};
  rx(&follow,event+2800000,event+3000000);assert(role.rx.accepted==i+1-(i>=2));
  assert(role.rx.last_tx_ns==follow.utc_ns+LP_FIXED_EVENT_DELAY_NS);
  assert(owner.listening&&tuning_phy==R2_LORA_FHSS&&tuning_channel==50);
 }
 if(mode==32){
  m.tz=(struct r2_tz){2,-360};m.serial=2;m.cursor=20;m.utc_ns=UINT64_C(1310000000000);rx(&m,310000000,310700000);
  assert(published.observer_available&&role.watch.admitted);
  fake_capture_epoch=8;now_us+=1000;report(now_us);
  assert(!published.utc_us&&!published.observer_available&&published.metadata_cached&&!role.watch.admitted&&role.watch.reason==4);
  assert(!lp_receiver_watch_sync(&role.watch,20,now_us*16,8,now_us,now_us));
  m.serial=3;m.utc_ns=UINT64_C(1313000000000);rx_epoch(&m,313000000,313700000,8);
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=20};rx_epoch(&sync,314000000,314200000,8);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=20,.utc_ns=UINT64_C(1314000000000)};rx_epoch(&follow,316800000,317000000,8);
  assert(published.observer_available&&!published.prediction_n&&role.watch.rx.fit_count==1);
  r2_lora_request(false);step();assert(!role.watch.admitted&&role.watch.rx.cached&&!published.utc_us);
  assert(role.watch.rx.tz.id==2&&role.watch.rx.tz.offset_min==-360);
  r2_lora_request(true);step();assert(!role.watch.admitted&&!published.utc_us);
  m.serial=4;m.cursor=22;m.utc_ns=UINT64_C(1330000000000);rx_epoch(&m,330000000,330700000,8);
  sync.cursor=22;rx_epoch(&sync,331000000,331200000,8);follow.cursor=22;follow.utc_ns=UINT64_C(1331000000000);rx_epoch(&follow,333800000,334000000,8);
  assert(role.watch.admitted&&published.observer_available&&!published.prediction_n);
  m.serial=5;m.cursor=24;m.utc_ns=UINT64_C(1340000000000);m.time_valid=false;m.has_pair=false;m.delay_10ms=0;rx_epoch(&m,340000000,340700000,8);
  assert(!role.watch.admitted&&!published.utc_us&&published.metadata_cached&&role.watch.reason==3);
  uint32_t admitted=role.rx.accepted;sync.cursor=24;rx_epoch(&sync,341000000,341200000,8);follow.cursor=24;follow.utc_ns=UINT64_C(1341000000000);rx_epoch(&follow,343800000,344000000,8);
  assert(!published.utc_us&&!published.observer_available&&role.rx.accepted==admitted&&role.watch.rx.tz.id==2);
  puts("PASS real owner capture/OFF/authenticated TIME_INVALID fences; cached META/TZ cannot readmit time without fresh valid META");return 0;
 }
 if(mode==29||mode==30){
  uint64_t original_meta=role.rx.observed_deadline_us;
  m.tz=(struct r2_tz){2,-360};m.serial=2;m.cursor=20;m.utc_ns+=UINT64_C(300000000000);
  rx(&m,310000000,310700000);original_meta=role.rx.observed_deadline_us;
  for(unsigned i=10;i<30;i++){
   uint64_t event=UINT64_C(17000000)+(uint64_t)i*LP_BEAT_US;
   struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};rx(&sync,event,event+200000);
   struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+event*1000};
   rx(&follow,event+2800000,event+3000000);report(now_us);
   assert(published.observer_available&&published.utc_us&&published.metadata_cached&&published.prediction_n);
   assert(role.rx.observed_deadline_us==original_meta&&role.watch.rx.observed_deadline_us==original_meta);
  }
  assert(!published.context_known&&!published.time_valid&&!published.quality_known&&!published.until_us);
  assert(published.choice==RC8_SELECT_NONE&&role.watch.rx.accepted==29&&role.rx.accepted<role.watch.rx.accepted);
  assert(published.tz.id==2&&published.tz.offset_min==-360);
  witness_enabled=true;r2_lora_shadow_publish();assert(!witness_published);
  uint32_t count=role.watch.predictions,rms=published.prediction_rms_ns;uint64_t reference=role.watch.rx.display_us;
  now_us=role.watch.rx.last_us+90000000;report(now_us);
  assert(published.observer_available&&published.utc_us&&published.rx_age_ms==90000&&published.prediction_rms_ns==rms);
  now_us=role.watch.rx.last_us+UINT64_C(1000000000);report(now_us);
  assert(published.observer_available&&published.utc_us&&published.prediction_n==count&&published.prediction_rms_ns==rms&&role.watch.rx.display_us==reference);
  m.serial++;m.cursor=60;m.utc_ns+=UINT64_C(1500000000000);uint64_t resume=now_us+1000000;rx(&m,resume,resume+700000);report(now_us);
  assert(published.observer_available&&published.context_known&&role.watch.predictions==count&&role.watch.rx.display_us==reference);
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=60};uint64_t physical=resume+2000000;rx(&sync,physical,physical+200000);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=60,.utc_ns=UINT64_C(1000000000000)+physical*1000};rx(&follow,physical+2800000,physical+3000000);report(now_us);
  assert(published.observer_available&&role.watch.rx.fit_count==1&&role.watch.rx.display_us==physical&&role.watch.predictions==count+1&&!published.quality_known);
  m.scope.stream++;m.serial++;m.cursor=62;m.utc_ns+=UINT64_C(1000000000);rx(&m,now_us+1000000,now_us+1700000);report(now_us);
  assert(!published.observer_available&&!published.utc_us&&!published.prediction_n&&published.metadata_cached);
  printf("PASS %s observer retains META/TZ, actual error and UTC through expiredMETA/2pairloss/longHOLD; no live grant/shadow, sameMETA keeps history and newcontext fences it\n",mode==30?"BLE preferred":"LoRa selected");return 0;
 }
 if(mode>=26&&mode<=28){
  uint64_t event=317000000,predicted;assert(lp_rx_estimate(&role.rx,event*16,7,event,&predicted));
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=20};rx(&sync,event,event+200000);
  int64_t jump=mode==26?20000:mode==27?INT64_C(3000000000):-INT64_C(3000000000);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=20,.utc_ns=(uint64_t)((int64_t)(UINT64_C(1000000000000)+event*1000)+jump)};
  int64_t error=(int64_t)(follow.utc_ns+LP_FIXED_EVENT_DELAY_NS)-(int64_t)predicted;
  rx(&follow,event+2800000,event+3000000);report(now_us);
  assert(role.rx.accepted==10&&(diagnostics.pair_prediction_flags&1));
  assert(diagnostics.pair_prediction_error_ns==(mode==26?error:mode==27?INT32_MAX:-INT32_MAX));
  assert(((diagnostics.pair_prediction_flags&2)!=0)==(mode!=26));
  char json[3072];int n=r2_lora_diagnostics_json(json,sizeof json,0);assert(n>0&&n<(int)sizeof json);
  char expected[80];snprintf(expected,sizeof expected,"\"pair_prediction\":[true,%d,%s]",diagnostics.pair_prediction_error_ns,mode==26?"false":"true");assert(strstr(json,expected));
  n=r2_lora_json(json,sizeof json,0);assert(n>0&&n<(int)sizeof json&&strstr(json,expected)&&strstr(json,"\"tx_source_max_us\":[0,0]"));
  if(mode==26){
   assert(error==20000); /* A post-assimilation residual would be smaller. */
   rx(&follow,event+6000000,event+6200000);report(now_us);assert(diagnostics.pair_prediction_error_ns==20000&&(diagnostics.pair_prediction_flags&1));
   m.scope.stream++;m.serial++;m.cursor=22;m.utc_ns+=UINT64_C(400000000000);rx(&m,event+10000000,event+10700000);report(now_us);
   assert(!role.rx.have_pair&&!(diagnostics.pair_prediction_flags&1));
  }
  printf("PASS causal pre-update clock prediction mode%u, signed clipping, authenticated context reset and bounded truthful JSON\n",mode);return 0;
 }
 if(mode==23||mode==24){
  report(now_us);assert(role.rx.trained&&(atomic_get(&flags)&NORMAL));
  assert(published.active&&published.requested&&published.state==RC8_AVAILABLE&&published.utc_us&&published.choice==RC8_SELECT_NONE);
  assert(published.context_known&&published.link_samples==9&&published.fit_span_ms==270000);
  witness_enabled=true;r2_lora_shadow_publish();assert(!witness_published);
  uint64_t until=role.rx.physical_deadline_us;now_us=until;report(now_us);
  assert(published.state!=RC8_AVAILABLE&&published.utc_us&&published.observer_available&&published.choice==RC8_SELECT_NONE&&!published.time_valid&&!published.quality_known);
  assert(!published.display_held&&!published.display_acquiring);
  printf("PASS %s independent qualified then observer LoRa time while BLE preferred; no primary/witness takeover, actual span and original physical expiry\n",mode==24?"RC":"GM");return 0;
 }
 if(mode==8){
  assert(role.rx.trained&&role.rx.display_valid);witness_enabled=true;r2_lora_shadow_publish();unsigned initial=witness_published;
  struct lp_ble_scope ble={.root=3,.root_boot=44,.parent=3,.parent_boot=44,.source_epoch=5,.train=7,.capture_epoch=1,.parent_capture_epoch=6};
  uint64_t first=now_us;
  for(unsigned i=0;i<=30;i++){
   now_us=first+(uint64_t)i*1000000;r2_lora_ble_observe(&ble,now_us+1100000,0,now_us);
   r2_lora_control(true,true,RC8_SELECT_BLE);step();
   assert(role.rx.display_valid&&role.rx.trained);
   assert(((atomic_get(&flags)&NORMAL)!=0)==(i==30));
   assert(r2_lora_ble_shadow_allowed()==(i==30));
  }
  /* A LoRa-export quality refusal is not loss of the already selected BLE
   * clock. Only the common owner can withdraw that preferred clock. */
  now_us+=1000000;r2_lora_ble_observe(&ble,now_us+1100000,1,now_us);
  assert(!lp_ble_stable(&ble_return,now_us));
  r2_lora_control(true,true,RC8_SELECT_BLE);step();
  assert((atomic_get(&flags)&NORMAL)&&r2_lora_ble_shadow_allowed());
  now_us+=1000000;r2_lora_ble_observe(NULL,0,1,now_us);
  r2_lora_control(true,true,RC8_SELECT_BLE);step();
  assert((atomic_get(&flags)&NORMAL)&&r2_lora_ble_shadow_allowed());
  r2_lora_ble_observe(NULL,0,0,now_us);r2_lora_control(true,true,RC8_SELECT_NONE);step();r2_lora_shadow_publish();
  assert(!(atomic_get(&flags)&NORMAL)&&role.rx.display_valid&&published.choice==RC8_SELECT_LORA&&witness_published>initial&&witness.lora);
  /* A real withdrawal still falls back immediately and requires a fresh
   * continuous return dwell before common BLE/shadow ownership resumes. */
  first=now_us+1000000;
  for(unsigned i=0;i<=30;i++){
   now_us=first+(uint64_t)i*1000000;r2_lora_ble_observe(&ble,now_us+1100000,1,now_us);
   r2_lora_control(true,true,RC8_SELECT_BLE);step();
   assert(((atomic_get(&flags)&NORMAL)!=0)==(i==30));
   assert(r2_lora_ble_shadow_allowed()==(i==30));
  }
  struct lp_ble_return guard={0};uint64_t at=1000000;
  for(unsigned i=0;i<=30;i++){lp_ble_observe(&guard,&ble,at+1100000,0,at);at+=1000000;}
  assert(lp_ble_stable(&guard,at-1000000));ble.parent_boot++;
  lp_ble_observe(&guard,&ble,at+1100000,0,at);assert(!lp_ble_stable(&guard,at));
  lp_ble_observe(&guard,&ble,at+1100000,1,at+1000);assert(guard.since_us==at+1000);
  assert(!lp_ble_stable(&guard,at+1100000));
  ++ble.train;lp_ble_observe(&guard,&ble,at+1100000,1,at+2000);assert(guard.since_us==at+2000);
  ++ble.parent_capture_epoch;lp_ble_observe(&guard,&ble,at+1100000,1,at+3000);assert(guard.since_us==at+3000);
  ++ble.capture_epoch;lp_ble_observe(&guard,&ble,at+1100000,1,at+4000);assert(guard.since_us==at+4000);
  assert(r2_lora_ble_needed());r2_lora_request(false);assert(!r2_lora_ble_needed()&&!ble_return.qualified);
  r2_lora_ble_observe(&ble,now_us+1100000,1,now_us);assert(!ble_return.qualified);
  r2_lora_request(true);assert(r2_lora_ble_needed());r2_lora_control(true,false,RC8_SELECT_NONE);
  assert(!r2_lora_ble_needed()&&!ble_return.qualified);
  puts("PASS RC actual BLE selection survives export-only refusal/loss; real withdrawal retains LoRa and restarts30s exact-scope/refusal/expiry dwell");return 0;
 }
 uint64_t accepted=role.rx.accepted;lp_rx_expire(&role.rx,role.rx.observed_deadline_us);
 assert(!lp_rx_sync(&role.rx,22,7000000000,7,440000000,440000000)&&role.rx.accepted==accepted);
 puts("PASS actualRX authenticatedkind dispatch, conservativeMETAlease, nohint loss reacquisition and context expiry");
}
