/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual passive owner, fake boundaries; no hardware/UTC qualification. */
#define main fast_regression_main
#include "lora_ram_fixture.h"
#undef main
static unsigned observed;static bool event_due;static struct r2_lora_hw_event completion;
static void inspect_wire(void){
 assert(sent[0]=='D'&&sent[1]=='X'&&sent[2]>=LP_DISC&&sent[2]<=LP_FOLLOW&&sent[3]==1);
 assert(sent_len==lp_size(sent[2]));
 for(size_t i=8;i<sent_len;i++)assert(!sent[i]);
 struct lp_packet p;uint8_t context[16]={1};assert(lp_decode(&p,sent,sent_len,context,&test_group)==-EINVAL);
 assert(role.tx.core.durable.pending||!role.tx.reservation.has_pair); /* Full RAM precharge before SetTx. */
 if(sent[2]>=LP_SYNC)assert(tuning_phy==R2_LORA_FHSS&&tuning_channel==lp_channel(role.tx.core.order,role.tx.reservation.cursor+(sent[2]==LP_FOLLOW)));
 else assert(tuning_phy==R2_LORA_DTS);
 assert(r2_diag_scratch.lora.diagnostic.new_charge_us<=DIAGNOSTIC_CHARGE_US);
 assert(now_us<=lab_until);assert(!role.tx.context_epoch&&!role.tx.context_root_boot&&!role.tx.stream);
}
static void drive(uint64_t end){
 while(now_us<end){
  if(event_due&&now_us>=completion.end_local_us){queued=completion;have_event=true;event_due=false;}
  step();
  if(txs!=observed){assert(txs==observed+1);observed=txs;inspect_wire();uint64_t start=now_us-2000;
   completion=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=start+lp_duration(sent[2])-10000};event_due=true;}
 }
}
static bool fresh_boot;
static void setup(void){
 init(true);assert(transmitter&&!owner.active&&!(atomic_get(&flags)&REQUESTED));
 assert(!r2_lora_source_needed());struct rc8_export m=source();r2_lora_source_publish(&m);assert(!(atomic_get(&flags)&SOURCE_PRESENT));
 now_us=fresh_boot?2000000:25000000;step();assert(!owner.active&&!txs);r2_lora_request(true);step();assert(owner.active);
 assert(r2_diag_scratch.lora.diagnostic.used&&lab_until==r2_diag_scratch.lora.diagnostic.armed_us+LAB_RUN_US);
 drive(now_us+10000);r2_lora_source_withdraw();assert(!fenced());uint64_t expiry=lab_until;r2_lora_request(true);assert(lab_until==expiry);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);fresh_boot=mode==10;setup();uint64_t armed=r2_diag_scratch.lora.diagnostic.armed_us,expires=lab_until;
 if(mode==2)capture_valid=false;
 if(mode==3){drive(now_us+10000);uint64_t slot=role.tx.core.durable.next_slot_us;
  now_us=slot-PRECHARGE_LEAD_US;step();assert(owner.stage==3);now_us=slot+46000;step();
  assert(owner.late==1&&!txs&&!role.tx.core.durable.pending&&!role.tx.core.durable.fh_disabled);return 0;}
 if(mode==4||mode==5||mode==8||mode==9){lp_tx_cancel(&role.tx.core,true);owner.stage=0;}
 if(mode==4)lp_tx_fault(&role.tx.core);
 if(mode==5){role.tx.core.durable.buckets[0]=LP_CAP_US;}
 if(mode==6){reject_next_tx=true;drive(now_us+6000000);assert(!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.pending&&!r2_diag_scratch.lora.diagnostic.new_charge_us&&owner.no_rf_refunds==1&&!txs);return 0;}
 if(mode==8)r2_diag_scratch.lora.diagnostic.new_charge_us=DIAGNOSTIC_CHARGE_US-(lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW))+1;
 if(mode==9)lab_until=now_us+LP_FRAME_END_US-1;
 if(mode==7){while(owner.stage!=2){drive(now_us+2000);assert(now_us<expires);}r2_lora_request(false);drive(now_us+10000);assert(!role.tx.core.durable.fh_disabled&&!role.tx.core.durable.pending&&atomic_get(&closed));return 0;}
 drive(expires+1000000);
 assert(!(atomic_get(&flags)&REQUESTED)&&atomic_get(&closed)&&!owner.active&&!owner.stage&&!owner.tx_kind);
 assert(!role.tx.core.durable.pending&&!role.tx.dirty&&r2_diag_scratch.lora.diagnostic.armed_us==armed);
 assert(!atomic_get(&faulted)&&!owner.error&&!owner.storage_fail&&!owner.missing_map&&!owner.late);
 if(mode==1||mode==10){assert(owner.sent[0]>=2&&owner.sent[1]>=2&&owner.sent[2]>=(fresh_boot?3u:5u)&&owner.sent[2]==owner.sent[3]);assert(!role.tx.core.durable.fh_disabled&&role.tx.core.durable.cursor==2*owner.sent[2]);assert(lp_charged(&role.tx.core.durable,now_us)==r2_diag_scratch.lora.diagnostic.new_charge_us);}
 else assert(!txs);
 unsigned before=txs;r2_lora_request(true);drive(now_us+12000000);assert(txs==before&&!(atomic_get(&flags)&REQUESTED)&&lab_until==0);
 char json[3072];int n=r2_lora_json(json,sizeof json,0);assert(n>0&&n<(int)sizeof json&&strstr(json,"\"tx_diagnostic\":true")&&strstr(json,"\"time_valid\":false")&&strstr(json,"\"choice\":0"));
 printf("PASS diagnostic %u sent=%u new_charge_us=%u json=%d scratch=%zu\n",mode,txs,r2_diag_scratch.lora.diagnostic.new_charge_us,n,sizeof(union r2_diag_scratch));
}
