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
  puts("PASS strictMETA300 no offgrid retry/capoverride; skipped slots retain original phase");return 0;
 }
 if(mode==3){
  init(true);uint64_t next=1337000,finish=now_us+340000000;unsigned publications=0;
  while(now_us<finish){
   if(now_us>=next){struct rc8_export m=source();m.scope.kind=RC8_SELECTED_BLE;m.scope.root=m.scope.parent=3;m.scope.root_boot=m.scope.parent_boot=44;
    uint64_t room=1100000;m.from_tick=m.anchor_tick=next*16;m.until_tick=(next+room)*16;
    m.anchor_ns+=next*1000;m.created_ns=m.anchor_ns;m.expires_ns=m.anchor_ns+room*1000;m.mode=RC8_MAP_DELIVERED;m.map_generation=++publications;r2_lora_source_publish(&m);next+=1000000;}
   if(owner.tx_kind&&!have_event){uint32_t duration=r2_lora_hw_budget_us(lp_size(owner.tx_kind));
    if(now_us>=sent_at_us+duration-10000){queued=(struct r2_lora_hw_event){.tick=sent_at_us*16,.capture_epoch=7,.generation=sent_generation,.kind=R2_LORA_HW_TX_DONE,.end_local_us=sent_at_us+duration-10000};have_event=true;}}
   step();assert(!atomic_get(&faulted)&&!role.tx.core.durable.fh_disabled);
  }
  printf("OWNER META%u SYNC%u FOLLOW%u DISC%u\n",owner.sent[1],owner.sent[2],owner.sent[3],owner.sent[0]);
  assert(owner.sent[1]==2&&owner.sent[2]>=10&&owner.sent[2]==owner.sent[3]&&!owner.sent[0]);
  puts("PASS actual staged owner fixedPHY/noDISC/strictMETA300 and unchangedBLE grants plus explicitLABhold");return 0;
 }
 assert(mode==4);init(false);step();assert(owner.listening&&tuning_phy==R2_LORA_FHSS&&tuning_channel==50);
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
 uint64_t accepted=role.rx.accepted;lp_rx_expire(&role.rx,role.rx.observed_deadline_us);
 assert(!lp_rx_sync(&role.rx,22,7000000000,7,440000000,440000000)&&role.rx.accepted==accepted);
 puts("PASS actualRX authenticatedkind dispatch, conservativeMETAlease, nohint loss reacquisition and context expiry");
}
