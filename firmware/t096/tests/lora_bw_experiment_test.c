/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual finite lab owner/codec at synthetic hardware boundaries. */
#include "lora_ram_fixture.h"
static void actual_done(uint64_t start,unsigned kind){
 uint32_t duration=lp_duration(kind);
 if(tuning_phy==R2_LORA_FHSS_BW500)duration=kind==LP_SYNC?28496:31568;
 now_us=start+duration-10000;
 queued=(struct r2_lora_hw_event){.tick=start*16,.capture_epoch=7,.generation=sent_generation,
  .kind=R2_LORA_HW_TX_DONE,.end_local_us=now_us};have_event=true;step();
}
static void measured_packet(const struct lp_packet *p,const uint8_t ctx[16],uint64_t event,uint64_t completion){
 now_us=completion;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,
  .generation=generation,.kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind),
  .rssi_half_dbm=-161,.snr_quarter_db=13,.packet_status_valid=true};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,ctx,&test_group));have_event=true;step();
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 assert(LP_SCHEDULE==5&&LP_BEAT_US==4000000&&LAB_RUN_US==240000000);
 init(mode!=4);assert(!(flags&REQUESTED)&&!lab_until);
 if(mode==1){
  r2_lora_request(true);uint64_t end=lab_until;
  assert(end==now_us+240000000&&r2_diag_scratch.lora.diagnostic.used);
  now_us+=1000000;r2_lora_request(true);assert(lab_until==end);
  r2_lora_request(false);r2_lora_request(true);assert(!lab_until&&!(flags&REQUESTED));
  puts("PASS defaultOFF one nonrenewable240s arm; OFF prevents rearm");return 0;
 }
 r2_lora_request(true);uint64_t end=lab_until;
 if(mode==6||mode==7||mode==8){
  /* Declared feasibility model: original BLE maps arrive at UTC337ms each
   * second with1.312476s permission. No per-poll renewal is permitted. */
  uint64_t next=1337000,finish=now_us+40000000;unsigned publications=0;
  while(now_us<finish){
   if(now_us>=next){
    struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;
    m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
    uint64_t room=mode==8?1100000:1312476;
    m.from_tick=m.anchor_tick=next*16;m.until_tick=(next+room)*16;
    m.anchor_ns+=next*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;
    m.mode=RC8_MAP_DELIVERED;m.map_generation=++publications;
    r2_lora_source_publish(&m);next+=1000000;
   }
   if(owner.tx_kind&&!have_event){
    uint32_t duration=lp_duration(owner.tx_kind);
    if(tuning_phy==R2_LORA_FHSS_BW500)duration=owner.tx_kind==LP_SYNC?28496:31568;
    if(now_us>=sent_at_us+duration-10000){
     queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,
      .kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+duration-10000};have_event=true;
    }
   }
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  if(mode==6){assert(owner.sent[LP_META-1]&&owner.sent[LP_FOLLOW-1]>=4);
   printf("PASS bounded original BLE grant model400ms admits META%u/FOLLOW%u; no source/deadline extension\n",owner.sent[LP_META-1],owner.sent[LP_FOLLOW-1]);}
  else{assert(!owner.sent[LP_META-1]&&!owner.sent[LP_FOLLOW-1]&&owner.refused);
   puts(mode==7?"PASS causal200ms grid negative has no META/FOLLOW under same declared original grants":
    "PASS400ms cannot transmit META with original1.1s grants; no lease extension");}
  return 0;
 }
 if(mode==2){
  now_us=end;step();assert(!(flags&REQUESTED)&&!lab_until);
  now_us+=1000;r2_lora_request(true);assert(!(flags&REQUESTED)&&!lab_until);
  puts("PASS exact finite expiry without host cleanup or renewal");return 0;
 }
 if(mode==3||mode==5){
  struct rc8_export m=source();r2_lora_source_publish(&m);step();step();
  now_us=owner.quiet_until_us+21000000;assert(restore(now_us+PRECHARGE_LEAD_US+100000));step();
  if(mode==5){
   role.tx.core.durable.next_slot_us=end-LP_FRAME_END_US+1;now_us=end-4000000;
   step();assert(!(flags&REQUESTED)&&!txs&&!role.tx.core.durable.pending);
   puts("PASS no reservation extends beyond immutable arm deadline");return 0;
  }
  for(unsigned i=0;i<4;i++){
   uint64_t slot=role.tx.core.durable.next_slot_us;
   now_us=slot-PRECHARGE_LEAD_US;step();now_us=slot;step();
   unsigned control=owner.tx_kind;assert(control==LP_META||control==LP_DISC);
   assert(tuning_phy==R2_LORA_DTS);done(slot,control);
   now_us=slot+LP_SYNC_OFFSET_US;step();assert(owner.tx_kind==LP_SYNC);
   unsigned expected=i==1||i==2?R2_LORA_FHSS_BW500:R2_LORA_FHSS;
   assert(tuning_phy==expected&&role.tx.reservation.cursor==2*i);
   actual_done(slot+LP_SYNC_OFFSET_US,LP_SYNC);
   now_us=owner.last_sync_us+LP_FOLLOW_OFFSET_US;uint64_t at=now_us;
   step();assert(owner.tx_kind==LP_FOLLOW&&tuning_phy==expected);actual_done(at,LP_FOLLOW);
   assert(!role.tx.core.durable.pending);
  }
  puts("PASS actual TX owner ABBA SYNC/FOLLOW arms; unchanged DTS and hop cursor");return 0;
 }
 assert(mode==4);step();assert(owner.listening&&tuning_phy==R2_LORA_DTS);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,
  .has_pair=true,.delay_10ms=85,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
  .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 for(unsigned i=0;i<4;i++){
  uint64_t start=UINT64_C(10000000)+i*4000000;
  m.cursor=2*i;m.serial=i+1;m.utc_ns=UINT64_C(1000000000000)+start*1000;
  measured_packet(&m,NULL,start+lp_duration(LP_META)-10000,start+lp_duration(LP_META)-9000);
  assert(role.rx.hint);now_us=start+1850000;step();
  unsigned expected=i==1||i==2?R2_LORA_FHSS_BW500:R2_LORA_FHSS;
  assert(owner.listening&&tuning_phy==expected&&tuning_channel==lp_channel(role.rx.order,2*i));
  uint64_t header=start+LP_SYNC_OFFSET_US+(expected==R2_LORA_FHSS?77000:6500);
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};
  measured_packet(&sync,role.rx.context,header,header+150000);
  assert(role.rx.pending&&tuning_phy==expected&&tuning_channel==lp_channel(role.rx.order,2*i+1));
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,
   .utc_ns=UINT64_C(1000000000000)+(start+LP_SYNC_OFFSET_US)*1000};
  measured_packet(&follow,role.rx.context,header+400000,header+600000);
  assert(role.rx.accepted==i+1&&tuning_phy==R2_LORA_DTS&&audits[AUDIT_INDEX(LP_SYNC)].phy==expected);
  assert(audits[AUDIT_INDEX(LP_SYNC)].packet_status_valid&&audits[AUDIT_INDEX(LP_SYNC)].rssi==-161&&audits[AUDIT_INDEX(LP_SYNC)].snr==13);
 }
 uint8_t wire[LP_SYNC_BYTES];struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE};
 assert(!lp_encode(wire,sizeof wire,&sync,role.rx.context,&test_group));
 wire[3]=LP_FAST;struct lp_packet decoded;assert(lp_decode(&decoded,wire,sizeof wire,role.rx.context,&test_group));
 char text[8192];assert(r2_lora_json(text,sizeof text,0)>0&&strstr(text,"\"bw_experiment\":true")&&strstr(text,"ABBA_CURSOR_DIV2"));
 puts("PASS actual RX authenticated ABBA arms, same-arm FOLLOW, audit PHY and ordinary namespace rejection");
}
