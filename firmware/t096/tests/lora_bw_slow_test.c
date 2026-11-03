/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "lora_ram_fixture.h"
static void measured(const struct lp_packet *p,const uint8_t ctx[16],uint64_t event,uint64_t completion){
 now_us=completion;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,
 .kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind),.rssi_half_dbm=-160,.snr_quarter_db=16,.packet_status_valid=true};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,ctx,&test_group));have_event=true;step();
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 assert(LP_SCHEDULE==6&&LP_BEAT_US==30000000&&LP_SYNC_OFFSET_US==7000000&&LP_FOLLOW_OFFSET_US==2500000&&LAB_RUN_US==480000000);
 init(mode!=4);assert(!(flags&REQUESTED)&&!lab_until&&sizeof audits==48);
 r2_lora_request(true);uint64_t end=lab_until;assert(end==now_us+480000000);
 if(mode==1){now_us+=1000000;r2_lora_request(true);assert(lab_until==end);r2_lora_request(false);r2_lora_request(true);assert(lab_until==end&&!(flags&REQUESTED));puts("PASS SLOW-derivedLAB defaultOFF and single nonrenewable480s arm");return 0;}
 if(mode==2){now_us=end;step();assert(!(flags&REQUESTED)&&lab_until==end);r2_lora_request(true);assert(lab_until==end&&!(flags&REQUESTED));puts("PASS exact finite480s expiry and no rearm");return 0;}
 if(mode==3||mode==5){
  uint64_t next=1337000,finish=now_us+160000000;unsigned publications=0;
  while(now_us<finish){
   if(now_us>=next){struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
    uint64_t room=mode==5?1100000:1312476;m.from_tick=m.anchor_tick=next*16;m.until_tick=(next+room)*16;
    m.anchor_ns+=next*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;m.mode=RC8_MAP_DELIVERED;m.map_generation=++publications;r2_lora_source_publish(&m);next+=1000000;}
   if(owner.tx_kind&&!have_event){uint32_t duration=lp_duration(owner.tx_kind);if(tuning_phy==R2_LORA_FHSS_BW500)duration=owner.tx_kind==LP_SYNC?28496:31568;
    if(now_us>=sent_at_us+duration-10000){queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+duration-10000};have_event=true;}}
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  if(mode==3){assert(owner.sent[LP_FOLLOW-1]>=4);printf("PASS actual SLOW-derivedLAB owner originalBLEgrants META%u/SYNC%u/FOLLOW%u; no lease extension\n",owner.sent[1],owner.sent[2],owner.sent[3]);}
  else{assert(!txs&&!owner.sent[1]&&!owner.sent[3]);puts("PASS original1.1s grant cannot admit META; no RF or expiry extension");}return 0;
 }
 assert(mode==4);step();assert(owner.listening&&tuning_phy==R2_LORA_DTS);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,.has_pair=true,.delay_10ms=587,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
 .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 for(unsigned i=0;i<4;i++){
  uint64_t start=UINT64_C(10000000)+i*30000000;m.cursor=2*i;m.serial=i+1;m.utc_ns=UINT64_C(1000000000000)+start*1000;
  measured(&m,NULL,start+lp_duration(LP_META)-10000,start+lp_duration(LP_META)-9000);assert(role.rx.hint);
  now_us=start+3200000;step();unsigned phy=i==1||i==2?R2_LORA_FHSS_BW500:R2_LORA_FHSS;assert(owner.listening&&tuning_phy==phy);
  uint64_t header=start+LP_SYNC_OFFSET_US+(phy==R2_LORA_FHSS?77000:6500);
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};measured(&sync,role.rx.context,header,header+150000);assert(role.rx.pending&&tuning_phy==phy);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+(start+LP_SYNC_OFFSET_US)*1000};
  measured(&follow,role.rx.context,header+2800000,header+3000000);assert(role.rx.accepted==i+1&&tuning_phy==R2_LORA_DTS&&audit_kind==LP_FOLLOW&&audits[0].phy==phy&&audits[0].packet_status_valid);
 }
 uint8_t wire[LP_SYNC_BYTES];struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE};assert(!lp_encode(wire,sizeof wire,&sync,role.rx.context,&test_group));wire[3]=LP_SLOW;struct lp_packet decoded;assert(lp_decode(&decoded,wire,sizeof wire,role.rx.context,&test_group));
 char text[8192];assert(r2_lora_json(text,sizeof text,0)>0&&strstr(text,"RAM_BW_AB_30")&&strstr(text,"\"audit_rf\""));
 puts("PASS matched actualRX7s hint/fourSF7ABBAarms/FOLLOW/single48Baudit and normalnamespace rejection");
}
