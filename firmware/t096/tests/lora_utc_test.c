/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual owner/core and receive retunes; physical radio/time are synthetic. */
#include "lora_ram_fixture.h"
struct frame {uint64_t start,end;unsigned kind,phy,channel;size_t n;uint8_t bytes[128];};
static struct frame trace[80];static unsigned trace_n,seen_txs;static uint64_t tx_start;
static unsigned tx_kind;
static void run_step(void){
 if(owner.tx_kind&&!have_event&&now_us>=tx_start+lp_duration(tx_kind)-10000){
  queued=(struct r2_lora_hw_event){.tick=tx_start*16,.capture_epoch=fake_capture_epoch,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;
 }
 step();
 if(txs!=seen_txs){assert(txs==seen_txs+1&&trace_n<80);seen_txs=txs;tx_start=now_us-2000;tx_kind=owner.tx_kind;
  struct frame *f=&trace[trace_n++];*f=(struct frame){tx_start,tx_start+lp_duration(tx_kind)-10000,tx_kind,tuning_phy,tuning_channel,sent_len,{0}};memcpy(f->bytes,sent,sent_len);
 }
}
static void until(uint64_t end){while(now_us<end)run_step();}
static void warm(void){
 init(true);r2_lora_request(true);struct rc8_export m=source();r2_lora_source_publish(&m);
 uint64_t end=now_us+25000000;while(!owner.sent[LP_FOLLOW-1]&&now_us<end)run_step();
 assert(owner.sent[LP_FOLLOW-1]==1&&!owner.tx_kind&&!owner.stage&&!role.tx.core.durable.owed);
}
static void finish(void){uint64_t end=now_us+16000000;while(utc_active(utc_job.state)&&now_us<end)run_step();assert(utc_job.state==UTC_COMPLETED&&!utc_retry_us&&!utc_running_token);}
static void replay_frames(unsigned from){
 assert(from<trace_n);uint64_t start=trace[from].start-200000,end=trace[trace_n-1].end+300000;
 now_us=start;have_event=false;init(false);r2_lora_request(true);step();unsigned index=from,got=0;bool listening=false;
 while(now_us<end){
  if(index<trace_n){struct frame *f=&trace[index];
   if(!listening&&now_us>=f->start){assert(owner.active&&owner.listening&&tuning_phy==f->phy&&tuning_channel==f->channel);listening=true;}
   if(listening&&now_us>=f->end){if(tuning_phy!=f->phy||tuning_channel!=f->channel)fprintf(stderr,"PHY UTC mode frame=%u kind=%u now=%llu start=%llu phy=%u want=%u channel=%u want=%u hint=%d pending=%d next=%llu\n",index,f->kind,(unsigned long long)now_us,(unsigned long long)f->start,tuning_phy,f->phy,tuning_channel,f->channel,role.rx.hint,role.rx.pending,(unsigned long long)owner.next_control_us);assert(tuning_phy==f->phy&&tuning_channel==f->channel);queued=(struct r2_lora_hw_event){.tick=f->end*16,.capture_epoch=fake_capture_epoch,.generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)f->n};memcpy(queued.frame,f->bytes,f->n);have_event=true;++index;++got;listening=false;}
  }
  step();
 }
 assert(got==trace_n-from&&!owner.bad&&!role.rx.rejected&&!role.rx.missing);
 unsigned pairs=0;for(unsigned i=from;i<trace_n;i++)pairs+=trace[i].kind==LP_FOLLOW;
 assert(role.rx.accepted==pairs);printf("{\"receive_retune\":true,\"frames\":%u,\"pairs\":%u,\"missing\":0}\n",got,pairs);
}
static bool save(void *p,const struct lp_checkpoint *c){(void)c;++*(unsigned*)p;return true;}
static void core_cases(void){
 struct lp_tx t;struct lp_reservation r;assert(lp_tx_start(&t,99,1000000,60000000,1));
 uint64_t next=t.durable.next_slot_us;assert(lp_tx_reserve_utc(&t,1000000,&r,false)&&t.manual);assert(!t.durable.owed&&t.durable.next_slot_us==next&&!t.durable.slot);
 assert(lp_tx_refund_no_rf(&t,&r)&&!t.durable.owed&&t.durable.next_slot_us==next&&!t.durable.serial);
 assert(lp_tx_reserve_utc(&t,2000000,&r,false));lp_tx_control_complete(&t);lp_tx_cancel(&t,true);assert(!t.durable.owed&&t.durable.next_slot_us==next);
 assert(lp_tx_reserve_utc(&t,3000000,&r,false));lp_tx_control_complete(&t);assert(lp_tx_capture_sync(&t,3000000+LP_SYNC_OFFSET_US+lp_duration(LP_SYNC)-10000));assert(lp_tx_retire_quiescent(&t,t.sync_end_us+1000)&&!t.durable.owed);
 t.durable.owed=LP_OWED_PAIR|LP_OWED_META;t.durable.next_slot_us=8000000;
 assert(lp_tx_reserve_utc(&t,9000000,&r,true)&&!t.manual);lp_tx_control_complete(&t);assert(t.durable.owed==LP_OWED_PAIR);assert(lp_tx_capture_sync(&t,9000000+LP_SYNC_OFFSET_US+lp_duration(LP_SYNC)-10000));assert(lp_tx_capture_follow(&t,t.sync_end_us+260000+lp_duration(LP_FOLLOW)-10000)&&!t.durable.owed);
 unsigned saves=0;t.save=save;t.storage=&saves;assert(!lp_tx_reserve_utc(&t,15000000,&r,true)&&!saves);t.durable.next_slot_us=15000000;assert(!lp_tx_reserve_utc(&t,15000000,&r,true)&&!saves);
 t.manual=true;struct lp_checkpoint old=t.durable;old.pending=0;assert(lp_tx_restore(&t,&old,save,&saves,true)&&!t.manual);
 assert(lp_tx_start(&t,99,1000000,UINT64_C(3661000000),1));t.durable.minute=60;t.durable.buckets[0]=(uint32_t)LP_CAP_US;t.durable.owed=LP_OWED_PAIR;
 assert(lp_tx_reserve_utc(&t,UINT64_C(3657000000),&r,true)&&t.durable.minute==61);
 assert(lp_tx_refund_no_rf(&t,&r));assert(!lp_tx_reserve(&t,UINT64_C(3657020000),&r));assert(!lp_tx_reserve_utc(&t,UINT64_C(3657020000),&r,true));
 assert(lp_tx_reserve(&t,UINT64_C(3660000000),&r));
 puts("{\"core_cases\":16,\"status\":\"PASS\"}");
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 if(mode==45){init(true);assert(r2_lora_utc_request(1,0,true)==-ENOTSUP);char out[1024];assert(r2_lora_utc_json(out,sizeof out,0)>0&&strstr(out,"\"supported\":false"));puts("{\"fast_unsupported\":true}");return 0;}
#ifdef R2_LORA_PASSIVE_SLOW
 if(mode==9){core_cases();return 0;}warm();
 uint64_t periodic=role.tx.core.durable.next_slot_us;uint32_t slot=role.tx.core.durable.slot;
 if(mode==1||mode==4||mode==5||mode==6||mode==7){
  if(mode==4){r2_lora_request(false);while(!atomic_get(&closed))run_step();r2_lora_request(true);struct rc8_export m=source();++m.scope.source_epoch;r2_lora_source_publish(&m);assert(owner.revision!=(uint32_t)atomic_get(&source_revision));}
  assert(!r2_lora_utc_request(1,0,false));uint64_t deadline=utc_job.deadline_us;assert(!r2_lora_utc_request(1,10000,true)&&utc_job.deadline_us==deadline&&utc_job.admitted==1&&!utc_job.override);
  assert(r2_lora_utc_request(2,0,false)==-EBUSY&&utc_job.admitted==1&&utc_job.floor==2);assert(r2_lora_utc_request(2,0,false)==-EBUSY);assert(!r2_lora_utc_request(1,0,false));
  bool injected=false;uint64_t end=now_us+16000000;
  while(utc_active(utc_job.state)&&now_us<end){
   if(mode==5&&!injected&&owner.stage==3&&utc_running_token&&now_us>=role.tx.reservation.slot_us){reject_next_tx=true;injected=true;}
   if(mode==6&&owner.tx_kind==LP_SYNC)utc_job.deadline_us=now_us-1; /* TTL never cuts a started pair. */
   if(mode==7&&owner.tx_kind==LP_META)utc_job.admitted=UINT32_MAX;
   run_step();
  }
  assert(utc_job.state==UTC_COMPLETED&&!utc_retry_us&&!utc_running_token&&role.tx.core.manual);
  assert(role.tx.core.durable.next_slot_us==periodic&&role.tx.core.durable.slot==slot&&!role.tx.core.durable.owed);
  if(mode==5)assert(injected&&owner.no_rf_refunds==1&&utc_job.deferrals==1);
  if(mode==7){assert(r2_lora_utc_request(3,0,true)==-EOVERFLOW&&utc_job.admitted==UINT32_MAX);}
  else {assert(r2_lora_utc_request(1,0,false)==0&&r2_lora_utc_request(2,0,false)==-EBUSY);}
  uint64_t tx_count=txs;until(periodic-200000);assert(txs==tx_count);until(periodic+LP_FRAME_END_US);
  assert(owner.sent[LP_FOLLOW-1]==3&&!atomic_get(&faulted));
  if(mode==1||mode==5)replay_frames(0);
 }else if(mode==2||mode==3||mode==8){
  if(mode==2){now_us=periodic+100000;role.tx.core.durable.minute=now_us/LP_MINUTE_US;memset(role.tx.core.durable.buckets,0,sizeof role.tx.core.durable.buckets);role.tx.core.durable.buckets[role.tx.core.durable.minute%61]=(uint32_t)LP_CAP_US;}
  if(mode==3){now_us=UINT64_C(3658000000);role.tx.core.durable.next_slot_us=now_us-1000000;role.tx.core.durable.minute=60;memset(role.tx.core.durable.buckets,0,sizeof role.tx.core.durable.buckets);role.tx.core.durable.buckets[0]=(uint32_t)LP_CAP_US;}
  if(mode==8)now_us=periodic-1000000; /* Near future slot must coalesce, not emit an extra pair. */
  if(mode!=8){role.tx.core.durable.owed=LP_OWED_PAIR|LP_OWED_META;retry_at_us=now_us-10000;}
  assert(!r2_lora_utc_request(1,0,true));unsigned first=trace_n;finish();
  assert(!role.tx.core.manual&&!role.tx.core.durable.owed&&!retry_at_us&&utc_job.manual_serial);
  uint64_t next=role.tx.core.durable.next_slot_us,count=txs;assert(next>now_us+50000000);until(next-200000);assert(txs==count);
  if(mode==2){until(next+LP_FRAME_END_US);assert(txs==count&&owner.refused);struct lp_reservation r;assert(!lp_tx_reserve(&role.tx.core,now_us,&r));}
  else {until(next+LP_FRAME_END_US);assert(txs==count+3);replay_frames(mode==3?first:0);}
 }else if(mode>=10&&mode<38){
  unsigned target=(mode-10)%7,action=(mode-10)/7;assert(!r2_lora_utc_request(1,target?0:1000,false));uint64_t end=now_us+LP_FRAME_END_US+PRECHARGE_LEAD_US+2000000;bool applied=false;
  while(now_us<end){
   bool hit=target==0?utc_job.state==UTC_PENDING:target==1?owner.stage==3&&!owner.tx_kind:target==2?owner.tx_kind==LP_META:target==3?owner.stage==1&&!owner.tx_kind:target==4?owner.tx_kind==LP_SYNC:target==5?owner.stage==2&&!owner.tx_kind:owner.tx_kind==LP_FOLLOW;
   if(hit&&!applied){applied=true;if(action==0)r2_lora_request(false);else if(action==1){struct rc8_export m=source();++m.scope.source_epoch;r2_lora_source_publish(&m);}else if(action==2)r2_lora_control(true,true,RC8_SELECT_NONE);else ++fake_capture_epoch;}
   run_step();if(applied&&!utc_active(utc_job.state)&&!role.tx.core.durable.pending)break;
  }
  assert(applied&&utc_job.state==UTC_EXPIRED&&!utc_retry_us&&!utc_running_token&&!role.tx.core.durable.pending&&!role.tx.core.durable.fh_disabled&&!atomic_get(&faulted));
  assert(!role.tx.core.durable.owed&&!retry_at_us); /* Interrupted extra job creates no ordinary debt or unsolicited retry. */
 }else if(mode==38||mode==40){
  if(mode==38){role.tx.core.durable.buckets[role.tx.core.durable.minute%61]=(uint32_t)LP_CAP_US;}
  else for(unsigned i=0;i<64;i++)role.tx.core.durable.channel_end_ms[i]=(uint32_t)(now_us/1000);
  assert(!r2_lora_utc_request(1,0,false));uint64_t count=txs;until(utc_job.deadline_us+2000);
  assert(utc_job.state==UTC_EXPIRED&&utc_job.reason==UTC_DEADLINE&&utc_job.wait_reason==(mode==38?UTC_WAIT_BUDGET:UTC_WAIT_COOLDOWN)&&txs==count&&!utc_running_token&&!utc_retry_us);
 }else if(mode==39){
  r2_lora_source_withdraw();assert(r2_lora_utc_request(1,0,false)<0&&!utc_job.admitted&&!utc_active(utc_job.state));
  int previous=utc_job.last_result;assert(r2_lora_utc_request(1,0,true)==previous&&!utc_job.admitted);
  init(false);assert(r2_lora_utc_request(1,0,true)==-EACCES&&!utc_job.admitted);
 }else if(mode==41){
  assert(!r2_lora_utc_request(1,0,false));uint64_t end=now_us+8000000;
  while(now_us<end&&utc_active(utc_job.state)){if(owner.stage==3&&now_us>=role.tx.reservation.slot_us)unknown_tx_error=true;run_step();}
  assert(utc_job.state==UTC_EXPIRED&&utc_job.manual_serial&&utc_job.manual_stream&&utc_job.reserved_us==1643309&&atomic_get(&faulted));
 }else if(mode==42){
  assert(!r2_lora_utc_request(1,0,false));finish();init(true);assert(!utc_job.token&&!utc_job.floor&&!utc_job.admitted&&!utc_running_token&&!utc_retry_us);
 }else if(mode==44){
  uint64_t requested=now_us;assert(!r2_lora_utc_request(1,10000,false));uint64_t deadline=utc_job.deadline_us;
  assert(deadline==requested+10000000+2*PRECHARGE_LEAD_US+LP_FRAME_END_US+2000000&&deadline-requested<=31000000);
  assert(!r2_lora_utc_request(1,0,true)&&utc_job.deadline_us==deadline);
  uint64_t end=deadline;while(utc_active(utc_job.state)&&now_us<end)run_step();assert(utc_job.state==UTC_COMPLETED&&now_us<deadline);
 }else if(mode==46){
  assert(!r2_lora_utc_request(1,0,false));uint64_t end=utc_job.deadline_us;bool injected=false;
  while(utc_active(utc_job.state)&&now_us<end){
   if(!injected&&owner.stage==1&&!owner.tx_kind){now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US-PAIR_PREPARE_LEAD_US;run_step();assert(owner.prepared_kind==LP_SYNC);now_us=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US+LP_LAUNCH_LATE_US+1;injected=true;}
   run_step();
  }
  while(owner.stopping)run_step();assert(injected&&utc_job.state==UTC_EXPIRED&&!role.tx.core.durable.pending&&!role.tx.core.durable.owed&&!retry_at_us&&!atomic_get(&faulted));
 }else if(mode==43){
  memset(&utc_job,0xff,sizeof utc_job);utc_job.override=true;utc_job.scope.kind=RC8_SELECTED_BLE;
  memset(&diagnostics,0xff,sizeof diagnostics);memset(tx_due,0xff,sizeof tx_due);diagnostics.trained=true;published.quality_known=true;published.display_held=false;
  char full[2976],diag[2976];int a=r2_lora_json(full,sizeof full,INT32_MIN),b=r2_lora_diagnostics_json(diag,sizeof diag,INT32_MIN);
  assert(a>0&&a<2900&&b>0&&b<2900);printf("%s\n%s\n",full,diag);
 }else assert(false);
 char json[2976];report(now_us);int n=r2_lora_utc_json(json,sizeof json,0);assert(n>0&&(size_t)n<sizeof json);puts(json);printf("{\"mode\":%u,\"status\":\"PASS\"}\n",mode);
#else
 assert(false);
#endif
}
