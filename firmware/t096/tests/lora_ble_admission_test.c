/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual owner + BLE exporter + hardware window guard. RF/SPI are simulated. */
#include "lora_ram_fixture.h"
#include "lora_source.h"
static void refund_tokens(void){
 struct lp_tx t;struct lp_reservation r,bad;assert(lp_tx_start(&t,99,59000000,59999000,1));
 assert(lp_tx_reserve(&t,59999000,&r)&&r.control==LP_META&&r.has_pair);
 uint64_t charged=lp_charged(&t.durable,59999000);assert(charged==1643309);
 bad=r;bad.serial++;assert(!lp_tx_refund_no_rf(&t,&bad));
 bad=r;bad.slot_us++;assert(!lp_tx_refund_no_rf(&t,&bad));
 bad=r;bad.control=LP_DISC;assert(!lp_tx_refund_no_rf(&t,&bad));
 assert(lp_charged(&t.durable,59999000)==charged);
 assert(lp_tx_refund_no_rf(&t,&r)&&!lp_charged(&t.durable,59999000));
 assert(!lp_tx_refund_no_rf(&t,&r)&&!t.durable.pending&&t.durable.owed==(LP_OWED_META|LP_OWED_PAIR));
 assert(lp_tx_reserve(&t,61000000,&r));lp_tx_control_complete(&t);
 assert(!lp_tx_refund_no_rf(&t,&r)&&lp_charged(&t.durable,61000000)==charged);
 lp_tx_cancel(&t,true);assert(!lp_tx_refund_no_rf(&t,&r));
 struct lp_tx d;struct lp_reservation disc;assert(lp_tx_start(&d,100,62000000,63000000,1));
 d.durable.slot=1;d.durable.buckets[d.durable.minute%61]=2000000;
 assert(lp_tx_reserve(&d,63000000,&disc)&&disc.control==LP_DISC);bad=disc;bad.control=LP_META;
 uint64_t old=lp_charged(&d.durable,63000000);assert(!lp_tx_refund_no_rf(&d,&bad));assert(lp_charged(&d.durable,63000000)==old);
 assert(lp_tx_refund_no_rf(&d,&disc)&&lp_charged(&d.durable,63000000)==2000000);
 assert(lp_tx_reserve(&t,62000000,&r));lp_tx_fault(&t);
 assert(!lp_tx_refund_no_rf(&t,&r)&&t.durable.fh_disabled);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);if(mode==9){refund_tokens();puts("{\"refund_token_cases\":8,\"status\":\"PASS\"}");return 0;}
 init(true);r2_lora_request(true);uint64_t next=now_us,available=now_us;unsigned gen=0;
 if(mode==4)next+=70000;
 uint64_t begin=now_us,stop=now_us+600000000,tx_start=0;unsigned tx_kind=0,last_txs=0;
 bool checked=false,injected=false;
 struct lp_rx receiver={0};uint32_t previous_meta=0;unsigned received_meta=0;
 while(now_us<stop){
  uint64_t life=mode==4?1200000:(mode==2&&now_us-begin<120000000)?1000000:1497000;
  fake_encode_delay_us=mode==3?200000:0;fake_spi_delay_us=mode==3?200000:0;
  if(now_us>=next){available=now_us;next+=mode==4?600000:625000;gen++;}
  struct rc8_export m;struct r2_capture at;assert(r2_capture_now(&at));
  r2_servo_map_t delivered={.delivery=available*16,.anchor=(int64_t)(1000000000000ull+available*1000),.valid=true};
  struct rc8_scope s={.emitter=1,.emitter_boot=22,.stream=1,.root=2,.root_boot=44,.parent=2,.parent_boot=44,.source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
  uint64_t until=(available+life)*16;
  bool valid=r2_lora_source_follower(&m,&delivered,s,at,until,1000000000000ull+available*1000,1000000000000ull+(available+life)*1000,0,gen);
  if(valid){assert(m.until_tick<=until);r2_lora_source_publish(&m);assert(role.tx.maps[0].until_tick<=until);}else r2_lora_source_withdraw();
  if(owner.tx_kind&&!have_event&&now_us>=tx_start+lp_duration(tx_kind)-10000){queued=(struct r2_lora_hw_event){.tick=tx_start*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;}
  bool inject=mode==5&&!injected&&previous_meta&&!owner.tx_kind&&owner.stage==3&&
      role.tx.reservation.serial==previous_meta+1&&
      role.tx.reservation.control==LP_META&&now_us>=role.tx.reservation.slot_us;
  uint64_t before_charge=lp_charged(&role.tx.core.durable,now_us);uint32_t before_refunds=owner.no_rf_refunds,before_finish=rejected.finish_count;
  if(inject){reject_next_tx=true;injected=true;}
  step();
  if(inject){assert(owner.no_rf_refunds==before_refunds+1&&rejected.finish_count==before_finish+1);assert(role.tx.core.durable.serial==previous_meta&&
      lp_charged(&role.tx.core.durable,now_us)<before_charge&&
      role.tx.core.durable.owed==(LP_OWED_META|LP_OWED_PAIR));}
  if(txs!=last_txs){
   tx_start=sent_at_us;tx_kind=owner.tx_kind;last_txs=txs;
   if(mode==5&&tx_kind==LP_META){struct lp_packet p;
    assert(!lp_decode(&p,sent,sent_len,role.tx.context,&test_group));
    assert(p.serial==previous_meta+1);previous_meta=p.serial;
    uint64_t end=tx_start+lp_duration(LP_META)-10000;
    assert(lp_rx_meta(&receiver,&p,&test_group,NULL,end,end+1000));
    assert(!receiver.missing);++received_meta;
   }
  }
  if(mode==2&&!checked&&now_us-begin>=119000000){
   assert(!txs&&!role.tx.core.durable.cursor&&!role.tx.core.durable.fh_disabled);
   uint64_t charge=lp_charged(&role.tx.core.durable,now_us);
   /* Current preparation may hold one charged reservation. Every completed
    * failed launch requires a single-use healthy no-RF proof to refund it. */
   assert(charge==(role.tx.core.durable.pending?UINT64_C(1643309):0));
   assert(owner.refused<1000&&rejected.source_count); // bounded retry, not a 2ms refusal loop
   assert(role.tx.core.durable.owed==(LP_OWED_META|LP_OWED_PAIR)&&owner.no_rf_refunds&&role.tx.core.durable.serial==(role.tx.core.durable.pending?1u:0u)); // logical event still owed, only proven no-RF refunded
   checked=true;
  }
 }
 assert(mode==1||mode==3||mode==4||mode==5||checked);assert(owner.sent[LP_META-1]>=2&&owner.sent[LP_FOLLOW-1]>=(mode==2?6u:7u));
 assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled&&!owner.storage_fail);
 assert(owner.max_source_room_us>=(mode==4?1190000u:1490000u)&&owner.max_source_room_us<=(mode==4?1200000u:1497000u));
 assert(owner.refused<1200);
 if(mode==2||mode==4)assert(rejected.reason&&rejected.source_count);if(mode==3)assert(!owner.late);
 if(mode==5)assert(injected&&received_meta>=2&&owner.no_rf_refunds>=1&&rejected.finish_count==1&&!receiver.missing);
 report(now_us);char json[2976];int n=r2_lora_json(json,sizeof json,0);assert(n>0&&(size_t)n<sizeof json);
 printf("{\"mode\":%u,\"status\":\"PASS\",\"txs\":%u,\"sent\":[%u,%u,%u,%u],\"charged_us\":%llu,\"source_reject\":%u,\"finish_reject\":%u,\"refunds\":%u,\"max_room_us\":%u,\"max_phase_us\":%u,\"refused\":%u,\"json_bytes\":%d}\n",mode,txs,owner.sent[0],owner.sent[1],owner.sent[2],owner.sent[3],(unsigned long long)lp_charged(&role.tx.core.durable,now_us),rejected.source_count,rejected.finish_count,owner.no_rf_refunds,owner.max_source_room_us,owner.max_source_phase_us,owner.refused,n);
}
