/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Real native owner/core/codec with synthetic public hardware boundaries. */
#include "lora_ram_fixture.h"
#include "../protocol/lora_gm_admission.h"
static void boot_clear(void){
 memset(&role,0,sizeof role);memset(&owner,0,sizeof owner);memset(&diagnostics,0,sizeof diagnostics);
 memset(&published,0,sizeof published);memset(replay,0,sizeof replay);
 observer_ready=flags=faulted=source_revision=0;closed=1;stop_deadline=lab_until=0;generation=0;
#ifdef R2_LORA_PASSIVE_SLOW
 retry_at_us=0;retry_count=0;
#endif
 stop_ack_missing=0;txs=0;now_us=1000000;have_event=false;init(true);
}
static void prepare_tx(void){
 boot_clear();
#ifdef R2_LORA_PASSIVE_SLOW
 r2_lora_gm_admission(true,UINT64_C(10000000000),true,5,7);step();step();
#else
 r2_lora_request(true);step();
#endif
 struct rc8_export m=source();r2_lora_source_publish(&m);step();
 now_us=owner.quiet_until_us+21000000;assert(restore(now_us+PRECHARGE_LEAD_US+100000));step();
 assert(role.tx.core.ready&&role.tx.core.durable.seed&&lp_charged(&role.tx.core.durable,now_us)==0);
}
static void launch(void){
 uint64_t slot=role.tx.core.durable.next_slot_us;now_us=slot-PRECHARGE_LEAD_US;step();assert(owner.stage==3&&!txs);
 now_us=slot;step();if(txs!=1||owner.tx_kind!=LP_META||role.tx.core.durable.pending!=1){char diagnostic[4096];r2_lora_json(diagnostic,sizeof diagnostic,0);fprintf(stderr,"%s\n",diagnostic);}assert(txs==1&&owner.tx_kind==LP_META&&role.tx.core.durable.pending==1);
}
static void finish_pair(void){
 uint64_t slot=role.tx.reservation.slot_us;done(slot,LP_META);assert(owner.stage==1);
 now_us=slot+LP_SYNC_OFFSET_US;step();assert(owner.tx_kind==LP_SYNC);
 done(slot+LP_SYNC_OFFSET_US,LP_SYNC);assert(role.tx.core.durable.pending==2);
 now_us=owner.last_sync_us+LP_FOLLOW_OFFSET_US;uint64_t at=now_us;step();assert(owner.tx_kind==LP_FOLLOW);
 done(at,LP_FOLLOW);assert(!role.tx.core.durable.pending&&role.tx.core.durable.cursor==role.tx.reservation.cursor+2);
}
static void owner_test(unsigned mode){
 prepare_tx();
 if(mode==1){
  assert(owner.quiet_until_us<=LP_BEAT_US+2000000&&owner.quiet_until_us<LP_HOUR_US);
  for(unsigned i=0;i<61;i++)assert(!role.tx.core.durable.buckets[i]);
  assert(!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.cursor&&!role.tx.core.durable.serial);
  assert(!r2_diag_scratch.lora.retained.boot);
  struct lp_tx fresh;struct lp_reservation r;
  assert(lp_tx_start(&fresh,99,4000000,6200000,1));assert(lp_tx_reserve(&fresh,6200000,&r)&&r.has_pair);
  assert(r.slot_us<20000000);return;
 }
 if(mode==6){role.tx.core.durable.buckets[role.tx.core.durable.minute%61]=LP_CAP_US;
  uint64_t slot=role.tx.core.durable.next_slot_us;now_us=slot;step();assert(!txs&&owner.refused);
  assert(lp_charged(&role.tx.core.durable,now_us)==LP_CAP_US);return;
 }
 launch();
 if(mode==4){r2_lora_request(false);step();step();assert(!role.tx.core.durable.pending&&!role.tx.core.durable.fh_disabled&&lp_charged(&role.tx.core.durable,now_us)>0);return;}
 if(mode==5){done(role.tx.reservation.slot_us,LP_META);now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;reject_next_tx=true;step();assert(txs==1&&!role.tx.core.durable.fh_disabled);step();assert(txs==2);return;}
 finish_pair();struct lp_checkpoint saved=role.tx.core.durable;
 assert(lp_charged(&saved,now_us)==lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW));
 if(mode==2){
#ifdef R2_LORA_PASSIVE_SLOW
  r2_lora_gm_admission(false,0,false,0,0);step();step();step();assert(transmitter);
  // RX activity and report cannot alias any TX byte.
  owner.received[0]=123;report(now_us);assert(!memcmp(&saved,&role.tx.core.durable,sizeof saved));
  r2_lora_gm_admission(true,UINT64_C(10000000000),true,5,7);step();step();step();assert(transmitter);
#else
  r2_lora_request(false);step();step();r2_lora_request(true);step();
#endif
  assert(!memcmp(&saved,&role.tx.core.durable,sizeof saved));
 }else if(mode==3){
  r2_lora_control(true,true,RC8_SELECT_NONE);step();step();step();
  r2_lora_control(false,true,RC8_SELECT_NONE);step();step();step();
  assert(!memcmp(&saved,&role.tx.core.durable,sizeof saved));
 }else if(mode==7){
  role.tx.core.durable.fh_disabled=1;uint32_t seed=role.tx.core.durable.seed;
  boot_clear();
#ifdef R2_LORA_PASSIVE_SLOW
  r2_lora_gm_admission(true,UINT64_C(10000000000),true,5,7);step();step();
#endif
  now_us=owner.quiet_until_us+21000000;assert(restore(now_us+2000000));
  assert(role.tx.core.durable.seed!=seed&&!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.pending);
  assert(!lp_charged(&role.tx.core.durable,now_us)&&!role.tx.core.durable.cursor&&!role.tx.core.durable.serial);
 }else if(mode==8){
  // Separate diagnostic RX counts and TX reserve timing values.
  owner.received[1]=42;owner.reserve_lead_us=77;report(now_us);
  assert(diagnostics.received[1]==42&&diagnostics.reserve_lead_us==77);
 }
 assert(!owner.storage_fail&&!owner.storage_max_us);
}
static void replay_test(void){
 boot_clear();init(false);r2_lora_request(true);step();
 struct lp_packet p={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,.utc_ns=UINT64_C(1000000000000)};
 p.tz=r2_tz_unknown();p.scope=(struct lp_scope){.kind=1,.emitter=1,.root=1,.root_boot=101,.epoch=5,.stream=1};
 struct lp_packet history[20];
 for(unsigned n=0;n<20;n++){
  p.scope.boot=100+n;p.scope.seed=17+n;p.cursor=0;p.serial=1;p.utc_ns+=UINT64_C(1000000000);history[n]=p;
  uint8_t ctx[16];assert(!lp_context(ctx,&p.scope,LP_SCHEDULE,&test_group));
  assert(!tune(R2_LORA_DTS,0,true));uint64_t at=now_us+2000000;
  packet(&p,ctx,at,at+1000);assert(role.rx.scope.boot==100+n&&role.rx.scope.seed==17+n);
  assert(!role.rx.have_pair&&!role.rx.trained&&!role.rx.display_valid&&!role.rx.fit_count);
  for(unsigned j=0;j<n;j++){
   uint64_t boot=role.rx.scope.boot;assert(!tune(R2_LORA_DTS,0,true));at=now_us+2000000;
   assert(!lp_context(ctx,&history[j].scope,LP_SCHEDULE,&test_group));
   packet(&history[j],ctx,at,at+1000);assert(role.rx.scope.boot==boot);
  }
 }
 // A recently retired boot with fabricated greater time still cannot roll back.
 struct lp_packet old=history[18];old.utc_ns=p.utc_ns+1;assert(!lp_replay_admit(&replay[0],&old));
 // Same boot cannot change seed, reset serial/cursor or replay META.
 old=p;old.utc_ns++;old.scope.seed++;assert(!lp_replay_admit(&replay[0],&old));
 old=p;old.utc_ns++;assert(!lp_replay_admit(&replay[0],&old));
 uint8_t wire[LP_META_BYTES],final_ctx[16];struct lp_packet decoded;
 assert(!lp_context(final_ctx,&p.scope,LP_SCHEDULE,&test_group));assert(!lp_encode(wire,sizeof wire,&p,final_ctx,&test_group));
 wire[1]=2;assert(lp_decode(&decoded,wire,sizeof wire,final_ctx,&test_group)==-EINVAL); // separate .250 cohort
}
static void bridge_test(unsigned mode){
 prepare_tx();struct rc8_export m=source();
 if(mode==9||mode==10){
  m.scope.root=m.scope.parent=mode==9?2:3;m.scope.root_boot=m.scope.parent_boot=202;
  m.scope.kind=RC8_SELECTED_BLE;m.scope.source_epoch=9;m.map_generation++;
  r2_lora_source_publish(&m);step();step();step();
  assert(transmitter&&(atomic_get(&flags)&SOURCE_PRESENT));launch();
  struct lp_packet p;assert(!lp_decode(&p,sent,sent_len,role.tx.context,&test_group));
  assert(p.scope.root==m.scope.root&&p.scope.parent==m.scope.parent&&p.scope.kind==RC8_SELECTED_BLE);
  assert(p.scope.root_boot==202&&p.scope.parent_boot==202&&p.scope.emitter==1&&p.scope.boot==22);
  report(now_us);assert(published.kind==RC8_SELECTED_BLE&&published.root_tag==m.scope.root&&published.parent_tag==m.scope.parent);
  finish_pair();return;
 }
 if(mode==11){
  uint64_t until=(now_us+2000000)*16;m.until_tick=until;r2_lora_source_publish(&m);
  assert(role.tx.maps[0].until_tick==until);m.until_tick=UINT64_MAX;r2_lora_source_publish(&m);
  assert(role.tx.maps[0].until_tick==until);now_us=until/16;uint64_t ns,boot;uint32_t ep;struct r2_capture cap;
  assert(!source_now(&ns,&ep,&boot,&cap,NULL));return;
 }
 launch();uint64_t slot=role.tx.reservation.slot_us;done(slot,LP_META);now_us=slot+LP_SYNC_OFFSET_US;step();assert(owner.tx_kind==LP_SYNC);
 if(mode==12){
  queued=(struct r2_lora_hw_event){.tick=now_us*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_TX_DONE};have_event=true;
  preserve();assert(role.tx.pinned);m.until_tick=queued.tick;r2_lora_source_publish(&m);assert(!role.tx.pinned);return;
 }
 done(slot+LP_SYNC_OFFSET_US,LP_SYNC);assert(role.tx.core.durable.pending==2);
 uint64_t charge=lp_charged(&role.tx.core.durable,now_us);uint32_t old=role.tx.core.durable.cursor;
 if(mode==14)owner.error=-EIO;
 if(mode==15){now_us=owner.last_sync_us+LP_FOLLOW_LATEST_US-4000;step();assert(owner.late==1&&owner.stopping);}
 else r2_lora_source_withdraw();
 step();step();assert((mode==15||atomic_get(&closed))&&!role.tx.core.durable.pending);
 assert(lp_charged(&role.tx.core.durable,now_us)==charge);
 if(mode==14){assert(role.tx.core.durable.fh_disabled);return;}
 assert(!role.tx.core.durable.fh_disabled&&role.tx.core.durable.cursor==old+1);
#ifdef R2_LORA_PASSIVE_SLOW
 assert(role.tx.core.durable.owed&LP_OWED_PAIR);
#endif
 for(unsigned i=0;i<2;i++)assert(role.tx.core.durable.channel_end_ms[lp_channel(role.tx.core.order,i)]>=now_us/1000-4);
 assert(!role.tx.core.sync_end_us);
#ifdef R2_LORA_PASSIVE_SLOW
 assert(role.tx.context_ready==(mode==15));
#else
 assert(!role.tx.context_ready);
#endif
 if(mode==15){
  unsigned before=txs;for(unsigned i=0;i<10000&&txs==before;i++)step();
  assert(txs==before+1&&owner.tx_kind==LP_META&&role.tx.reservation.cursor==2);
  assert(role.tx.reservation.slot_us>slot&&!role.tx.core.durable.fh_disabled);
  finish_pair();assert(role.tx.core.durable.cursor==4&&!role.tx.core.durable.owed);
 }
}
static void shrink_during_completion(void){
 struct rc8_export m=role.tx.maps[0];m.until_tick=queued.tick;r2_lora_source_publish(&m);
}
static void arm_shrink_at_unlock(void){unlock_hook=shrink_during_completion;}
static unsigned projection_preemptions;
static void repeated_publication(void){
 struct rc8_export m=role.tx.maps[0];r2_lora_source_publish(&m);
 if(++projection_preemptions<3)projection_hook=repeated_publication;
}
static void scheduling_test(unsigned mode){
 prepare_tx();assert(k_thread_priority_get(&worker_thread)>9);
 if(mode==21){
  /* Actual priority selector at every TX phase, overdue deadlines, RX and
   * drain: none may outrank main7, precision8 or the GM owner9. */
  for(unsigned stage=0;stage<4;stage++)for(unsigned kind=0;kind<=LP_FOLLOW;kind++){
   owner.stage=stage;owner.tx_kind=kind;
   for(unsigned late=0;late<2;late++){
    now_us=role.tx.core.durable.next_slot_us+(late?10000000:0);
    assert(worker_priority()>9);
   }
  }
  transmitter=false;assert(worker_priority()>9);owner.stopping=true;
  assert(worker_priority()>9);stop_now();assert(k_thread_priority_get(&worker_thread)>9);return;
 }
#ifndef R2_LORA_TX_DIAGNOSTIC
 if(mode==24){
  projection_hook=r2_lora_source_withdraw;uint64_t ns,boot;uint32_t epoch;struct r2_capture limit;
  assert(!source_now(&ns,&epoch,&boot,&limit,NULL));assert(!(atomic_get(&flags)&SOURCE_PRESENT));return;
 }
 launch();uint64_t slot=role.tx.reservation.slot_us;done(slot,LP_META);
 now_us=slot+LP_SYNC_OFFSET_US;step();assert(owner.tx_kind==LP_SYNC);
 uint64_t start=now_us-2000;struct r2_lora_hw_edge floor=role.tx.edge;
 queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=generation,
  .kind=R2_LORA_HW_TX_DONE,.end_local_us=start+lp_duration(LP_SYNC)-10000};have_event=true;
 if(mode==23){
  queued.tick=floor.tick-1;preserve();assert(!role.tx.pinned);
  uint64_t ns;uint32_t version;bool raced=false;
  assert(!completed_source(&queued,&ns,&version,&raced)&&raced);
  queued.tick=start*16;queued.generation++;preserve();assert(!role.tx.pinned);
  queued.generation=generation;queued.capture_epoch++;preserve();assert(!role.tx.pinned);
  queued.capture_epoch=7;preserve();assert(role.tx.pinned);return;
 }
 now_us=queued.end_local_us+1000;
 if(mode==25||mode==26||mode==27){
  projection_hook=mode==25?r2_lora_source_withdraw:mode==26?arm_shrink_at_unlock:repeated_publication;step();step();
  if(mode==27){
   /* No-op publications must not manufacture projection contention or lose
    * the captured SYNC; the genuine pin transition remains versioned. */
   assert(projection_preemptions>=2&&projection_preemptions<=3&&owner.stage==2&&role.tx.core.durable.pending==2&&!owner.missing_map);
#ifdef R2_LORA_PASSIVE_SLOW
   assert(sync_map[1]==0&&sync_map[0]==1);
#endif
   projection_hook=NULL;return;
  }
  assert(!role.tx.core.durable.pending&&!role.tx.core.durable.fh_disabled&&!atomic_get(&faulted));
  assert(role.tx.core.durable.cursor==2&&lp_charged(&role.tx.core.durable,now_us)>0);
  assert(owner.missing_map==1);if(mode==27)assert(projection_preemptions==3&&!projection_hook);
#ifdef R2_LORA_PASSIVE_SLOW
  assert(role.tx.core.durable.owed&LP_OWED_PAIR);
#endif
  return;
 }
 struct rc8_correction expected;assert(rc8_export_event(&expected,&role.tx.maps[0],1,
  (struct rc8_event){.tick=queued.tick,.capture_epoch=7,.kind=RC8_EVENT_TX_START}));
 uint64_t anchor=role.tx.maps[0].anchor_tick;
 for(unsigned n=0;n<5;n++){
  struct rc8_export m=source();m.anchor_tick=now_us*16;m.anchor_ns+=now_us*1000;m.map_generation+=n+1;
  r2_lora_source_publish(&m);assert(role.tx.pinned&&role.tx.maps[1].anchor_tick==anchor);now_us+=2000;
 }
 step();assert(owner.stage==2&&role.tx.sync_ns==expected.event_ns&&!role.tx.pinned);
