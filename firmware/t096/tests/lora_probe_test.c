/* Actual serialized owner/codec/policy with synthetic physical boundaries.
 * Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main original_runtime_main
#include "lora_service_test.c"
#undef main
static void feed_probe(struct rc8_probe_packet *p,uint64_t at){uint8_t w[80];assert(!rc8_probe_encode(w,p,&test_group));packet(w,80,at);}
static struct rc8_scope ready_client(void){
 struct rc8_scope s=coordination_acquire();companion(s,true,1,5300000);
 for(unsigned i=3;i<=6;i++){
  struct rc8_packet p={.sequence=i,.root=s.root,.source_epoch=s.source_epoch,.scale=s.scale,.kind=s.kind,.corrected=true,
   .correction={.sequence=i-1,.event_ns=UINT64_C(9000000000)+(i-2)*UINT64_C(4000000000),.created_ns=0,
    .expires_ns=UINT64_C(9000000000)+(i-2)*UINT64_C(4000000000)+1000000000,.map_generation=i}};
  uint8_t w[80];assert(!rc8_context_id(p.context,&s,&test_group));assert(!rc8_encode(w,&p,&test_group));packet(w,80,1200000+(i-1)*4000000);
 }
 assert(role.rx.fit.result_n>=4&&probe_client_ready(now_us));probe.next_us=0;return s;
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
 if(mode<=8){
  struct rc8_probe_packet p=request();uint64_t t1=probe.t1;
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
  uint32_t accepted=role.rx.accepted;feed_probe(&p,now_us+1000);assert(probe.completed==1&&role.rx.accepted==accepted);
  struct r2_lora_view v;r2_lora_view(&v);assert(v.choice==RC8_SELECT_LORA); /* GM profile remains a watch receiver. */
  if(mode==8){r2_lora_request(false);step();assert(!probe.cal_until&&!published.view.utc_us&&!atomic_get(&faulted));}
 }else if(mode==9){
  uint32_t d;assert(rc8_rtt_delay(16000320,0,1000000000,&d)&&d==10000);
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
 }else if(mode==12||mode==17||mode==18){
  init(true);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);step();step();done();done();
  now_us+=60000000;map=source(now_us*16,2);r2_lora_source_publish(&map);step();done();
  if(role.tx.pending)done();
  assert(probe.tx_kind==RC8_OFFER);struct rc8_probe_packet offer;assert(!rc8_probe_decode(&offer,sent,80,&test_group));
  now_us+=258560;done();assert(probe.state==4);
  uint32_t before=txs;events(RC8_D_TZ);step();assert(txs==before); /* No metadata inside the request window. */
  struct rc8_probe_packet p={.kind=RC8_REQUEST,.offer=offer.offer,.client=2,.boot=222,.nonce=333};memcpy(p.context,offer.context,16);
  uint64_t original_ns=0;
  if(mode>=17){
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
  init(true);r2_lora_tz_publish(r2_tz_manual(),UINT64_MAX);struct rc8_export map=source(now_us*16,1);r2_lora_source_publish(&map);step();step();done();
  struct rc8_companion c;uint8_t context[16];assert(sent_len==128&&!rc8_companion_decode(&c,context,sent,128,&test_group));
  assert(c.tz.id==2&&c.tz.offset_min==-300&&c.time_valid);
  sent[108]=sent[109]=0;assert(!r2_group_mac(sent+112,&test_group,"PTOBR8D4",sent,112));
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
 }else assert(false);
 printf("PASS optional probe mode%d; GM/RC watch only, no normal timing authority\n",mode);return 0;
}
