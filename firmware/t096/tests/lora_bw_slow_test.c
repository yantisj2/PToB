/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "lora_ram_fixture.h"
static void measured(const struct lp_packet *p,const uint8_t ctx[16],uint64_t event,uint64_t completion){
 now_us=completion;queued=(struct r2_lora_hw_event){.tick=event*16,.capture_epoch=7,.generation=generation,
 .kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(p->kind),.rssi_half_dbm=-160,.snr_quarter_db=16,.packet_status_valid=true};
 assert(!lp_encode(queued.frame,sizeof queued.frame,p,ctx,&test_group));have_event=true;step();
}
static struct rc8_export during_projection;
static void publish_during_projection(void){r2_lora_source_publish(&during_projection);}
static void withdraw_during_projection(void){r2_lora_source_withdraw();}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 assert(LP_SCHEDULE==7&&LP_BEAT_US==30000000&&LP_SYNC_OFFSET_US==7000000&&LP_FOLLOW_OFFSET_US==2500000&&LAB_RUN_US==600000000);
 init(mode!=4);assert(!(flags&REQUESTED)&&!lab_until&&sizeof audits==48);
 r2_lora_request(true);uint64_t end=lab_until;assert(end==now_us+600000000);
 if(mode==1){now_us+=1000000;r2_lora_request(true);assert(lab_until==end);r2_lora_request(false);r2_lora_request(true);assert(lab_until==end&&!(flags&REQUESTED));puts("PASS SLOW-derivedLAB defaultOFF and single nonrenewable600s arm");return 0;}
 if(mode==2){now_us=end;step();assert(!(flags&REQUESTED)&&lab_until==end);r2_lora_request(true);assert(lab_until==end&&!(flags&REQUESTED));puts("PASS exact finite600s expiry and no rearm");return 0;}
 if(mode==7){
  struct rc8_export m=source();r2_lora_source_publish(&m);uint32_t version=map_version,revision=(uint32_t)atomic_get(&source_revision);
  for(unsigned i=0;i<20;i++){r2_lora_source_publish(&m);assert(map_version==version&&(uint32_t)atomic_get(&source_revision)==revision);}
  uint64_t phase,root,at;uint32_t epoch;struct r2_capture limit;uint32_t races=source_reads[3];
  during_projection=m;projection_hook=publish_during_projection;
  assert(source_now(&phase,&epoch,&root,&limit,&at)==SOURCE_READY&&limit.tick==m.until_tick&&source_reads[3]==races&&map_version==version);
  m.until_tick=now_us*16+16000000;during_projection=m;projection_hook=publish_during_projection;
  assert(source_now(&phase,&epoch,&root,&limit,&at)==SOURCE_READY&&limit.tick==m.until_tick&&source_reads[3]==races+1&&map_version==version+1);
  version=map_version;races=source_reads[3];m.anchor_ns++;m.map_generation++;now_us++;
  during_projection=m;projection_hook=publish_during_projection;
  assert(source_now(&phase,&epoch,&root,&limit,&at)==SOURCE_READY&&source_reads[3]==races+1&&map_version==version+1&&role.tx.maps[0].map_generation==m.map_generation);
  projection_hook=withdraw_during_projection;
  assert(source_now(&phase,&epoch,&root,&limit,&at)==SOURCE_ABSENT&&!(flags&SOURCE_PRESENT));
  puts("PASS exact duplicate DURING projection creates no false race; shortening/replacement/withdrawal still fence");return 0;
 }
 if(mode==8||mode==9){
  struct rc8_export m=source();r2_lora_source_publish(&m);generation=3;owner.tx_kind=LP_SYNC;
  role.tx.edge=(struct r2_lora_hw_edge){.tick=now_us*16-16,.capture_epoch=7,.generation=generation};
  queued=(struct r2_lora_hw_event){.tick=now_us*16,.capture_epoch=7,.generation=generation,.kind=R2_LORA_HW_TX_DONE};have_event=true;
  uint64_t expected=m.anchor_ns+now_us*1000,ns=0;uint32_t version=map_version,mapped=0;bool raced=false;
  during_projection=m;projection_hook=publish_during_projection;
  assert(completed_source(&queued,&ns,&mapped,&raced)&&!raced&&ns==expected&&role.tx.pinned&&role.tx.edge.tick==queued.tick&&map_version==version+1);
  version=map_version;r2_lora_source_publish(&m);assert(map_version==version&&role.tx.pinned);
  if(mode==9){m.until_tick=queued.tick;r2_lora_source_publish(&m);assert(!role.tx.pinned&&map_version==version+1&&role.tx.maps[1].until_tick==queued.tick);assert(!completed_source(&queued,&ns,&mapped,&raced));puts("PASS shortening pinnedSYNC original interval fences its physical edge");return 0;}
  now_us+=2000;m.anchor_ns+=100;m.map_generation++;r2_lora_source_publish(&m);
  assert(role.tx.pinned&&role.tx.maps[1].map_generation==1&&completed_source(&queued,&ns,&mapped,&raced)&&ns==expected);
  r2_lora_source_withdraw();assert(!completed_source(&queued,&ns,&mapped,&raced));
  puts("PASS duplicate pin transition fences readers; real replacement preserves exact originalSYNC association; withdrawal fences");return 0;
 }
 if(mode==10){
  struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
  m.from_tick=m.anchor_tick=now_us*16;m.until_tick=m.anchor_tick+1100000*16;m.expires_ns=m.anchor_ns+1100000*1000;m.mode=RC8_MAP_DELIVERED;
  struct rc8_export original=m;r2_lora_source_publish(&m);assert(!memcmp(&m,&original,sizeof m));
  uint64_t held=original.until_tick+5000000*16;uint32_t version=map_version;
  assert(role.tx.maps[0].until_tick==held&&role.tx.maps[0].expires_ns==original.expires_ns+UINT64_C(5000000000)&&lab_source.original_until_tick==original.until_tick&&lab_source.original_expires_ns==original.expires_ns);
  now_us+=1000000;m.until_tick+=100000*16;m.expires_ns+=100000*1000;r2_lora_source_publish(&m);
  assert(map_version==version&&role.tx.maps[0].until_tick==held&&lab_source.original_until_tick==original.until_tick);
  m=original;m.until_tick-=50000*16;m.expires_ns-=50000*1000;r2_lora_source_publish(&m);
  assert(map_version==version+1&&role.tx.maps[0].until_tick==held-50000*16&&lab_source.original_until_tick==m.until_tick&&lab_source.original_expires_ns==m.expires_ns);
  now_us+=1000000;r2_lora_source_publish(&m);assert(!(flags&SOURCE_PRESENT));
  m=original;m.capture_epoch=8;m.until_tick=UINT64_MAX-16;r2_lora_source_publish(&m);assert(!(flags&SOURCE_PRESENT));
  puts("PASS LAB-only originalexpiry+fixed5s copy; originalBLEmap unchanged; duplicate cannot renew; originalshortening/expiry/captureepoch still fence");return 0;
 }
 if(mode==3||mode==5){
  uint64_t next=1337000,finish=now_us+340000000;unsigned publications=0;
  while(now_us<finish){
   if(now_us>=next){struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
    uint64_t room=mode==5?1100000:1312476;m.from_tick=m.anchor_tick=next*16;m.until_tick=(next+room)*16;
    m.anchor_ns+=next*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;m.mode=RC8_MAP_DELIVERED;m.map_generation=++publications;r2_lora_source_publish(&m);next+=1000000;}
   if(owner.tx_kind&&!have_event){uint32_t duration=r2_lora_hw_budget_us(lp_size(owner.tx_kind));
    if(now_us>=sent_at_us+duration-10000){queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+duration-10000};have_event=true;}}
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  assert(owner.sent[LP_FOLLOW-1]>=10);printf("PASS actual SLOW LAB5shold owner originalBLEgrant%u META%u/SYNC%u/FOLLOW%u; BLElease unchanged\n",mode==3?1312476:1100000,owner.sent[1],owner.sent[2],owner.sent[3]);return 0;
 }
 if(mode==6){
  for(unsigned arm=0;arm<5;arm++){
   struct lp_tx t;struct lp_reservation r;uint64_t slot=60000000;assert(lp_tx_start(&t,17,30000000,slot,30000));t.durable.cursor=2*arm;
   assert(lp_channel(t.order,2*arm)==16&&lp_channel(t.order,2*arm+1)==48);
   uint64_t earliest=slot+LP_SYNC_OFFSET_US-50000+lp_bw_packet_budget_us(2*arm,24)-10000+LP_FOLLOW_OFFSET_US;
   t.durable.channel_end_ms[48]=(uint32_t)((earliest-20000000+1999)/1000);
   assert(!lp_tx_reserve_control(&t,slot,&r,true)&&!t.durable.pending&&!lp_charged(&t.durable,slot));
   t.durable.channel_end_ms[48]=0;t.durable.channel_end_ms[16]=(uint32_t)((slot+LP_SYNC_OFFSET_US-50000-20000000+1999)/1000);
   assert(!lp_tx_reserve_control(&t,slot,&r,true));t.durable.channel_end_ms[16]=0;
   assert(lp_tx_reserve_control(&t,slot,&r,true)&&r.has_pair);
  }
  puts("PASS all5arms actualfixedchannels16/48 enforce20s SYNC/FOLLOWcooldowns and conservative ledger");return 0;
 }
 assert(mode==4);step();assert(owner.listening&&tuning_phy==R2_LORA_DTS);
 struct lp_packet m={.kind=LP_META,.schedule=LP_SCHEDULE,.serial=1,.time_valid=true,.has_pair=true,.delay_10ms=587,.utc_ns=UINT64_C(1000000000000),.tz={0,INT16_MIN},
 .scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=1,.stream=1,.seed=17,.kind=1}};
 for(unsigned i=0;i<10;i++){
  uint64_t start=UINT64_C(10000000)+i*30000000;m.cursor=2*i;m.serial=i+1;m.utc_ns=UINT64_C(1000000000000)+start*1000;
  measured(&m,NULL,start+lp_duration(LP_META)-10000,start+lp_duration(LP_META)-9000);assert(role.rx.hint);
  now_us=start+3200000;step();static const unsigned phys[5]={2,4,5,6,3};unsigned arm=i<5?i:9-i;unsigned phy=phys[arm];assert(lp_bw_arm(2*i)==arm);assert(owner.listening&&tuning_phy==phy);
  uint64_t header=start+LP_SYNC_OFFSET_US+(uint64_t)(UINT64_C(77000)*41667/(arm==0?41667:arm==1?62500:arm==2?125000:arm==3?250000:500000));
  struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE,.cursor=2*i};measured(&sync,role.rx.context,header,header+150000);assert(role.rx.pending&&tuning_phy==phy);
  struct lp_packet follow={.kind=LP_FOLLOW,.schedule=LP_SCHEDULE,.cursor=2*i,.utc_ns=UINT64_C(1000000000000)+(start+LP_SYNC_OFFSET_US)*1000};
  measured(&follow,role.rx.context,header+2800000,header+3000000);assert(role.rx.accepted==i+1&&tuning_phy==R2_LORA_DTS&&audit_kind==LP_FOLLOW&&audits[0].phy==phy&&audits[0].packet_status_valid);
 }
 uint8_t wire[LP_SYNC_BYTES];struct lp_packet sync={.kind=LP_SYNC,.schedule=LP_SCHEDULE};assert(!lp_encode(wire,sizeof wire,&sync,role.rx.context,&test_group));wire[3]=LP_SLOW;struct lp_packet decoded;assert(lp_decode(&decoded,wire,sizeof wire,role.rx.context,&test_group));
 char text[8192];assert(r2_lora_json(text,sizeof text,0)>0&&strstr(text,"RAM_BW_SWEEP_30")&&strstr(text,"\"audit_rf\""));
 puts("PASS matched actualRX7s hint/fixedchannels16/48/all5SF7forward/reversearms/FOLLOW/single48Baudit and normalnamespace rejection");
}