#endif
}
static void drain_test(unsigned mode){
 prepare_tx();assert(owner.active&&!txs);
#ifdef R2_LORA_PASSIVE_SLOW
 assert(PASSIVE_DRAIN_US==UINT64_C(2000000));
#else
 assert(PASSIVE_DRAIN_US==RC8_DRAIN_US);
#endif
 if(mode==34){
  launch();uint64_t slot=role.tx.reservation.slot_us;done(slot,LP_META);
  now_us=slot+LP_SYNC_OFFSET_US;step();done(slot+LP_SYNC_OFFSET_US,LP_SYNC);
  assert(role.tx.core.durable.pending==2);uint64_t charge=lp_charged(&role.tx.core.durable,now_us);
  r2_lora_source_withdraw();uint64_t deadline=stop_deadline;
  now_us=deadline-PASSIVE_DRAIN_US/2;step();
  assert(!atomic_get(&faulted)&&!r2_lora_close_result()&&!role.tx.core.durable.fh_disabled);
  assert(role.tx.core.durable.cursor==2&&!role.tx.core.durable.pending);
  assert(lp_charged(&role.tx.core.durable,now_us)==charge);
#ifdef R2_LORA_PASSIVE_SLOW
  assert(role.tx.core.durable.owed&LP_OWED_PAIR);
#endif
  return;
 }
 r2_lora_request(false);uint64_t started=now_us,deadline=stop_deadline;
 assert(deadline==started+PASSIVE_DRAIN_US&&worker_priority()==DRAIN_PRIORITY);
 if(mode==28){
  now_us=started+PASSIVE_DRAIN_US/4;r2_lora_request(false);r2_lora_stop();
  assert(stop_deadline==deadline&&r2_diag_scratch.lora.drain_deadline_us==deadline);
  now_us=started+PASSIVE_DRAIN_US/2;
#ifdef R2_LORA_PASSIVE_SLOW
  assert(now_us-started>RC8_DRAIN_US); /* Delayed ACK exceeds old200ms guard. */
#endif
 }else if(mode==29)now_us=deadline-1;
 else if(mode==30||mode==31)now_us=deadline;
 else if(mode==32||mode==33)now_us=deadline+1;
 else assert(false);
 if(mode==31){
  for(unsigned missing=1;missing<=8;missing<<=1){
   if(missing!=1){prepare_tx();r2_lora_request(false);now_us=stop_deadline;}
   stop_ack_missing=missing;step();
   assert(!atomic_get(&closed)&&(atomic_get(&faulted)&2)&&r2_diag_scratch.lora.terminal);
   assert(r2_lora_close_result()==-EIO);
  }
  return;
 }
 step();assert(atomic_get(&closed)&&!stop_deadline&&!atomic_get(&faulted)&&!r2_diag_scratch.lora.terminal);
 assert(r2_diag_scratch.lora.drain_ack_us==now_us-2000);
 if(mode==28||mode==29){assert(!r2_lora_close_result()&&!r2_diag_scratch.lora.drain_late_count);return;}
 assert(r2_lora_close_result()==-ETIMEDOUT&&r2_diag_scratch.lora.drain_late_count==1);
 r2_lora_request(false);step();assert(r2_lora_close_result()==-ETIMEDOUT); /* OFF cannot erase failed closure. */
 if(mode==33){
  uint32_t old_generation=generation;r2_lora_request(true);step();
  assert(owner.active&&generation>old_generation&&!r2_diag_scratch.lora.drain_late&&!txs);
  r2_lora_request(false);uint64_t recovery_deadline=stop_deadline;step();
  assert(!r2_lora_close_result()&&atomic_get(&closed)&&r2_diag_scratch.lora.drain_ack_us<recovery_deadline);
  assert(r2_diag_scratch.lora.drain_late_count==1&&!txs); /* Retain historical failure evidence. */
 }
}
#ifdef R2_LORA_PASSIVE_SLOW
static unsigned read_publications;static bool read_pps;
static void publish_during_read(void){
 struct rc8_export m=role.tx.maps[0];
 if(read_pps){++now_us;m.anchor_tick=now_us*16;m.anchor_ns=UINT64_C(1000000000000)+now_us*1000;++m.map_generation;}
 r2_lora_source_publish(&m);
 assert(read_publications);if(--read_publications)projection_hook=publish_during_read;
}
static void source_read_test(unsigned mode){
 prepare_tx();uint64_t ns,boot;uint32_t ep;struct r2_capture limit;
 uint32_t revision=(uint32_t)source_revision;
 if(mode==47||mode==49){
  read_pps=mode==49;read_publications=1;projection_hook=publish_during_read;
  assert(source_now(&ns,&ep,&boot,&limit,NULL)==SOURCE_READY&&source_observation.tx_reads[3]==(mode==49?1u:0u)&&!source_observation.tx_reads[1]&&!source_observation.tx_reads[2]);
 }else if(mode==48){
  uint64_t slot=role.tx.core.durable.next_slot_us;now_us=slot-PRECHARGE_LEAD_US;step();assert(owner.stage==3&&owner.prepared_kind==LP_META);
  uint64_t charge=lp_charged(&role.tx.core.durable,now_us);uint32_t serial=role.tx.core.durable.serial;
  read_pps=true;read_publications=3;projection_hook=publish_during_read;step();
  assert(source_observation.tx_reads[1]==1&&source_observation.tx_reads[3]==3&&owner.stage==3&&owner.prepared_kind==LP_META&&!owner.stopping&&!txs);
  assert(role.tx.core.durable.serial==serial&&role.tx.core.durable.pending==1&&lp_charged(&role.tx.core.durable,now_us)==charge);
  now_us=slot;step();assert(txs==1&&owner.tx_kind==LP_META&&!owner.late);
 }else if(mode==50||mode==51){
  uint64_t slot=role.tx.core.durable.next_slot_us;now_us=slot-PRECHARGE_LEAD_US;step();assert(owner.stage==3&&owner.prepared_kind==LP_META);
  uint64_t charge=lp_charged(&role.tx.core.durable,now_us);uint32_t serial=role.tx.core.durable.serial;
  for(unsigned n=0;n<500;n++){
   read_pps=mode==51;read_publications=1;projection_hook=publish_during_read;step();
   assert(!owner.stopping&&!stop_deadline&&!atomic_get(&faulted)&&role.tx.core.durable.serial==serial&&role.tx.core.durable.pending==1);
   assert(lp_charged(&role.tx.core.durable,now_us)==charge);
  }
  now_us=slot;step();assert(txs==1&&owner.tx_kind==LP_META&&(mode==51?source_observation.tx_reads[3]>=500:source_observation.tx_reads[3]==0)&&!source_observation.tx_reads[1]&&!source_observation.tx_reads[2]);
 }else if(mode==52){
  read_pps=true;read_publications=1;capture_now_hook=publish_during_read;
  assert(source_now(&ns,&ep,&boot,&limit,NULL)==SOURCE_READY&&source_observation.tx_reads[3]==1&&!source_observation.tx_reads[2]);
 }else if(mode==53){
  projection_hook=r2_lora_source_withdraw;
  assert(source_now(&ns,&ep,&boot,&limit,NULL)==SOURCE_ABSENT&&source_observation.tx_reads[2]==1&&!(flags&SOURCE_PRESENT));return;
 }else if(mode==54){
  role.tx.maps[0].until_tick=now_us*16;
  assert(source_now(&ns,&ep,&boot,&limit,NULL)==SOURCE_ABSENT&&source_observation.tx_reads[2]==1);return;
 }else if(mode==55){
  read_pps=true;read_publications=3;projection_hook=publish_during_read;
  assert(r2_lora_utc_request(1,0,false)==-EAGAIN&&utc_job.reason==UTC_WAIT_SOURCE&&!utc_job.admitted);
  assert(source_observation.tx_reads[1]==1&&!source_observation.tx_reads[2]);
 }
 assert((flags&SOURCE_PRESENT)&&source_revision==revision);
 char json[4096];assert(r2_lora_diagnostics_json(json,sizeof json,0)>0&&strstr(json,"\"source_read\":["));
 assert(r2_lora_json(json,sizeof json,0)>0&&strstr(json,"\"source_read\":["));
 printf("PASS source read mode=%u ready=%u transient=%u absent=%u races=%u\n",mode,source_observation.tx_reads[0],source_observation.tx_reads[1],source_observation.tx_reads[2],source_observation.tx_reads[3]);
}
static unsigned fence_stage,fence_action,fence_txs;static bool fence_fired;
static void prepared_fence(void){
 if(fence_fired||owner.stage!=fence_stage)return;
 fence_fired=true;fence_txs=txs;
 if(fence_action==0)r2_lora_source_withdraw();
 else{struct rc8_export m=source();
  if(fence_action==1){++m.scope.source_epoch;++m.map_generation;}
  else{m.until_tick=now_us*16+16000;m.expires_ns=UINT64_C(1000000000000)+now_us*1000+1000000;}
  r2_lora_source_publish(&m);
 }
}
static void prepared_fence_test(unsigned mode){
 for(unsigned phase=1;phase<=3;phase++){
  fake_encode_hook=fake_prepare_hook=NULL;fake_encode_delay_us=fake_spi_delay_us=fake_launch_delay_us=0;prepare_tx();
  fence_stage=phase;fence_action=mode==45?2:(mode-39)%2;fence_fired=false;fence_txs=0;
  fake_encode_delay_us=75000;fake_spi_delay_us=200000;
  if(mode<=40)fake_encode_hook=prepared_fence;
  else if(mode<=42)fake_prepare_hook=prepared_fence;
  uint64_t finish=now_us+20000000;
  while(now_us<finish){
   if(owner.tx_kind&&!have_event&&now_us>=sent_at_us+lp_duration(owner.tx_kind)-10000){
    queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+lp_duration(owner.tx_kind)-10000};have_event=true;
   }
   if(mode>=43&&owner.prepared_kind&&owner.stage==phase&&!fence_fired){
    now_us=phase==3?role.tx.reservation.slot_us:phase==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;
    prepared_fence();
   }
   step();if(fence_fired&&atomic_get(&closed))break;
  }
  assert(fence_fired&&atomic_get(&closed)&&txs==fence_txs&&!owner.prepared_kind&&!role.tx.core.durable.pending);
  assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled&&!r2_lora_close_result());
 }
 puts("PASS prepared META/SYNC/FOLLOW fenced before launch; source/context/shortening preserved");
}
static void receive_window_test(void){
 struct lp_rx r={.cached=true,.hint=true,.hint_cursor=2,.hint_us=1000000,.observed_deadline_us=10000000};
 lp_rx_expire(&r,1500000);assert(r.hint); /* Retention includes CRC and bounded owner processing. */
 assert(!lp_rx_sync(&r,2,20000000,7,1000000+LP_HEADER_WINDOW_US+1,1500000+LP_HEADER_WINDOW_US));
 r.hint=true;r.hint_cursor=2;r.hint_us=1000000;
 assert(lp_rx_sync(&r,2,20000000,7,1000000+LP_HEADER_WINDOW_US,1500000+LP_HEADER_WINDOW_US));
 struct lp_packet p={.kind=LP_FOLLOW,.schedule=LP_SLOW,.cursor=2,.utc_ns=UINT64_C(1000000000000)};
 assert(!lp_rx_follow(&r,&p,NULL,7,1000000+LP_HEADER_WINDOW_US+LP_PENDING_US)); /* One original pending deadline, exclusive expiry. */
 r.hint=true;r.hint_cursor=2;r.hint_us=1000000;
 assert(!lp_rx_sync(&r,2,20000000,7,1000000,1000000+LP_SYNC_PROCESS_US+1));
 r.hint=true;r.hint_cursor=2;r.hint_us=1000000;
 assert(lp_rx_sync(&r,2,20000000,7,1000000,1000000+LP_SYNC_PROCESS_US));uint64_t original=r.pending_deadline_us;
 for(unsigned i=0;i<100;i++){lp_rx_expire(&r,original-1);assert(r.pending&&r.pending_deadline_us==original);}lp_rx_expire(&r,original);assert(!r.pending);
 r.hint=r.prediction=true;r.hint_cursor=2;r.hint_us=5000000;
 assert(!lp_rx_sync(&r,2,20000000,7,5000000+LP_PREDICT_HEADER_WINDOW_US+1,5000000+LP_PREDICT_HEADER_WINDOW_US+1));
 r.hint=r.prediction=true;r.hint_cursor=2;r.hint_us=5000000;
 assert(lp_rx_sync(&r,2,20000000,7,5000000-LP_PREDICT_HEADER_WINDOW_US,5000000-LP_PREDICT_HEADER_WINDOW_US+LP_SYNC_PROCESS_US)&&!r.prediction);
 r.pending=false;r.hint=r.prediction=true;r.hint_us=1000000;lp_rx_expire(&r,1000000+LP_PREDICT_HINT_RETIRE_US);assert(r.hint);lp_rx_expire(&r,1000000+LP_PREDICT_HINT_RETIRE_US+1);assert(!r.hint&&!r.prediction);
 r.hint=true;r.hint_cursor=2;r.hint_us=3000000;lp_rx_expire(&r,3000000+LP_HINT_RETIRE_US+1);assert(!r.hint);
 puts("PASS retained hint keeps selected physical and pending deadline fences");
}
static void auto_complete(void){
 if(owner.tx_kind&&!have_event&&now_us>=sent_at_us+lp_duration(owner.tx_kind)-10000){
  queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+lp_duration(owner.tx_kind)-10000};have_event=true;
 }
}
static void fresh_ble(uint64_t room){
 struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
 m.from_tick=m.anchor_tick=now_us*16;m.until_tick=(now_us+room)*16;m.anchor_ns+=now_us*1000;
 m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;m.map_generation=(uint32_t)(now_us/1000);m.mode=RC8_MAP_DELIVERED;
 r2_lora_source_publish(&m);
}
static void withdraw_at_launch(void){fake_launch_hook=NULL;r2_lora_source_withdraw();fake_launch_canceled=true;}
static void cancellation260_test(unsigned mode){
 prepare_tx();
 if(mode>=68){launch();done(role.tx.reservation.slot_us,LP_META);if(mode==69){now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;step();done(sent_at_us,LP_SYNC);}}
 else{now_us=role.tx.core.durable.next_slot_us-PRECHARGE_LEAD_US;step();}
 uint64_t charge=lp_charged(&role.tx.core.durable,now_us);unsigned count=txs;
 if(mode==66)fake_scheduling_abort=true;else fake_launch_hook=withdraw_at_launch;
 now_us=owner.stage==3?role.tx.reservation.slot_us:owner.stage==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;step();
 assert(owner.stopping&&!atomic_get(&faulted)&&role.tx.core.durable.pending);step();
 unsigned cursor=mode>=68?2:0;
 assert(atomic_get(&closed)&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.pending&&txs==count&&role.tx.core.durable.cursor==cursor);
 assert(lp_charged(&role.tx.core.durable,now_us)==charge&&role.tx.core.durable.owed&&!r2_lora_close_result());
 struct rc8_export m=source();m.anchor_tick=now_us*16;m.anchor_ns+=now_us*1000;++m.map_generation;r2_lora_source_publish(&m);uint64_t finish=now_us+30000000;
 while(now_us<finish&&!owner.sent[LP_FOLLOW-1]){auto_complete();step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);}
 assert(owner.sent[LP_FOLLOW-1]==1&&role.tx.core.durable.cursor==cursor+2&&!role.tx.core.durable.owed);
 puts("PASS control scheduling abort / in-launch source withdrawal closes and recovers without clearing charge");
}
static void timers260_test(unsigned mode){
 fake_encode_delay_us=fake_spi_delay_us=fake_launch_delay_us=0;fake_encode_hook=fake_prepare_hook=fake_launch_hook=NULL;
 if(mode==57){
  prepare_tx();fake_encode_delay_us=fake_spi_delay_us=200000;unsigned failures=0;uint64_t finish=now_us+30000000,charge=0;
  while(now_us<finish&&!owner.sent[LP_FOLLOW-1]){
   if(failures<3&&owner.stage==3&&owner.prepared_kind&&now_us>=role.tx.reservation.slot_us){reject_next_tx=true;++failures;}
   uint32_t refunds=owner.no_rf_refunds;auto_complete();step();
   if(owner.no_rf_refunds!=refunds){assert(retry_at_us>=now_us-2000+PRECHARGE_LEAD_US+ADMISSION_RETRY_US);assert(!lp_charged(&role.tx.core.durable,now_us));}
   assert(!owner.late&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  charge=lp_charged(&role.tx.core.durable,now_us);
  assert(failures==3&&owner.no_rf_refunds==3&&owner.sent[LP_META-1]==1&&owner.sent[LP_FOLLOW-1]==1&&role.tx.core.durable.serial==1);
  assert(charge==lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW));
  puts("PASS three refused launches recover with full400ms preparation and no extra charge");return;
 }
 if(mode==58||mode==59){
  prepare_tx();fresh_ble(1500000);step();step();step();step();
  fake_encode_delay_us=fake_spi_delay_us=200000;fake_launch_delay_us=200000;
  bool prepared=false,attempted=false;uint64_t finish=now_us+20000000;
  while(now_us<finish&&!owner.sent[LP_FOLLOW-1]){
   if(mode==58||(!attempted&&!(owner.prepared_kind==LP_META&&now_us+2000>=role.tx.reservation.slot_us)))fresh_ble(1500000);
   if(mode==59&&owner.prepared_kind==LP_META&&now_us+2000>=role.tx.reservation.slot_us&&!attempted){fresh_ble(1200000);attempted=true;}
   auto_complete();step();
   if(owner.prepared_kind==LP_META){prepared=true;assert(!txs);}
   if(mode==59&&attempted&&owner.stopping){uint64_t charge=lp_charged(&role.tx.core.durable,now_us);step();assert(!txs&&atomic_get(&closed)&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled&&charge>0);puts("PASS original BLE grant expiring during final launch prevents RF and closes conservatively");return;}
   assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  assert(mode==58&&prepared&&owner.sent[LP_META-1]==1&&owner.sent[LP_SYNC-1]==1&&owner.sent[LP_FOLLOW-1]==1&&!owner.refused&&!owner.late);
  assert(role.tx.scope.kind==RC8_SELECTED_BLE&&role.tx.scope.root==3&&role.tx.scope.parent==3);
  puts("PASS fresh original1.5s BLE maps stage1s early and complete META/SYNC/FOLLOW");return;
 }
 if((mode>=60&&mode<=64)||mode==70){
  prepare_tx();launch();done(role.tx.reservation.slot_us,LP_META);
  if(mode!=60){now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;step();done(sent_at_us,LP_SYNC);assert(role.tx.core.durable.pending==2);}
  uint64_t charge=lp_charged(&role.tx.core.durable,now_us);uint32_t seed=role.tx.core.durable.seed;
  if(mode==64)unknown_tx_error=true;else if(mode==70)fake_launch_delay_us=LP_FOLLOW_LATEST_US-LP_FOLLOW_OFFSET_US+100000;else fake_scheduling_abort=true;
  now_us=owner.stage==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;step();unknown_tx_error=false;
  assert(owner.stopping);if(mode==70){assert(rejected.point==5&&!rejected.no_rf);fake_launch_delay_us=0;}uint64_t deadline=stop_deadline;
  if(mode==62)now_us=deadline;
  if(mode==63){stop_ack_missing=2;now_us=deadline;}
  step();
  if(mode>=62&&mode<=64){assert(role.tx.core.durable.fh_disabled||atomic_get(&faulted));assert(!owner.sent[LP_FOLLOW-1]);puts("PASS failed/late/unknown closure cannot recover FHSS");return;}
  assert(atomic_get(&closed)&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.pending&&role.tx.core.durable.cursor==2);
  assert(role.tx.core.durable.seed==seed&&lp_charged(&role.tx.core.durable,now_us)==charge&&(role.tx.core.durable.owed&LP_OWED_PAIR));
  assert(role.tx.core.durable.channel_end_ms[lp_channel(role.tx.core.order,0)]&&role.tx.core.durable.channel_end_ms[lp_channel(role.tx.core.order,1)]);
  uint64_t finish=now_us+30000000;
  while(now_us<finish&&!owner.sent[LP_FOLLOW-1]){auto_complete();step();assert(!role.tx.core.durable.fh_disabled&&!atomic_get(&faulted));}
  assert(owner.sent[LP_FOLLOW-1]==1&&role.tx.core.durable.cursor==4&&role.tx.core.durable.seed==seed&&!role.tx.core.durable.owed&&!r2_diag_scratch.lora.drain_late);
  assert(lp_charged(&role.tx.core.durable,now_us)==charge+lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW));
  printf("PASS healthy %s scheduling abort retains debit/hops/debt and autonomously recovers\n",mode==60?"SYNC":"FOLLOW");return;
 }
 assert(mode==65);boot_clear();struct rc8_export m=source();r2_lora_source_publish(&m);step();
 now_us=owner.quiet_until_us+800000;uint64_t before=now_us;step();
 assert(role.tx.loaded&&role.tx.core.durable.next_slot_us>=before+PRECHARGE_LEAD_US&&!role.tx.core.durable.pending);
 puts("PASS first slow grid opportunity retains full1s preparation lead");
}
static unsigned completion_races;
static void race_completion(void){
 /* A genuine replacement must still fence projection, unlike a no-op
  * duplicate. Advance the publication capture and formula independently. */
 struct rc8_export m=role.tx.maps[0];++now_us;m.from_tick=m.anchor_tick=now_us*16;
 m.anchor_ns=UINT64_C(1000000000000)+now_us*1000;++m.map_generation;
 r2_lora_source_publish(&m);assert(completion_races);if(--completion_races)projection_hook=race_completion;
}
static void projection261_test(unsigned mode){
 if(mode==81||mode==86){prepare_tx();launch();done(role.tx.reservation.slot_us,LP_META);now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;step();
  uint64_t stale=mode==86?role.tx.edge.tick:role.tx.edge.tick-16;now_us=sent_at_us+lp_duration(LP_SYNC)-10000+(mode==86?2000001:0);queued=(struct r2_lora_hw_event){.tick=stale,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;step();
  assert(role.tx.core.durable.fh_disabled&&!owner.sent[LP_SYNC-1]&&!owner.sent[LP_FOLLOW-1]&&!sync_map[0]);puts(mode==86?"PASS first hardware DONE older than2s cannot enter retained projection":"PASS stale prior DONE cannot replace the new packet capture floor or enter projection retry");return;
 }
 prepare_tx();launch();done(role.tx.reservation.slot_us,LP_META);now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;
 if(mode==85){struct rc8_export m=source();m.from_tick=m.anchor_tick=now_us*16;m.anchor_ns+=now_us*1000;m.until_tick=(now_us+3000000)*16;m.expires_ns=UINT64_C(1000000000000)+(now_us+3000000)*1000;++m.map_generation;r2_lora_source_publish(&m);}
 step();
 uint64_t start=sent_at_us;unsigned consumed=fake_done_consumed;queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=start+lp_duration(LP_SYNC)-10000};have_event=true;
 now_us=queued.end_local_us;uint64_t end=now_us,charge=lp_charged(&role.tx.core.durable,now_us);
 completion_races=mode==73?6:3;projection_hook=race_completion;step();
 assert(owner.stage==4&&owner.tx_kind==LP_SYNC&&role.tx.pinned&&role.tx.edge.tick==start*16&&!have_event&&role.tx.core.durable.pending==1&&owner.sent[LP_SYNC-1]==1&&sync_map[1]==1&&fake_done_consumed==consumed+1);
 if(mode==85){projection_hook=NULL;now_us=end+2100000;step();assert(owner.stage==2&&sync_map[0]==1&&!owner.missing_map&&owner.sent[LP_SYNC-1]==1&&fake_done_consumed==consumed+1);now_us=end+LP_FOLLOW_OFFSET_US;step();done(sent_at_us,LP_FOLLOW);assert(owner.sent[LP_FOLLOW-1]==1&&!role.tx.core.durable.pending);puts("PASS deferred original validated SYNC projects after2.1s without a second hardware DONE or renewed authority");return;}
 if(mode==73){step();assert(owner.stage==4&&sync_map[1]==2);step();assert(fake_done_consumed==consumed+1&&owner.stage==2&&owner.sent[LP_SYNC-1]==1&&sync_map[0]==1&&!owner.missing_map&&!sync_map[2]&&!sync_map[3]);
  now_us=end+LP_FOLLOW_OFFSET_US;step();done(sent_at_us,LP_FOLLOW);assert(owner.sent[LP_FOLLOW-1]==1&&role.tx.core.durable.cursor==2&&role.tx.stream==1&&lp_charged(&role.tx.core.durable,now_us)==charge);
  puts("PASS six projection races defer across polls, preserve captured original map, and complete FOLLOW once");return;}
 projection_hook=NULL;
 if(mode==74){role.tx.maps[1].expires_ns=UINT64_C(1000000000000)+start*1000;++map_version;}
 else if(mode==75)r2_lora_source_withdraw();
 else if(mode==76)++fake_capture_epoch;
 else if(mode==77){struct rc8_export m=source();++m.scope.source_epoch;++m.map_generation;r2_lora_source_publish(&m);}
 else if(mode==78)now_us=end+LP_FOLLOW_LATEST_US-4000;
 else if(mode==87)--audits[0].tick;
 else if(mode==88)++audits[0].end_us;
 else if(mode==89)published.sampled_us=now_us+1;
 else{assert(mode==79);struct rc8_export m=role.tx.maps[0];m.until_tick=start*16;r2_lora_source_publish(&m);}
 step();if(owner.stopping)step();
 assert(owner.sent[LP_SYNC-1]==1&&!owner.sent[LP_FOLLOW-1]&&fake_done_consumed==consumed+1&&lp_charged(&role.tx.core.durable,now_us)==charge);
 assert(atomic_get(&closed)||atomic_get(&faulted)||role.tx.core.durable.fh_disabled);
 if(mode==78)assert(sync_map[3]==1&&owner.missing_map==1&&!role.tx.core.durable.fh_disabled);
 if(mode==74)assert(sync_map[2]==1&&role.tx.core.durable.fh_disabled);
 fake_capture_epoch=7;puts("PASS deferred projection retains original expiry/withdrawal/epoch/context/latest-FOLLOW fences");
}
static void rendezvous261_test(unsigned mode){
 prepare_tx();fake_encode_delay_us=fake_spi_delay_us=mode>=92?400000:100000;fake_launch_delay_us=mode>=92?248000:200000;fake_config_delay_us=mode>=92?400000:0;
 struct frame {uint64_t start,end;unsigned kind,channel;size_t n;uint8_t bytes[LP_META_BYTES];} f[64];unsigned count=0,seen=0;bool injected=false;uint64_t closure=0,recovery_start=0,old_control=0;unsigned original_stream=0;
 uint64_t finish=now_us+UINT64_C(620000000);
 while(now_us<finish){
  if(!injected&&(((mode==80||mode==84||mode==92)&&owner.stage==1&&owner.sent[LP_META-1]+owner.sent[LP_DISC-1]==5)||(mode!=80&&mode!=84&&mode!=92&&owner.stage==2&&owner.sent[LP_SYNC-1]==5))){
   injected=true;original_stream=role.tx.stream;old_control=role.tx.reservation.slot_us;
   if(mode==84||mode==92){now_us=old_control+LP_SYNC_OFFSET_US-PAIR_PREPARE_LEAD_US;if(mode==92){struct rc8_export m=source();m.from_tick=m.anchor_tick=now_us*16;m.until_tick=(now_us+3000000)*16;m.anchor_ns+=now_us*1000;m.expires_ns=m.anchor_ns+UINT64_C(3000000000);m.map_generation=(uint32_t)(now_us/1000);r2_lora_source_publish(&m);}step();assert(owner.prepared_kind==LP_SYNC);now_us=old_control+LP_SYNC_OFFSET_US+LP_LAUNCH_LATE_US+1;}
   else if(mode==93)now_us=owner.last_sync_us+LP_FOLLOW_LATEST_US-4000;
   else if(mode==71)fake_launch_delay_us=LP_FOLLOW_LATEST_US-LP_FOLLOW_OFFSET_US+100000;
   else{struct rc8_export m=role.tx.maps[0];if(mode==80)r2_lora_source_withdraw();++m.scope.source_epoch;++m.map_generation;r2_lora_source_publish(&m);}
  }
  if(mode>=92){
   if(owner.stopping)now_us=stop_deadline-1000;
   else if(owner.prepared_kind&&!owner.tx_kind){uint64_t due=owner.stage==3?role.tx.reservation.slot_us:owner.stage==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;
    if(now_us<due&&due-now_us<=2000)now_us=due+LP_LAUNCH_LATE_US;
    fake_launch_delay_us=owner.stage==2?999000:248000;}
   struct rc8_export m=source();m.from_tick=m.anchor_tick=now_us*16;m.until_tick=(now_us+3000000)*16;m.anchor_ns+=now_us*1000;m.expires_ns=m.anchor_ns+UINT64_C(3000000000);m.map_generation=(uint32_t)(now_us/1000);r2_lora_source_publish(&m);
  }
  auto_complete();step();
  if(injected&&owner.stopping&&mode<92)fake_launch_delay_us=200000;
  if(injected&&atomic_get(&closed)&&!closure)closure=now_us-2000;
  if(txs!=seen){assert(txs==seen+1&&count<96);seen=txs;
   f[count]=(struct frame){sent_at_us,sent_at_us+lp_duration(owner.tx_kind)-10000,owner.tx_kind,tuning_channel,sent_len,{0}};memcpy(f[count].bytes,sent,sent_len);
   if(injected&&closure&&owner.tx_kind==LP_META&&!recovery_start)recovery_start=sent_at_us;
   ++count;
  }
  assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
 }
 (void)old_control; /* Also compiled against the intentionally old retry owner. */
 assert(injected&&closure&&recovery_start&&owner.sent[LP_FOLLOW-1]>=10&&role.tx.stream==original_stream+(mode!=71&&mode!=84&&mode<92));
#ifndef R2_QA_OLD_RENDEZVOUS
 assert(recovery_start>=closure+LP_PENDING_US+LP_RX_TUNE_LEAD_US);
 assert(recovery_start>=old_control+LP_SYNC_OFFSET_US+LP_LAUNCH_PAD_US+LP_HINT_RETIRE_US+LP_RX_TUNE_LEAD_US+10000);
#endif
 if(mode==84)assert(tx_due[1][LP_SYNC-1]>LP_LAUNCH_LATE_US);fake_encode_delay_us=fake_spi_delay_us=fake_launch_delay_us=0;boot_clear();init(false);fake_config_delay_us=mode>=92?1000000:200000;now_us=f[0].start-2000000;step();
 unsigned index=0,rx_rearms=rearms,metas=0,lost_meta=0;bool checked=false,visible=false;uint64_t ready=now_us,last_pair=0,max_pair_gap=0;
 while(index<count){struct frame *p=&f[index];
  if(!checked&&now_us>=p->start){checked=true;visible=owner.listening&&ready<=p->start&&tuning_phy==(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)&&(p->kind<=LP_META||tuning_channel==p->channel);}
  if(checked&&visible&&now_us<p->end&&(tuning_phy!=(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)||(p->kind>=LP_SYNC&&tuning_channel!=p->channel)))visible=false;
  uint64_t event=p->kind==LP_SYNC?p->start+75000:p->end;
  uint64_t age=mode>=92?LP_SYNC_PROCESS_US:500000;uint64_t processing=p->kind==LP_SYNC?age-(p->end-event):age;
  if(checked&&now_us>=p->end+(visible?processing:0)){
   if(visible){now_us=p->end+processing;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)p->n};memcpy(queued.frame,p->bytes,p->n);have_event=true;if(p->kind==LP_META)++metas;}
   else if(p->kind==LP_META)++lost_meta;
   ++index;checked=false;
  }
  uint32_t accepted=role.rx.accepted;uint64_t before_step=now_us;step();if(role.rx.accepted!=accepted){if(last_pair&&role.rx.last_us-last_pair>max_pair_gap)max_pair_gap=role.rx.last_us-last_pair;last_pair=role.rx.last_us;}if(rearms!=rx_rearms){rx_rearms=rearms;ready=now_us-2000;if(checked&&before_step<p->end&&now_us>p->start&&(tuning_phy!=(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)||(p->kind>=LP_SYNC&&tuning_channel!=p->channel)))visible=false;}
#ifdef R2_QA_OLD_RENDEZVOUS
  if(lost_meta){assert((mode==72||mode==80||mode==84)&&role.rx.scope.stream==1);puts("PASS old immediate recovery META is physically missed while RX retains the prior FHSS window");return;}
#else
  assert(!lost_meta&&!owner.bad&&!role.rx.rejected&&!atomic_get(&faulted));
#endif
 }
 assert(metas>=3&&role.rx.accepted>=10&&role.rx.trained&&role.rx.scope.stream==original_stream+(mode!=71&&mode!=84&&mode<92));
 assert(role.rx.missing==1&&max_pair_gap>LP_BEAT_US&&max_pair_gap<LP_HOLD_US);fake_config_delay_us=0;
 printf("PASS PHY-aware recovery mode=%u META=%u accepted=%u stream=%u missing=%u trained=true recovery_gap_us=%llu hold_us=%llu\n",mode,metas,role.rx.accepted,role.rx.scope.stream,role.rx.missing,(unsigned long long)max_pair_gap,(unsigned long long)LP_HOLD_US);
}
/* Combined engineering allowances: real codec/owner/core, synthetic bounded
 * SPI/SetTx/queue boundaries, and PHY visibility before Header/RxDone. */
