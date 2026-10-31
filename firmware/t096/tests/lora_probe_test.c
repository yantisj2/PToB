/* Actual serialized owner/codec/policy with synthetic physical boundaries.
 * Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main original_runtime_main
#include "lora_service_test.c"
#undef main
static void feed_probe(struct rc8_probe_packet *p,uint64_t at){uint8_t w[80];assert(!rc8_probe_encode(w,p,&test_group));packet(w,80,at);}
static void feed_delayed_probe(struct rc8_probe_packet *p,uint64_t edge,uint64_t observed){
 queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_RX_DONE,.tick=edge*16,.capture_epoch=7,.generation=generation,.length=80};
 assert(!rc8_probe_encode(queued.frame,p,&test_group));have_event=true;now_us=observed;step();
}
static struct rc8_scope ready_client(void){
 struct rc8_scope s=coordination_acquire();companion(s,true,1,5300000);
 for(unsigned i=3;i<=6;i++){
  struct rc8_packet p={.sequence=i,.root=s.root,.source_epoch=s.source_epoch,.scale=s.scale,.kind=s.kind,.corrected=true,
   .correction={.sequence=i-1,.event_ns=UINT64_C(9000000000)+(i-2)*UINT64_C(4000000000),.created_ns=0,
    .expires_ns=UINT64_C(9000000000)+(i-2)*UINT64_C(4000000000)+1000000000,.map_generation=i}};
  uint8_t w[80];assert(!rc8_context_id(p.context,&s,&test_group));assert(!rc8_encode(w,&p,&test_group));packet(w,80,1200000+(i-1)*4000000);
 }
 assert(role.rx.fit.result_n>=4&&probe_client_ready(now_us)&&probe.next_us<=now_us);return s;
}
static struct rc8_probe_packet request(void){
 (void)ready_client();struct rc8_probe_packet offer={.kind=RC8_OFFER,.offer=1,.nonce=7654,.t2=role.rx.highest_sequence};memcpy(offer.context,role.rx.context,16);
 feed_probe(&offer,21400000);assert(probe.state==1&&!probe.tx_kind);
 now_us=probe.ready_us;step();assert(probe.tx_kind==RC8_REQUEST);
 struct rc8_probe_packet p;assert(!rc8_probe_decode(&p,sent,80,&test_group));
 now_us+=258560;done();assert(probe.state==2&&probe.t1&&probe.epoch==7);return p;
}
int main(int argc,char **argv){
 assert(argc==2);int mode=atoi(argv[1]);
 if(mode<=8||mode==20||mode==21||mode==23||mode==28){
  struct rc8_probe_packet p=request();uint64_t t1=probe.t1;
  if(mode==28){witness_enabled=true;r2_lora_shadow_publish();assert(witness_published==1&&witness_map.lora&&!witness_map.lora_delay_ns);}
  if(mode==23){r2_lora_rtt_lab(true);step();assert(probe_lab(now_us));}
  p.kind=RC8_RESPONSE;p.t2=UINT64_C(10000000000)+t1*125/2+10000;
  if(mode==2)p.nonce++;
  if(mode==3)p.context[0]^=1;
  if(mode==4)p.offer++;
  feed_probe(&p,t1/16+500020);
  if(mode>=2&&mode<=4){assert(probe.state==2&&!probe.completed&&probe.rejected);return 0;}
  assert(probe.state==3&&probe.t4==t1+8000320);
  p.kind=RC8_FOLLOW;p.t3=p.t2+500000000;
  if(mode==5)r2_lora_control(false,true,RC8_SELECT_BLE);
  if(mode==6)now_us=probe.deadline+1;
  if(mode==7)p.t2++;
  feed_probe(&p,mode==6?now_us:now_us+400000);
  if(mode>=5&&mode<=7){assert(!probe.completed&&!probe.cal_until&&!atomic_get(&faulted));return 0;}
  assert(probe.completed==1&&probe.delay_ns==10000&&probe.cal_until>now_us&&!probe.state&&!atomic_get(&faulted));
  if(mode==28){
   assert(probe.events.t1==t1&&probe.events.t4==t1+8000320&&probe.events.t2==p.t2&&probe.events.t3==p.t3&&probe.events.epoch==7);
   r2_lora_shadow_publish();assert(witness_published==2&&witness_map.lora&&witness_map.lora_delay_ns==10000);
   assert(witness_map.normal_map.anchor==(int64_t)role.rx.sample.tx.event_ns+10000);
   assert(witness_map.normal_until<=role.rx.sample.rx.tick+RC8_HOLD_US*16&&witness_map.lo>role.rx.sample.rx.tick);
  }
  uint32_t accepted=role.rx.accepted;feed_probe(&p,now_us+1000);assert(probe.completed==1&&role.rx.accepted==accepted);
  struct r2_lora_view v;r2_lora_view(&v);assert(v.choice==RC8_SELECT_LORA); /* GM profile remains a watch receiver. */
  if(mode==8){r2_lora_request(false);step();assert(!probe.cal_until&&!published.view.utc_us&&!atomic_get(&faulted));}
  if(mode==20){
   uint64_t original_until=probe.cal_until;struct rc8_scope scope=role.rx.scope;
   companion(scope,false,2,now_us+1000);assert(!role.rx.sample.valid&&probe.cal_until==original_until);
   r2_lora_view(&v);assert(!v.utc_us&&v.choice==RC8_SELECT_NONE); /* Cache grants no time. */
   companion(scope,true,3,now_us+1000);assert(!role.rx.sample.valid&&probe.cal_until==original_until);
   uint64_t at=now_us+1000000,ns=UINT64_C(10000000000)+at*1000;uint32_t seq=role.rx.highest_sequence+1;
   struct rc8_packet beacon={.sequence=seq,.root=scope.root,.source_epoch=scope.source_epoch,.scale=scope.scale,.kind=scope.kind};
   uint8_t w[80];assert(!rc8_context_id(beacon.context,&scope,&test_group));assert(!rc8_encode(w,&beacon,&test_group));packet(w,80,at);
   beacon.sequence++;beacon.corrected=true;beacon.correction=(struct rc8_correction){.sequence=seq,.event_ns=ns,.expires_ns=ns+1000000000,.map_generation=1};
   assert(!rc8_encode(w,&beacon,&test_group));packet(w,80,at+4000000);
   assert(role.rx.sample.valid&&probe.cal_until==original_until&&probe.completed==1);r2_lora_view(&v);assert(v.utc_us&&v.choice==RC8_SELECT_LORA);
   scope.root=4;scope.root_boot=45;scope.source_epoch=6;beacon.sequence++;beacon.root=4;beacon.source_epoch=6;beacon.corrected=false;beacon.correction=(struct rc8_correction){0};
   assert(!rc8_context_id(beacon.context,&scope,&test_group));assert(!rc8_encode(w,&beacon,&test_group));packet(w,80,now_us+1000);assert(!probe.cal_until);
  }
  if(mode==21){role.rx.sample.rx.capture_epoch++;step();assert(!probe.cal_until);}
  if(mode==23){
   uint64_t measured=probe.cal_until-PROBE_CAL_US;
   assert(probe.next_us>=measured+15000000&&probe.next_us<measured+30000000);
   uint64_t original=probe.cal_until,at=now_us;r2_lora_rtt_lab(false);step();
   assert(!probe_lab(now_us)&&!probe.lab_until&&probe.cal_until==original);
   assert(probe.next_us>=at+900000000&&probe.next_us<at+1800000000);
  }

 }else if(mode==9){
  uint32_t d;assert(rc8_rtt_delay(20000000,0,1000000000,&d)&&d==125000000);
  assert(!rc8_rtt_delay(20000001,0,1000000000,&d)&&d==0);
  assert(rc8_rtt_delay(16000320,0,1000000000,&d)&&d==10000);
  assert(rc8_rtt_delay(16000000,10000,999990000,&d)&&d==10000);
  assert(!rc8_rtt_delay(16000000,0,1000000001,&d));assert(!rc8_rtt_delay(1600000001,0,1,&d));
  assert(!rc8_rtt_delay(16000000,100001,1,&d));assert(!rc8_rtt_delay(16000000,0,1,&d));
 }else if(mode==10){
  (void)ready_client();r2_lora_control(false,true,RC8_SELECT_BLE);step();
  struct rc8_probe_packet p={.kind=RC8_OFFER,.offer=1,.nonce=1,.t2=role.rx.highest_sequence};memcpy(p.context,role.rx.context,16);
  unsigned tx=txs;feed_probe(&p,now_us+1000);assert(!probe.state&&tx==txs&&!probe.completed);
 }else if(mode==11){
  struct rc8_scope s=ready_client();struct rc8_companion c={.scope=s,.sequence=2,.timing_sequence=role.rx.highest_sequence,.time_valid=true,.tz={2,-300}};
  uint8_t w[128];assert(!rc8_companion_encode(w,&c,&test_group));packet(w,128,now_us+1000);
  struct r2_lora_view v;r2_lora_view(&v);assert(v.tz.id==2&&v.tz.offset_min==-300&&v.tz_until_us>now_us);
  c.sequence++;c.tz=r2_tz_unknown();assert(!rc8_companion_encode(w,&c,&test_group));packet(w,128,now_us+1000);
  r2_lora_view(&v);assert(!v.tz.id&&role.rx.sample.valid);
  w[2]=3;assert(rc8_companion_decode(&c,probe.context,w,128,&test_group));
 }else if(mode==12||mode==17||mode==18||mode==25){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);step();step();done();done();
  if(mode==25){r2_lora_rtt_lab(true);step();assert(probe_lab(now_us));}
  now_us+=mode==25?5000000:60000000;map=source(now_us*16,2);r2_lora_source_publish(&map);step();done();
  if(role.tx.pending)done();
  assert(probe.tx_kind==RC8_OFFER);struct rc8_probe_packet offer;assert(!rc8_probe_decode(&offer,sent,80,&test_group));
  now_us+=258560;done();assert(probe.state==4);
  uint32_t before=txs;events(RC8_D_TZ);step();assert(txs==before); /* No metadata inside the request window. */
  struct rc8_probe_packet p={.kind=RC8_REQUEST,.offer=offer.offer,.client=2,.boot=222,.nonce=333};memcpy(p.context,offer.context,16);
  uint64_t original_ns=0;
  if(mode==17||mode==18){
   hide_rx_edge=mode==18;now_us+=300000;queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_RX_DONE,.tick=now_us*16,.capture_epoch=7,.generation=generation,.length=80};
   assert(!rc8_probe_encode(queued.frame,&p,&test_group));have_event=true;uint64_t original_tick=queued.tick;
   for(unsigned i=4;i<24;i++){now_us+=100000;map=source(now_us*16,i);r2_lora_source_publish(&map);}
   original_ns=probe.packet.t3;
   struct rc8_export evicted;assert(!current_map(&evicted,original_tick,7,0));step();
   if(mode==18){assert(!probe.tx_kind&&probe.rejected==1&&probe.refusal==5&&!atomic_get(&faulted));return 0;}
   assert(original_ns&&probe.packet.t2==original_ns); /* Original RX map, never a later formula. */
  }else feed_probe(&p,now_us+300000);
  assert(probe.state==5&&probe.packet.t2);
  struct rc8_probe_packet competing=p;competing.client=99;competing.nonce++;
  feed_probe(&competing,now_us+1000);assert(probe.state==5&&probe.packet.client==2&&probe.rejected==1&&!atomic_get(&faulted));
  if(!probe.tx_kind){if(now_us<probe.ready_us)now_us=probe.ready_us;map=source(now_us*16,30);r2_lora_source_publish(&map);step();}
  assert(probe.tx_kind==RC8_RESPONSE);
  now_us+=258560;done();assert(probe.state==6&&probe.packet.t3>probe.packet.t2);
  now_us=probe.ready_us;step();assert(probe.tx_kind==RC8_FOLLOW);now_us+=258560;done();assert(probe.completed==1&&!probe.state&&!atomic_get(&faulted));
 }else if(mode==13){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX,3,44,5);step();step();done();
  struct rc8_companion c;uint8_t context[16];assert(sent_len==128&&!rc8_companion_decode(&c,context,sent,128,&test_group));
  assert(c.tz.id==2&&c.tz.offset_min==-300&&c.time_valid);
  sent[108]=sent[109]=0;assert(!r2_group_mac(sent+112,&test_group,"PTOBR8D5",sent,112));
  assert(!rc8_companion_decode(&c,context,sent,128,&test_group)&&!c.tz.id&&c.time_valid);
 }else if(mode==14){
  struct rc8_probe_packet p={.kind=RC8_OFFER,.offer=1,.nonce=1,.t2=1};memset(p.context,1,16);
  uint8_t w[80],bad[80];assert(!rc8_probe_encode(w,&p,&test_group));
  for(unsigned i=0;i<80;i++)for(unsigned b=0;b<8;b++){
   memcpy(bad,w,80);bad[i]^=1u<<b;struct rc8_probe_packet decoded;memset(&decoded,0xa5,sizeof decoded);
   assert(rc8_probe_decode(&decoded,bad,80,&test_group));const uint8_t *z=(const uint8_t *)&decoded;
   for(unsigned j=0;j<sizeof decoded;j++)assert(!z[j]);
  }
 }else if(mode==15){
  (void)request();now_us=probe.deadline+1;step();assert(probe.expired==1&&!probe.state&&!probe.tx_kind&&!atomic_get(&faulted)&&probe.next_us>now_us);
 }else if(mode==16){
  (void)request();r2_lora_control(false,true,RC8_SELECT_LOCAL_GM);step();assert(!probe.state&&!probe.cal_until&&!atomic_get(&faulted));
 }else if(mode==19){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);
  for(unsigned bad=0;bad<4;bad++){
   r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX,3,44,5);assert(source_tz.value.id==2);
   r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX,bad==0?0:bad==1?4:3,bad==2?45:44,bad==3?6:5);assert(!source_tz.value.id&&!source_tz.until_us);
  }
  r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX,3,44,5);map.scope.root=4;map.scope.root_boot=45;map.scope.source_epoch=6;r2_lora_source_publish(&map);
  assert(!source_tz.value.id);r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX,3,44,5);assert(!source_tz.value.id);
  r2_lora_tz_publish((struct r2_tz){2,-360},UINT64_MAX,4,45,6);assert(source_tz.value.id==2&&source_tz.value.offset_min==-360);
 }else if(mode==22){
  (void)ready_client();uint64_t at=now_us;r2_lora_rtt_lab(true);step();assert(probe_lab(now_us)&&probe.next_us==at);
  uint64_t deadline=probe.lab_until;r2_lora_control(false,true,RC8_SELECT_BLE);step();assert(!probe_client_ready(now_us)&&!probe.state);
  r2_lora_request(false);step();step();now_us=deadline;step();assert(!probe.lab_until&&!probe_lab(now_us)&&!atomic_get(&faulted));
  assert(probe.next_us==deadline); /* Expiry works OFF; never-armed first request remains due. */
 }else if(mode==24){
  (void)request();r2_lora_rtt_lab(true);step();now_us=probe.deadline+1;uint64_t at=now_us;step();
  assert(probe.expired==1&&!probe.state&&!atomic_get(&faulted));
  assert(probe.next_us>=at+5000000&&probe.next_us<at+15000000);
  at=now_us;r2_lora_rtt_lab(false);step();assert(!probe.lab_until&&probe.next_us==at+PROBE_PERIOD_US);
 }else if(mode==26){
  (void)request();assert(now_us<60000000&&probe.serial==1); /* No manual due-time override. */
  now_us=probe.deadline+1;uint64_t at=now_us;step();
  assert(probe.expired==1&&probe.next_us>=at+PROBE_PERIOD_US&&!atomic_get(&faulted));
  struct rc8_probe_packet p={.kind=RC8_OFFER,.offer=2,.nonce=2,.t2=role.rx.highest_sequence};memcpy(p.context,role.rx.context,16);
  feed_probe(&p,now_us+1000);assert(!probe.state&&!probe.tx_kind&&probe.serial==1);
 }else if(mode==27){
  (void)ready_client();witness_enabled=true;r2_lora_shadow_publish();assert(witness_published==1&&witness_map.lora);
  uint32_t accepted=role.rx.accepted;struct rc8_link_fit fit=role.rx.fit;
  step();r2_lora_shadow_publish();assert(witness_published==1); /* Polling cannot churn history or renew a map. */
  assert(!memcmp(&fit,&role.rx.fit,sizeof fit)&&role.rx.accepted==accepted);
  r2_lora_control(false,true,RC8_SELECT_BLE);step();r2_lora_shadow_publish();assert(witness_withdrawn&&!shadow_key.tick&&witness_published==1);
  assert(!probe.completed&&!atomic_get(&faulted)); /* Observation grants no time/RTT authority. */
 }else if(mode==29){
  (void)ready_client();struct rc8_probe_packet p={.kind=RC8_OFFER,.offer=1,.nonce=7654,.t2=role.rx.highest_sequence};memcpy(p.context,role.rx.context,16);
  uint64_t edge=21400000,observed=edge+300000;feed_delayed_probe(&p,edge,observed);
  assert(probe.state==PROBE_CLIENT_READY&&probe.ready_us>=observed+500000&&probe.ready_us<observed+1000000);
  assert(probe.deadline==observed+PROBE_WINDOW_US&&!probe.t1&&!probe.t4); /* Completion observation is coarse only. */
 }else if(mode==30||mode==32||mode==33||mode==34||(mode>=35&&mode<=41)){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);step();step();done();done();
  now_us+=60000000;map=source(now_us*16,2);r2_lora_source_publish(&map);step();
  assert(role.tx.pending&&!role.tx.metadata);uint64_t start=now_us;
  probe.next_us=now_us;role.tx.diag_not_before=0;published.diagnostic_events=0;role.tx.meta_us=UINT64_MAX;role.tx.repeat_us=0;
  if(mode==32)events(RC8_D_TZ);
  if(mode==33)r2_lora_name_publish("Due Sender");
  if(mode==34)role.tx.next_us=now_us;
  queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_TX_DONE,.tick=start*16,.capture_epoch=7,.generation=sent_generation};have_event=true;now_us+=300000;step();
  if(mode==34){assert(role.tx.pending&&!role.tx.metadata&&!probe.tx_kind);return 0;} /* Ordinary timing still wins. */
  assert(probe.tx_kind==RC8_OFFER&&!role.tx.pending); /* A due full-slot offer precedes advisory work. */
  uint64_t offer_start=now_us,observed=offer_start+300000,limit=role.tx.next_us-PROBE_GUARD_US;
  if(mode==35)observed=limit-100000;
  if(mode==36)observed=limit+1;
  queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_TX_DONE,.tick=offer_start*16,.capture_epoch=7,.generation=sent_generation};have_event=true;now_us=observed;step();
  if(mode==36){assert(!probe.state&&!probe.tx_kind&&!atomic_get(&faulted));return 0;}
  assert(probe.state==PROBE_SERVER_LISTEN&&probe.ready_us==observed);
  assert(probe.deadline==((observed+PROBE_WINDOW_US<limit)?observed+PROBE_WINDOW_US:limit));
  if(mode>=37){
   uint64_t close=probe.deadline,edge=close-50000;
   if(mode==38)edge=close+1;
   now_us=edge-1;map=source(now_us*16,3);r2_lora_source_publish(&map);
   struct rc8_probe_packet p={.kind=RC8_REQUEST,.offer=probe.packet.offer,.client=2,.boot=222,.nonce=333};memcpy(p.context,probe.context,16);
   queued=(struct r2_lora_hw_event){.kind=R2_LORA_HW_RX_DONE,.tick=edge*16,.capture_epoch=mode==40?8:7,.generation=mode==39?generation+1:generation,.length=80};
   assert(!rc8_probe_encode(queued.frame,&p,&test_group));have_event=hold_completion=true;now_us=mode==41?edge+450001:close+2;step();
   if(mode!=37){assert(!probe.state&&!probe.tx_kind&&!atomic_get(&faulted));return 0;}
   assert(probe.state==PROBE_SERVER_LISTEN&&probe.deadline==close); /* Same captured header may still be receiving. */
   hold_completion=false;now_us=edge+200000;step();
   assert(probe.state==PROBE_SERVER_REPLY&&probe.packet.t2&&probe.deadline==edge+PROBE_LIFETIME_US&&!atomic_get(&faulted));
  }
 }else if(mode==31){
  struct rc8_probe_packet p=request();uint64_t t1=probe.t1,deadline=probe.deadline;
  assert(deadline==t1/16+PROBE_LIFETIME_US);
  p.kind=RC8_RESPONSE;p.t2=UINT64_C(10000000000)+t1*125/2+10000;
  feed_delayed_probe(&p,t1/16+500020,t1/16+800020);
  assert(probe.state==PROBE_CLIENT_FOLLOW&&probe.t4==t1+8000320&&probe.deadline==deadline);
 }else if(mode==42||mode==46){
  struct rc8_scope s=ready_client();probe.serial=9;probe.cal_until=now_us+PROBE_CAL_US;probe.next_us=now_us+900000000;
  uint64_t lost=probe.lost_us;unsigned tx=txs;s.root=4;s.root_boot=45;s.source_epoch=6;
  struct rc8_packet p={.sequence=role.rx.highest_sequence+1,.root=4,.source_epoch=6,.scale=s.scale,.kind=s.kind};uint8_t w[80];
  assert(!rc8_context_id(p.context,&s,&test_group)&&!rc8_encode(w,&p,&test_group));uint64_t changed=now_us+1000;packet(w,80,changed);
  assert(probe.next_us==changed&&!probe.cal_until&&probe.serial==9&&probe.lost_us==lost&&!role.rx.metadata_known&&tx==txs);
  if(mode==46){
   uint64_t armed=probe.next_us;p.sequence++;assert(!rc8_encode(w,&p,&test_group));packet(w,80,now_us+1000);
   companion(s,true,1,now_us+1000);assert(role.rx.metadata_known&&probe.next_us==armed&&probe.serial==9&&tx==txs&&!probe_client_ready(now_us));
  }
 }else if(mode==43){
  struct rc8_scope s=ready_client();probe.serial=9;probe.next_us=now_us+900000000;uint64_t next=probe.next_us;
  companion(s,false,2,now_us+1000);companion(s,false,3,now_us+1000);companion(s,true,4,now_us+1000);
  assert(probe.next_us==next&&probe.serial==9&&!probe.state&&!probe.tx_kind&&!atomic_get(&faulted));
 }else if(mode==44){
  (void)request();now_us=probe.deadline+1;step();uint64_t next=probe.next_us;uint32_t serial=probe.serial;
  assert(probe.expired==1&&next>now_us);
  struct rc8_scope s=role.rx.scope;struct rc8_packet p={.sequence=role.rx.highest_sequence+1,.root=s.root,.source_epoch=s.source_epoch,.scale=s.scale,.kind=s.kind};uint8_t w[80];
  assert(!rc8_context_id(p.context,&s,&test_group)&&!rc8_encode(w,&p,&test_group));packet(w,80,now_us+1000);
  assert(probe.next_us==next&&probe.serial==serial); /* Same context cannot bypass failed-attempt backoff. */
 }else if(mode==45){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);step();step();done();
  uint64_t next=probe.next_us;map.scope.root=4;map.scope.root_boot=45;map.scope.source_epoch=6;map.from_tick=map.anchor_tick=now_us*16;r2_lora_source_publish(&map);step();
  assert(probe.next_us==next&&!probe.tx_kind); /* Source opportunity floor survives context turnover. */
 }else assert(false);
 printf("PASS optional probe mode%d; GM/RC watch only, no normal timing authority\n",mode);return 0;
}