static void engineering263_test(unsigned mode){
 prepare_tx();fake_encode_delay_us=fake_spi_delay_us=400000;fake_config_delay_us=400000;
 struct bounded_frame {uint64_t start,end;unsigned kind,channel;size_t n;uint8_t bytes[LP_META_BYTES];} f[96];
 unsigned count=0,seen=0;uint64_t finish=now_us+UINT64_C(800000000),max_gap=0,last_sync=0;
 while(now_us<finish){
  struct rc8_export m=source();m.from_tick=m.anchor_tick=now_us*16;m.until_tick=(now_us+3000000)*16;m.anchor_ns+=now_us*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+UINT64_C(3000000000);m.map_generation=(uint32_t)(now_us/1000);r2_lora_source_publish(&m);
  if(owner.prepared_kind&&!owner.tx_kind){
   uint64_t due=owner.stage==3?role.tx.reservation.slot_us:owner.stage==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;
   /* Include a late META after an on-time preceding pair. DISC phases no
    * longer alternate with META, so slot parity alone misses this boundary. */
   bool late=mode==82||(role.tx.core.durable.slot%10==1)||
    (role.tx.core.durable.slot%4==2&&role.tx.core.durable.slot%10!=0);
   if(now_us<due&&due-now_us<=2000)now_us=due+(late?LP_LAUNCH_LATE_US:0);
   fake_launch_delay_us=owner.stage==2?(late?999000:2000):(late?248000:2000);
  }
  auto_complete();step();
  if(txs!=seen){assert(txs==seen+1&&count<96);seen=txs;
   f[count]=(struct bounded_frame){sent_at_us,sent_at_us+lp_duration(owner.tx_kind)-10000,owner.tx_kind,tuning_channel,sent_len,{0}};memcpy(f[count].bytes,sent,sent_len);++count;
   if(owner.tx_kind==LP_SYNC){if(last_sync&&sent_at_us-last_sync>max_gap)max_gap=sent_at_us-last_sync;last_sync=sent_at_us;}
  }
  if(owner.refused||owner.late||owner.missing_map||owner.bad||atomic_get(&faulted)||role.tx.core.durable.fh_disabled){char diagnostic[4096];r2_lora_json(diagnostic,sizeof diagnostic,0);fprintf(stderr,"%s\n",diagnostic);}
  assert(!owner.refused&&!owner.late&&!owner.missing_map&&!owner.bad&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
 }
 assert(owner.sent[LP_META-1]>=3&&owner.sent[LP_FOLLOW-1]>=13&&owner.sent[LP_SYNC-1]==owner.sent[LP_FOLLOW-1]);
 assert(max_gap<=LP_CADENCE_BOUND_US&&tx_due[1][LP_META-1]==LP_LAUNCH_LATE_US&&tx_due[1][LP_SYNC-1]==LP_LAUNCH_LATE_US&&tx_due[1][LP_FOLLOW-1]==LP_LAUNCH_LATE_US);
 unsigned pairs=owner.sent[LP_FOLLOW-1];char json[4096];assert(r2_lora_diagnostics_json(json,sizeof json,0)>0&&strstr(json,"\"tx_due_us\":[[")&&strstr(json,"\"cadence_bound_us\":32510000"));
 fake_encode_delay_us=fake_spi_delay_us=fake_launch_delay_us=fake_config_delay_us=0;boot_clear();init(false);fake_config_delay_us=1000000;now_us=f[0].start-2000000;step();
 unsigned index=0,rx_rearms=rearms,disc_seen=0;bool checked=false,visible=false;uint64_t ready=now_us;
 while(index<count){struct bounded_frame *p=&f[index];
  if(!checked&&now_us>=p->start){checked=true;visible=owner.listening&&ready<=p->start&&tuning_phy==(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)&&(p->kind<=LP_META||tuning_channel==p->channel);if(!visible)fprintf(stderr,"PHY index=%u kind=%u now=%llu start=%llu ready=%llu phy=%u channel=%u want=%u nextctrl=%llu hint=%d hintat=%llu pending=%d\n",index,p->kind,(unsigned long long)now_us,(unsigned long long)p->start,(unsigned long long)ready,tuning_phy,tuning_channel,p->channel,(unsigned long long)owner.next_control_us,role.rx.hint,(unsigned long long)role.rx.hint_us,role.rx.pending);assert(visible);}
  if(checked&&visible&&now_us<p->end&&(tuning_phy!=(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)||(p->kind>=LP_SYNC&&tuning_channel!=p->channel)))visible=false;
  uint64_t event=p->kind==LP_SYNC?p->start+75000:p->end;
  uint64_t process_at=p->kind==LP_SYNC?event+LP_SYNC_PROCESS_US:p->end+LP_CONTROL_PROCESS_US;
  bool dropped=p->kind==LP_DISC&&((mode==90&&disc_seen==0)||(mode==91&&disc_seen==1));
  if(checked&&now_us>=process_at){assert(visible);now_us=process_at;if(!dropped){queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)p->n};memcpy(queued.frame,p->bytes,p->n);have_event=true;}if(p->kind==LP_DISC)++disc_seen;++index;checked=false;}
  uint64_t before_step=now_us;step();if(rearms!=rx_rearms){rx_rearms=rearms;ready=now_us-2000;
   if(checked&&before_step<p->end&&now_us>p->start&&(tuning_phy!=(p->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS)||(p->kind>=LP_SYNC&&tuning_channel!=p->channel)))visible=false;
  }
  assert(!role.rx.rejected&&!role.rx.missing&&!owner.bad&&!atomic_get(&faulted));
 }
 assert(role.rx.accepted==pairs&&role.rx.trained);fake_config_delay_us=0;
 printf("PASS coherent engineering mode=%u pairs=%u max_cadence_us=%llu RXprocess=1s tune=1s callback_control_sync=248ms FOLLOW=999ms own_original_grant=3s\n",mode,pairs,(unsigned long long)max_gap);
}
static void delayed_follow_preempt(void){if(owner.stage==2)now_us+=350000;}
static void delayed_tx_test(unsigned mode){
 prepare_tx();fake_encode_delay_us=mode==35?75000:200000;fake_spi_delay_us=mode==35?150000:200000;fake_launch_delay_us=mode==56?200000:2000;fake_prepare_hook=mode==56?delayed_follow_preempt:NULL;
 struct delayed_frame {uint64_t start,end;unsigned kind;size_t n;uint8_t bytes[LP_META_BYTES];} frames[64];unsigned count=0,seen=0;
 uint64_t complete=0,start=0;unsigned pending=0;uint64_t finish=now_us+UINT64_C(800000000);
 while(now_us<finish){
  if(pending&&now_us>=complete){queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=complete};have_event=true;pending=0;}
  if(mode>=37&&owner.prepared_kind&&!owner.tx_kind){
   uint64_t due=owner.stage==3?role.tx.reservation.slot_us:owner.stage==1?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;
   if(now_us<due&&due-now_us<=2000)now_us=due+(mode==56?(owner.stage==2?0:150000):(owner.stage==3?100000:owner.stage==1?(mode==38?150000:10000):40000));
  }
  step();
  if(txs!=seen){
   assert(txs==seen+1&&!pending&&count<64);seen=txs;pending=owner.tx_kind;start=sent_at_us;complete=start+lp_duration(pending)-10000;
   struct delayed_frame *f=&frames[count++];*f=(struct delayed_frame){start,complete,pending,sent_len,{0}};memcpy(f->bytes,sent,sent_len);
  }
  assert(!owner.refused&&!owner.late&&!owner.missing_map&&!owner.bad&&!owner.storage_fail&&!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
 }
 assert(owner.sent[LP_META-1]>=3&&owner.sent[LP_FOLLOW-1]>=13&&owner.sent[LP_SYNC-1]==owner.sent[LP_FOLLOW-1]);
 unsigned pairs=owner.sent[LP_FOLLOW-1];uint64_t charge=lp_charged(&role.tx.core.durable,now_us);assert(!owner.no_rf_refunds);
 fake_prepare_hook=NULL;fake_encode_delay_us=fake_spi_delay_us=fake_launch_delay_us=0;boot_clear();init(false);fake_config_delay_us=mode==56?200000:0;now_us=frames[0].start-200000;step();unsigned index=0;
 while(index<count){
  struct delayed_frame *f=&frames[index];
  uint64_t processing=mode==56?(f->kind==LP_SYNC?500000-(f->end-f->start-75000):200000):0;
  if(now_us>=f->end+processing){
   now_us=f->end+processing;
   assert(owner.listening&&tuning_phy==(f->kind<=LP_META?R2_LORA_DTS:R2_LORA_FHSS));
   uint64_t event=f->kind==LP_SYNC?f->start+75000:f->end;
   queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)f->n};memcpy(queued.frame,f->bytes,f->n);have_event=true;++index;
  }
  bool was_pending=role.rx.pending;step();if(mode==56&&role.rx.pending&&!was_pending){assert(index<count&&frames[index].kind==LP_FOLLOW&&now_us<=frames[index].start);}
#ifdef R2_QA_OLD_HINT_EXPIRY
  if(role.rx.missing){assert(mode==38&&role.rx.missing==1&&!role.rx.accepted);puts("PASS old250ms hint expiry loses valid late SYNC before RxDone");return;}
#endif
  assert(!role.rx.rejected&&!role.rx.missing&&!owner.bad&&!atomic_get(&faulted));
 }
 assert(role.rx.accepted==pairs&&role.rx.trained&&role.rx.fit_count>=4);
 printf("PASS delayed prepare mode=%u frames=%u pairs=%u charge=%llu RX_trained=true\n",mode,count,pairs,(unsigned long long)charge);
}
static void scratch263_test(void){
 prepare_tx();assert(sizeof(union r2_diag_scratch)==512&&sizeof(struct r2_lora_watchdog_state)==416&&offsetof(union r2_diag_scratch,controller.words)==416&&offsetof(struct r2_lora_watchdog_state,tx_due_us)==376);
 uint8_t prefix[376];memcpy(prefix,&r2_diag_scratch.lora,sizeof prefix);memset(r2_diag_scratch.controller.words,0x5a,sizeof r2_diag_scratch.controller.words);
 for(unsigned kind=LP_DISC;kind<=LP_FOLLOW;kind++){due_lateness(kind,100,100+kind);due_lateness(kind,100,100+kind+10);}
 assert(!memcmp(prefix,&r2_diag_scratch.lora,sizeof prefix));for(unsigned i=0;i<24;i++)assert(r2_diag_scratch.controller.words[i]==UINT32_C(0x5a5a5a5a));
 for(unsigned i=0;i<4;i++)assert(tx_due[0][i]==i+11&&tx_due[1][i]==i+11);
 char out[4096];assert(r2_lora_json(out,sizeof out,0)>0&&strstr(out,"\"diagnostic_arm_us\":0")&&strstr(out,"\"diagnostic_charge_us\":0")&&strstr(out,"\"store_timing\":[0,0,0,0,0]")&&strstr(out,"\"tx_due_us\":[[11,12,13,14],[11,12,13,14]]"));
 r2_lora_init(1,22,1,&test_group);for(unsigned i=0;i<4;i++)assert(!tx_due[0][i]&&!tx_due[1][i]);for(unsigned i=0;i<24;i++)assert(r2_diag_scratch.controller.words[i]==UINT32_C(0x5a5a5a5a));
 puts("PASS slow due counters reuse existing32B scratch, preserve376B fault/observer prefix and24word controller tail, legacy fieldszero, bootclearswithoutNV");
}
static void staged_grant_test(unsigned mode){
 unsigned kind=LP_META+(mode-95)/4,action=(mode-95)%4;
 prepare_tx();
 if(kind!=LP_META){launch();done(role.tx.reservation.slot_us,LP_META);}
 if(kind==LP_FOLLOW){now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;step();done(sent_at_us,LP_SYNC);}
 uint64_t due=kind==LP_META?role.tx.core.durable.next_slot_us:
  kind==LP_SYNC?role.tx.reservation.slot_us+LP_SYNC_OFFSET_US:owner.last_sync_us+LP_FOLLOW_OFFSET_US;
 now_us=due-(kind==LP_META?PRECHARGE_LEAD_US:1000000);step();
 assert(owner.prepared_kind==kind&&!owner.tx_kind);
 uint64_t charge=lp_charged(&role.tx.core.durable,now_us),slot=role.tx.reservation.slot_us;
 uint32_t stream=role.tx.stream,serial=role.tx.core.durable.serial,refunds=owner.no_rf_refunds;
 unsigned count=txs;now_us=due;
 struct rc8_export m=source();m.anchor_tick=m.from_tick=now_us*16;m.anchor_ns+=now_us*1000;
 m.until_tick=(now_us+lp_duration(kind)-1)*16;++m.map_generation;r2_lora_source_publish(&m);step();
 assert(txs==count&&owner.prepared_kind==kind&&!owner.tx_kind&&prepared_size==lp_size(kind));
 assert(role.tx.reservation.slot_us==slot&&role.tx.stream==stream&&role.tx.core.durable.serial==serial);
 assert(lp_charged(&role.tx.core.durable,now_us)==charge&&owner.no_rf_refunds==refunds);
 now_us=due+100000;
 if(action==1)r2_lora_source_withdraw();
 else{
  if(action==3)now_us=kind==LP_FOLLOW?owner.last_sync_us+LP_FOLLOW_LATEST_US-4000:due+LP_LAUNCH_LATE_US+1;
  m=source();m.anchor_tick=m.from_tick=now_us*16;m.anchor_ns+=now_us*1000;++m.map_generation;
  if(action==2)++m.scope.source_epoch;
  r2_lora_source_publish(&m);
 }
 step();
 if(action==0){assert(txs==count+1&&owner.tx_kind==kind&&sent_at_us==due+100000);
  assert(lp_charged(&role.tx.core.durable,now_us)==charge&&owner.no_rf_refunds==refunds&&role.tx.stream==stream);
 }else{assert(txs==count&&!owner.tx_kind&&!atomic_get(&faulted));step();assert(txs==count);
  if(action==3){
   if(kind==LP_META)assert(owner.no_rf_refunds==refunds+1&&!lp_charged(&role.tx.core.durable,now_us));
   else assert(owner.no_rf_refunds==refunds&&lp_charged(&role.tx.core.durable,now_us)==charge);
  }
 }
 printf("PASS staged original grant kind=%u action=%u preserves token/debit/context; withdrawal and original deadlines fence RF\n",kind,action);
}
#endif
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
#ifdef R2_LORA_PASSIVE_SLOW
 if(mode>=95)staged_grant_test(mode);else if(mode==94)scratch263_test();else if(mode==82||mode==83||mode==90||mode==91)engineering263_test(mode);else if(mode==71||mode==72||mode==80||mode==84||mode==92||mode==93)rendezvous261_test(mode);else if(mode>=73)projection261_test(mode);else if(mode>=66&&mode<=69)cancellation260_test(mode);else if(mode==56)delayed_tx_test(mode);else if(mode>=57)timers260_test(mode);else if(mode>=47)source_read_test(mode);else if(mode==46)receive_window_test();else if(mode>=39)prepared_fence_test(mode);else if(mode>=35)delayed_tx_test(mode);else
#endif
 if(mode>=28)drain_test(mode);else if(mode>=21)scheduling_test(mode);else if(mode==20)replay_test();else if(mode>=9&&mode<=15)bridge_test(mode);else owner_test(mode);
 printf("PASS RAM mode=%u cadence=%u checkpoint=%zu tx=%zu rx=%zu replay=%zu owner=%zu\n",mode,LP_SCHEDULE,sizeof(struct lp_checkpoint),sizeof(struct lp_tx),sizeof(struct lp_rx),sizeof replay,sizeof role);
}
