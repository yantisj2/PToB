/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_rc8_policy.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=UINT64_C(0x0102030405060708),.epoch=7,
 .psk={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31}};
static struct rc8_scope scope(void){return (struct rc8_scope){.emitter=0x101,.emitter_boot=0x202,
 .stream=0x303,.root=0x404,.root_boot=0x505,.source_epoch=6,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};}
static struct rc8_packet packet(uint32_t seq){struct rc8_scope s=scope();struct rc8_packet p={.sequence=seq,.root=s.root,.source_epoch=s.source_epoch,.scale=s.scale,.kind=s.kind};
 assert(!rc8_context_id(p.context,&s,&group));return p;}
static struct rc8_correction correction(uint32_t seq,uint64_t ns){return (struct rc8_correction){.sequence=seq,
 .map_generation=9,.event_ns=ns,.created_ns=ns-100000000,.expires_ns=ns+2000000000,.map_age_us=1100,.upstream_bound_ns=RC8_UNKNOWN};}
static struct rc8_event event(const struct rc8_receiver *s,uint64_t us){return (struct rc8_event){.tick=us*16,
 .capture_epoch=77,.local_us=us,.generation=s->generation,.kind=RC8_EVENT_RX_DONE};}
static bool raw(struct rc8_receiver *s,const uint8_t *wire,size_t n,uint64_t rx,uint64_t service){
 assert(rc8_receiver_freeze(s,wire,n,event(s,rx)));return rc8_receiver_process(s,&group,service);}
static void metadata(struct rc8_receiver *s,struct rc8_scope value,uint64_t rx){uint8_t wire[RC8_META_BYTES];
 struct rc8_companion p={.scope=value,.sequence=s->diagnostic_sequence+1,.timing_sequence=s->highest_sequence,.time_valid=true};assert(!rc8_companion_encode(wire,&p,&group));assert(!raw(s,wire,sizeof wire,rx,rx));}
static void start(struct rc8_receiver *s,uint64_t us){const struct rc8_enrollment e={.factory=0x707,.transmitter_factory=0x101,.enrolled=true};
 assert(rc8_receiver_init(s,&e));rc8_receiver_switch(s,true);metadata(s,scope(),us);assert(s->cached);}
static bool receive(struct rc8_receiver *s,const struct rc8_packet *p,uint64_t rx,uint64_t service){uint8_t wire[RC8_BYTES];
 assert(!rc8_encode(wire,p,&group));return raw(s,wire,sizeof wire,rx,service);}
static void anchor(struct rc8_receiver *s,uint64_t us,uint64_t ns){struct rc8_bootstrap b={.scope=s->scope,
 .at_us=us,.until_us=us+RC8_ANCHOR_MAX_US,.lower_ns=ns,.upper_ns=ns+1000000,.rate_bound_ppb=100000,.min_sequence=1};
 assert(rc8_receiver_bootstrap(s,&b,us));}
static void codec(void){
 struct rc8_scope s=scope(),decoded;uint8_t meta[RC8_META_BYTES],id[16];assert(!rc8_metadata_encode(meta,&s,&group));
 assert(!rc8_metadata_decode(&decoded,id,meta,sizeof meta,&group));assert(rc8_scope_equal(&s,&decoded));
 struct rc8_packet p=packet(2),q;p.corrected=true;p.correction=correction(1,1001000000);
 uint8_t wire[RC8_BYTES];assert(!rc8_encode(wire,&p,&group));assert(!rc8_decode(&q,wire,sizeof wire,&group));
 assert(q.correction.event_ns==1001000000&&!memcmp(q.context,id,16));
 for(unsigned which=0;which<2;which++){
  size_t n=which?RC8_BYTES:RC8_META_BYTES;const uint8_t *original=which?wire:meta;uint8_t changed[RC8_META_BYTES];
  for(size_t i=0;i<n;i++)for(unsigned bit=0;bit<8;bit++){
   memcpy(changed,original,n);changed[i]^=(uint8_t)(1u<<bit);
   if(which){assert(rc8_decode(&q,changed,n,&group));assert(!q.sequence);}
   else{assert(rc8_metadata_decode(&decoded,id,changed,n,&group));assert(!decoded.emitter);}
  }
  for(size_t length=0;length<RC8_META_BYTES+2;length++)if(length!=n){
   if(which)assert(rc8_decode(&q,original,length,&group));
   else assert(rc8_metadata_decode(&decoded,id,original,length,&group));
  }
 }
 struct r2_group_config bad=group;bad.epoch++;assert(rc8_decode(&q,wire,sizeof wire,&bad)==-EACCES);
 bad=group;bad.id++;assert(rc8_metadata_decode(&decoded,id,meta,sizeof meta,&bad)==-EACCES);
 bad=group;bad.psk[0]++;assert(rc8_decode(&q,wire,sizeof wire,&bad)==-EACCES);
 p.corrected=false;assert(rc8_encode(wire,&p,&group));p.correction=(struct rc8_correction){0};assert(!rc8_encode(wire,&p,&group));
 p.sequence=0;assert(rc8_encode(wire,&p,&group));p=packet(2);p.corrected=true;p.correction=correction(2,1001000000);assert(rc8_encode(wire,&p,&group));
 s.kind=RC8_OWN_GM;assert(rc8_metadata_encode(meta,&s,&group));s.root=s.emitter;s.root_boot=0x909;assert(!rc8_metadata_encode(meta,&s,&group));assert(!rc8_metadata_decode(&decoded,id,meta,sizeof meta,&group)&&decoded.root_boot!=decoded.emitter_boot);
 meta[110]=1;assert(!r2_group_mac(meta+112,&group,"PTOBR8D3",meta,112));assert(rc8_metadata_decode(&decoded,id,meta,sizeof meta,&group));
}
static void exports_and_permission(void){
 struct rc8_enrollment tx={.factory=0x101,.transmitter_factory=0x101,.enrolled=true},rx=tx;rx.factory=0x707;
 assert(rc8_requests_default(&tx).tx&&!rc8_requests_default(&tx).rx);assert(rc8_requests_default(&rx).rx);
 for(int profile=0;profile<2;profile++)for(int requested=0;requested<2;requested++){
  assert(rc8_command_allowed(&tx,profile,requested,RC8_CMD_TX)==(profile==RC8_PROFILE_GM&&requested));
  assert(!rc8_command_allowed(&rx,profile,requested,RC8_CMD_TX));assert(!rc8_command_allowed(&rx,profile,requested,RC8_CMD_RANGING));
 }
 tx.enrolled=false;assert(!rc8_requests_default(&tx).tx);tx.enrolled=true;
 struct rc8_export selected={.scope=scope(),.from_tick=16000000,.until_tick=48000000,.anchor_tick=16000000,
 .anchor_ns=1000000000,.capture_epoch=55,.created_ns=900000000,.expires_ns=3000000000,.map_generation=9,
 .map_age_us=100,.upstream_bound_ns=RC8_UNKNOWN,.authenticated=true,.valid=true},own=selected,out;
 own.scope.kind=RC8_OWN_GM;own.scope.root=own.scope.emitter;own.scope.root_boot=own.scope.emitter_boot;
 assert(rc8_export_select(&out,&own,&selected,true,0x101,0x202)&&out.scope.root==0x101);
 assert(rc8_export_select(&out,&own,&selected,false,0x101,0x202)&&out.scope.root==0x404);
 selected.valid=false;assert(!rc8_export_select(&out,&own,&selected,false,0x101,0x202));selected.valid=true;
 selected.authenticated=false;assert(!rc8_export_select(&out,&own,&selected,false,0x101,0x202));selected.authenticated=true;
 struct rc8_transmission t={0};
 assert(!rc8_tx_prepare(&t,&rx,RC8_PROFILE_GM,true,&selected,&group,1,1,16000000,55));
 assert(!rc8_tx_prepare(&t,&tx,RC8_PROFILE_RC,true,&selected,&group,1,1,16000000,55));
 assert(rc8_tx_prepare(&t,&tx,RC8_PROFILE_GM,true,&selected,&group,1,1,16000000,55));assert(!t.packet.corrected);
 struct rc8_event e={.tick=16016000,.capture_epoch=55,.generation=2,.kind=RC8_EVENT_TX_DONE};struct rc8_correction c;
 assert(!rc8_tx_complete(&c,&t,e)&&t.pending);e.generation=1;e.kind=RC8_EVENT_CALLBACK;assert(!rc8_tx_complete(&c,&t,e)&&t.pending);
 e.kind=RC8_EVENT_TX_DONE;e.capture_epoch=56;assert(!rc8_tx_complete(&c,&t,e)&&t.pending);e.capture_epoch=55;
 selected.anchor_ns++;assert(rc8_tx_complete(&c,&t,e)&&c.event_ns==1001000000);assert(c.map_age_us==1100);
 assert(!rc8_tx_prepare(&t,&tx,RC8_PROFILE_GM,true,&selected,&group,1,2,16000000,55));
 assert(rc8_tx_prepare(&t,&tx,RC8_PROFILE_GM,true,&selected,&group,2,2,16000000,55));assert(t.packet.corrected&&t.packet.correction.sequence==1);
 assert(rc8_export_close(&t.map,16016000,55));e.generation=2;assert(!rc8_tx_complete(&c,&t,e));
 selected.scope.source_epoch++;assert(!rc8_tx_prepare(&t,&tx,RC8_PROFILE_GM,true,&selected,&group,3,3,16000000,55));
 selected.scope.stream++;assert(rc8_tx_prepare(&t,&tx,RC8_PROFILE_GM,true,&selected,&group,1,3,16000000,55));assert(!t.packet.corrected);
 e.generation=3;e.tick=32000001;assert(!rc8_tx_complete(&c,&t,e)); /* >1s cannot be reported */
 assert(!rc8_export_close(&selected,17000000,54));assert(rc8_export_close(&selected,17000000,55));assert(rc8_export_close(&selected,48000000,55));assert(selected.until_tick==17000000);
}
static void functional_and_freshness(void){
 struct rc8_receiver s;start(&s,1000000);struct rc8_packet p=packet(1);assert(!receive(&s,&p,1001000,1001000));
 p=packet(2);p.corrected=true;p.correction=correction(1,1001000000);assert(receive(&s,&p,5001000,5001000));
 assert(rc8_receiver_state(&s,5001000)==RC8_AVAILABLE);assert(rc8_receiver_assurance(&s,5001000)==RC8_FRESHNESS_UNKNOWN);
 assert(rc8_receiver_authority(&s,5001000)==RC8_AUTHORITY_UNKNOWN&&rc8_output_bound(&s)==RC8_UNKNOWN);
 assert(rc8_select(false,RC8_AVAILABLE,RC8_FRESHNESS_UNKNOWN,true)==RC8_SELECT_LORA);
 assert(rc8_select(false,RC8_AVAILABLE,RC8_FRESHNESS_UNKNOWN,false)==RC8_SELECT_NONE);
 assert(rc8_select(true,RC8_AVAILABLE,RC8_FRESHNESS_UNKNOWN,true)==RC8_SELECT_BLE);
 uint64_t expiry=s.sample.tx.expires_ns,observed=s.sample.rx.local_us;assert(!receive(&s,&p,5002000,5002000));assert(s.replayed==1);
 metadata(&s,scope(),6000000);assert(s.sample.tx.expires_ns==expiry&&s.sample.rx.local_us==observed&&s.highest_sequence==2);
 assert(rc8_receiver_state(&s,13001000)==RC8_EXPIRED);struct rc8_sample read;struct rc8_scope root;
 assert(!rc8_receiver_read(&s,13001000,&read,&root)&&!read.valid);
 /* Perfect ongoing LoRa alone works beyond12s and beyond1h. Optional anchor
  * ages into continuity; data/metadata do not renew its original instant. */
 start(&s,1000000);anchor(&s,1000000,1000000000);
 for(uint32_t seq=1;seq<=920;seq++){
  uint64_t us=1001000+(uint64_t)(seq-1)*4000000;
  if(seq%15==0)metadata(&s,scope(),us-1);
  p=packet(seq);
  if(seq>1){p.corrected=true;p.correction=correction(seq-1,1001000000+(uint64_t)(seq-2)*4000000000);}
  bool accepted=receive(&s,&p,us,us);assert(accepted==(seq>1));
  if(seq>1){assert(rc8_receiver_state(&s,us)==RC8_AVAILABLE);assert(s.trust.at_us==1000000);}
  if(seq==2){assert(rc8_receiver_assurance(&s,us)==RC8_ANCHORED_INTERVAL);assert(rc8_receiver_authority(&s,us)==RC8_ORIGINAL_ENVELOPE_EXPIRED);}
 }
 uint64_t now=1001000+UINT64_C(919)*4000000;
 assert(rc8_receiver_assurance(&s,now)==RC8_STREAM_CONTINUITY);assert(rc8_receiver_authority(&s,now)==RC8_AUTHORITY_UNKNOWN);
 assert(rc8_select(false,RC8_AVAILABLE,RC8_STREAM_CONTINUITY,true)==RC8_SELECT_LORA);
 p=packet(921);assert(!receive(&s,&p,now+16000000,now+16000000));
 p=packet(922);p.corrected=true;p.correction=correction(921,(now+16000000)*1000);
 assert(receive(&s,&p,now+20000000,now+20000000));assert(rc8_receiver_assurance(&s,now+20000000)==RC8_FRESHNESS_UNKNOWN);
 /* A full replay after cold boot may look functional; label UNKNOWN, never
  * invent absolute freshness. A genuinely independent anchor rejects it. */
 start(&s,1000000000);p=packet(1);assert(!receive(&s,&p,1000001000,1000001000));
 p=packet(2);p.corrected=true;p.correction=correction(1,1001000000);assert(receive(&s,&p,1004001000,1004001000));
 assert(rc8_receiver_assurance(&s,1004001000)==RC8_FRESHNESS_UNKNOWN);
 start(&s,1000000000);anchor(&s,1000000000,1000000000000);p=packet(1);assert(!receive(&s,&p,1000001000,1000001000));
 p=packet(2);p.corrected=true;p.correction=correction(1,1001000000);assert(!receive(&s,&p,1004001000,1004001000));assert(!s.sample.valid);
 start(&s,1000000);anchor(&s,1000000,1000000000);p=packet(1);assert(!receive(&s,&p,1001000,1001000));
 p=packet(2);p.corrected=true;p.correction=correction(1,1001000000);assert(receive(&s,&p,5001000,5001000));
 p=packet(3);p.correction=(struct rc8_correction){0};uint8_t wire[RC8_BYTES];assert(!rc8_encode(wire,&p,&group));
 struct rc8_event changed=event(&s,9001000);changed.capture_epoch=88;assert(rc8_receiver_freeze(&s,wire,sizeof wire,changed));
 assert(!rc8_receiver_process(&s,&group,9001000)&&!s.sample.valid&&!s.anchored&&!s.continuity);
}
static void cache_and_faults(void){
 struct rc8_receiver s;start(&s,1000000);struct rc8_packet p=packet(1);
 assert(!receive(&s,&p,1001000,1001000));uint32_t high=s.highest_sequence;
 struct rc8_scope changed=scope();changed.root++;metadata(&s,changed,2000000);assert(s.highest_sequence==high&&s.replayed==1);
 changed=scope();changed.stream--;metadata(&s,changed,2000000);assert(s.scope.stream==0x303);
 changed=scope();changed.stream++;changed.root++;metadata(&s,changed,3000000);assert(s.scope.root==changed.root&&!s.sample.valid&&s.highest_sequence==0);
 assert(!receive(&s,&p,3001000,3001000)&&s.replayed>=1); /* exact full context */
 start(&s,1000000);p=packet(UINT32_MAX);assert(!receive(&s,&p,1001000,1001000));metadata(&s,scope(),2000000);
 p=packet(1);assert(!receive(&s,&p,2001000,2001000)&&s.replayed==1);
 metadata(&s,scope(),200000000);assert(s.highest_sequence==UINT32_MAX); /* expiry cannot reset replay floor */
 changed=scope();changed.stream=UINT64_MAX;metadata(&s,changed,200000001);changed.stream=1;metadata(&s,changed,200000002);assert(s.scope.stream==UINT64_MAX);
 start(&s,1000000);p=packet(1);assert(!receive(&s,&p,1001000,1001000));changed=scope();changed.emitter_boot++;
 metadata(&s,changed,2000000);assert(s.scope.emitter_boot==0x202);metadata(&s,changed,14001000);assert(s.scope.emitter_boot==0x203&&!s.anchored);
 metadata(&s,scope(),28001000);assert(s.scope.emitter_boot==0x203); /* immediate retired boot */
 start(&s,1000000);assert(!receive(&s,&p,122000000,122000000)&&s.highest_sequence==1);
 metadata(&s,scope(),122000001);assert(!receive(&s,&p,122001000,122001000)&&s.highest_sequence==1);
 /* Missing metadata cannot create context or timing. */
 const struct rc8_enrollment e={.factory=0x707,.transmitter_factory=0x101,.enrolled=true};assert(rc8_receiver_init(&s,&e));rc8_receiver_switch(&s,true);
 assert(!receive(&s,&p,1001000,1001000)&&s.cached&&!s.metadata_known&&!s.scope.root_boot);
 /* Fixed payload/event copy and drop-newest queue/history behavior. */
 start(&s,1000000);uint8_t wire[RC8_BYTES];assert(!rc8_encode(wire,&p,&group));assert(rc8_receiver_freeze(&s,wire,sizeof wire,event(&s,1001000)));
 memset(wire,0,sizeof wire);assert(!rc8_receiver_freeze(&s,wire,sizeof wire,event(&s,1002000)));assert(!rc8_receiver_process(&s,&group,1003000));
 assert(s.history[0].sequence==1&&s.history[0].rx.local_us==1001000&&s.overflow==1);
 p=packet(2);assert(!receive(&s,&p,2001000,2001000));p=packet(3);assert(!receive(&s,&p,3001000,3001000)&&s.overflow==2);
 start(&s,1000000);p=packet(1);assert(!receive(&s,&p,1001000,1001000));p=packet(2);p.corrected=true;p.correction=correction(1,1001000000);
 assert(!receive(&s,&p,10001000,10001000)&&!s.sample.valid); /* >8s correction */
 start(&s,1000000);assert(!receive(&s,&p,5001000,5001000)&&!s.sample.valid); /* no retained K */
 uint32_t gen=s.generation;rc8_receiver_switch(&s,false);assert(!s.cached&&!s.sample.valid);rc8_receiver_switch(&s,true);
 struct rc8_event old=event(&s,6000000);old.generation=gen;assert(!rc8_receiver_freeze(&s,wire,sizeof wire,old));
 old=event(&s,6000000);old.kind=RC8_EVENT_CALLBACK;assert(!rc8_receiver_freeze(&s,wire,sizeof wire,old));
 s.generation=UINT32_MAX;rc8_receiver_switch(&s,true);assert(!s.enabled);
 start(&s,1000000);changed=scope();changed.root=0x707;metadata(&s,changed,2000000);assert(s.scope.root==0x404);changed=scope();changed.emitter++;metadata(&s,changed,2000000);assert(s.scope.emitter==0x101);
 struct rc8_bootstrap bad={.scope=scope(),.at_us=3000000,.until_us=3000000+RC8_ANCHOR_MAX_US+1,.lower_ns=1,.upper_ns=2,.min_sequence=1};
 assert(!rc8_receiver_bootstrap(&s,&bad,3000000));bad.until_us--;bad.upper_ns=100000002;assert(!rc8_receiver_bootstrap(&s,&bad,3000000));
 bad.upper_ns=2;bad.rate_bound_ppb=100001;assert(!rc8_receiver_bootstrap(&s,&bad,3000000));
 struct rc8_enrollment tx=e;tx.factory=tx.transmitter_factory;assert(rc8_receiver_init(&s,&tx));rc8_receiver_switch(&s,true);assert(!s.enabled);
}
static void companion_and_rounding(void){
 struct rc8_packet p=packet(2),q;p.corrected=true;p.correction=correction(1,1001000123);
 p.correction.created_ns-=999;p.correction.expires_ns+=789;
 uint8_t wire[RC8_META_BYTES];assert(!rc8_encode(wire,&p,&group));assert(!rc8_decode(&q,wire,RC8_BYTES,&group));
 assert(q.root==p.root&&q.source_epoch==p.source_epoch&&q.correction.event_ns==p.correction.event_ns);
 assert(q.correction.created_ns>=p.correction.created_ns&&q.correction.created_ns-p.correction.created_ns<1000);
 assert(q.correction.expires_ns<=p.correction.expires_ns&&p.correction.expires_ns-q.correction.expires_ns<1000);
 p.correction.expires_ns=p.correction.event_ns+999;assert(rc8_encode(wire,&p,&group));
 p.correction=correction(1,UINT64_C(5000000000000));p.correction.created_ns=0;assert(rc8_encode(wire,&p,&group));
 p.correction=correction(1,1001000000);p.correction.expires_ns=p.correction.event_ns+(UINT64_C(1)+UINT32_MAX)*1000;assert(rc8_encode(wire,&p,&group));
 struct rc8_receiver r;const struct rc8_enrollment e={.factory=0x707,.transmitter_factory=0x101,.enrolled=true};
 assert(rc8_receiver_init(&r,&e));rc8_receiver_switch(&r,true);
 p=packet(1);assert(!receive(&r,&p,1000000,1000000));
 p=packet(2);p.corrected=true;p.correction=correction(1,1000000000);assert(receive(&r,&p,5000000,5000000));
 assert(r.cached&&!r.metadata_known&&!r.scope.emitter_boot&&!r.scope.root_boot&&!r.scope.parent&&!r.scope.stream);
 struct rc8_bootstrap unknown={.scope=r.scope,.at_us=5000000,.until_us=6000000,.lower_ns=5000000000,.upper_ns=5000000001,.min_sequence=3};
 assert(!rc8_receiver_bootstrap(&r,&unknown,5000000));
 assert(r.sample.rx.local_us==1000000&&rc8_receiver_assurance(&r,5000000)==RC8_FRESHNESS_UNKNOWN);
 struct rc8_scope reordered=scope();reordered.stream++;reordered.source_epoch++;
 struct rc8_packet earlier=packet(9);earlier.source_epoch=reordered.source_epoch;assert(!rc8_context_id(earlier.context,&reordered,&group));
 uint8_t retained[16];memcpy(retained,r.context,16);assert(!receive(&r,&earlier,4000000,5100000));
 assert(!memcmp(retained,r.context,16)&&r.sample.valid&&r.highest_sequence==2);
 struct rc8_companion d={.scope=scope(),.sequence=1,.timing_sequence=2,.events=RC8_D_QUALITY,.time_valid=true,
  .quality={.known=RC8_Q_LOCK|RC8_Q_PARENT_CLASS,.values=RC8_Q_LOCK,.parent_class=6,.correction_age_ms=55}};
 uint8_t id[16],first[16];assert(!rc8_context_id(first,&d.scope,&group));
 assert(!rc8_companion_encode(wire,&d,&group));assert(!raw(&r,wire,RC8_META_BYTES,5100000,5100000));
 assert(r.metadata_known&&r.scope.root_boot==d.scope.root_boot&&r.highest_sequence==2&&r.sample.rx.local_us==1000000);
 uint64_t hold=r.sample.rx.local_us;uint32_t high=r.highest_sequence;
 d.sequence++;d.quality.parent_class=7;d.quality.values=0;assert(!rc8_companion_encode(wire,&d,&group));
 struct rc8_companion decoded;assert(!rc8_companion_decode(&decoded,id,wire,RC8_META_BYTES,&group));assert(!memcmp(id,first,16));
 assert(!raw(&r,wire,RC8_META_BYTES,5200000,5200000));assert(r.highest_sequence==high&&r.sample.rx.local_us==hold);
 /* A stale loss snapshot cannot withdraw a subsequently received timing. */
 p=packet(3);p.corrected=true;p.correction=correction(2,5000000000);assert(receive(&r,&p,9000000,9000000));
 d.sequence++;d.time_valid=false;d.events=RC8_D_LOSS;d.timing_sequence=2;assert(!rc8_companion_encode(wire,&d,&group));
 assert(!raw(&r,wire,RC8_META_BYTES,9100000,9100000));assert(r.sample.valid&&r.highest_sequence==3);
 d.sequence++;d.timing_sequence=3;assert(!rc8_companion_encode(wire,&d,&group));assert(!raw(&r,wire,RC8_META_BYTES,9200000,9200000));
 assert(!r.sample.valid&&r.withdrawal_sequence==3&&!r.history[0].occupied&&!r.history[1].occupied);
 assert(!raw(&r,wire,RC8_META_BYTES,9300000,9300000)&&!r.sample.valid); /* reordered same diagnostic */
 p=packet(4);p.corrected=true;p.correction=correction(3,9000000000);assert(!receive(&r,&p,13000000,13000000));
 p=packet(5);p.corrected=true;p.correction=correction(4,13000000000);assert(receive(&r,&p,17000000,17000000));
 /* Old context/parent data cannot erase a newer context's sample/history. */
 struct rc8_scope newer=scope();newer.parent=0x999;newer.parent_boot=0xaaa;newer.stream++;
 p=packet(6);assert(!rc8_context_id(p.context,&newer,&group));assert(!receive(&r,&p,21000000,21000000));
 assert(!r.metadata_known&&!r.sample.valid&&r.highest_sequence==6);
 d.sequence++;d.timing_sequence=5;assert(!rc8_companion_encode(wire,&d,&group));assert(!raw(&r,wire,RC8_META_BYTES,21100000,21100000));
 assert(r.highest_sequence==6&&!memcmp(r.context,p.context,16));
 d.scope=newer;d.sequence=1;d.timing_sequence=6;d.time_valid=true;
 assert(!rc8_companion_encode(wire,&d,&group));assert(!raw(&r,wire,RC8_META_BYTES,21200000,21200000));assert(r.scope.parent==0x999&&r.highest_sequence==6);
 assert(rc8_receiver_state(&r,UINT64_C(999999999))!=RC8_AVAILABLE);
 /* A companion can itself introduce a genuinely different physical boot
  * after the old hold gap, even though its timing sequence restarted. */
 start(&r,1000000);p=packet(100);assert(!receive(&r,&p,1001000,1001000));
 d=(struct rc8_companion){.scope=scope(),.sequence=1,.timing_sequence=1,.time_valid=true};d.scope.emitter_boot++;d.scope.stream=1;
 assert(!rc8_companion_encode(wire,&d,&group));assert(!raw(&r,wire,RC8_META_BYTES,2000000,2000000));assert(r.scope.emitter_boot==0x202);
 assert(!raw(&r,wire,RC8_META_BYTES,14001000,14001000));assert(r.scope.emitter_boot==0x203&&r.highest_sequence==0&&!r.sample.valid);

}
static void no_source_diagnostic(void){
 struct rc8_companion d={.scope={.emitter=0x101,.emitter_boot=0x202,.stream=1},.sequence=1,.events=RC8_D_BOOT,.time_valid=false};
 uint8_t w[RC8_META_BYTES],id[16];struct rc8_companion q;
 assert(rc8_scope_validate(&d.scope));assert(rc8_context_id(id,&d.scope,&group));
 assert(!rc8_companion_encode(w,&d,&group));assert(!rc8_companion_decode(&q,id,w,sizeof w,&group));
 assert(q.scope.emitter_boot==0x202&&!q.scope.root&&!q.time_valid);
 struct rc8_receiver r;const struct rc8_enrollment e={.factory=0x707,.transmitter_factory=0x101,.enrolled=true};
 assert(rc8_receiver_init(&r,&e));rc8_receiver_switch(&r,true);assert(!raw(&r,w,sizeof w,1000000,1000000));
 assert(r.metadata_known&&!r.sample.valid&&rc8_receiver_state(&r,1000001)==RC8_ACQUIRING);
 d.time_valid=true;assert(rc8_companion_encode(w,&d,&group));d.time_valid=false;d.scope.root=0x404;assert(rc8_companion_encode(w,&d,&group));
 struct rc8_packet p={.sequence=1};memcpy(p.context,r.context,16);assert(rc8_encode(w,&p,&group));
}
static void closure_and_gesture(void){
 for(int phase=RC8_PHASE_IDLE;phase<=RC8_PHASE_FOLLOW;phase++)for(unsigned missing=0;missing<4;missing++){
  struct rc8_stop s={0};rc8_stop_begin(&s,phase,1000000);uint64_t deadline=s.deadline_us;rc8_stop_begin(&s,phase,1100000);assert(s.deadline_us==deadline);
  bool ack[4]={true,true,true,true};ack[missing]=false;
  assert(rc8_stop_poll(&s,deadline-1,ack[0],ack[1],ack[2],ack[3])==RC8_STOP_WAIT);
  assert(rc8_stop_poll(&s,deadline,ack[0],ack[1],ack[2],ack[3])==RC8_STOP_FAULT);
  assert(rc8_stop_poll(&s,deadline+1,true,true,true,true)==RC8_STOP_FAULT);
  s=(struct rc8_stop){0};rc8_stop_begin(&s,phase,1000000);assert(rc8_stop_poll(&s,1000001,true,true,true,true)==RC8_STOP_QUIESCENT);
  assert(rc8_stop_poll(&s,deadline+1,true,true,true,true)==RC8_STOP_QUIESCENT);
 }
 for(unsigned duration=0;duration<6000000;duration+=100000){struct rc8_gesture g={0};assert(!rc8_gesture_update(&g,true,true,100));
  assert(rc8_gesture_update(&g,true,true,100+duration)==(duration>=1500000));
  assert(!rc8_gesture_update(&g,true,true,100+duration));
  assert(!rc8_gesture_update(&g,false,true,100+duration));assert(g.claimed);}
 struct rc8_gesture exact={0};assert(!rc8_gesture_update(&exact,true,true,100));
 assert(!rc8_gesture_update(&exact,true,true,1500099));assert(rc8_gesture_update(&exact,true,true,1500100));
 assert(!rc8_gesture_update(&exact,true,true,9000100)&&exact.claimed&&exact.consumed);
 assert(!rc8_gesture_update(&exact,false,true,9000200));
 assert(!rc8_gesture_update(&exact,true,false,9000300)&&!exact.claimed&&!exact.consumed);
 struct rc8_gesture g={0};assert(!rc8_gesture_update(&g,true,false,100));assert(!rc8_gesture_update(&g,false,true,2000100));
 g=(struct rc8_gesture){0};assert(!rc8_gesture_update(&g,true,true,100));assert(!rc8_gesture_update(&g,true,false,1600100));assert(!rc8_gesture_update(&g,false,true,2000100));
}
int main(int argc,char **argv){
 if(argc==2){uint8_t wire[RC8_META_BYTES];size_t n=0;
  if(!strcmp(argv[1],"metadata")){struct rc8_scope s=scope();s.parent=0x606;s.parent_boot=0x808;
   struct rc8_companion d={.scope=s,.sequence=23,.snapshot_age_ms=17,.timing_sequence=2,.events=175,.time_valid=true,
    .quality={.known=8191,.values=13,.role=2,.hop=1,.parent_class=6,.parent_accuracy=34,.parent_bound_ns=9999,
     .correction_age_ms=17,.map_life_ms=1350,.rate_ppb=-421,.residual_ns=3800,.losses=7,.recoveries=8}};
   assert(!rc8_companion_encode(wire,&d,&group));n=RC8_META_BYTES;}
  if(!strcmp(argv[1],"timing")){struct rc8_packet p=packet(2);struct rc8_scope s=scope();s.parent=0x606;s.parent_boot=0x808;assert(!rc8_context_id(p.context,&s,&group));p.corrected=true;p.correction=correction(1,1001000000);assert(!rc8_encode(wire,&p,&group));n=RC8_BYTES;}
  assert(n);for(size_t i=0;i<n;i++)printf("%02x",wire[i]);puts("");return 0;
 }
 codec();exports_and_permission();functional_and_freshness();cache_and_faults();companion_and_rounding();no_source_diagnostic();closure_and_gesture();
 printf("RC8 wire3 PASS receiver=%zu export=%zu packet=%zu transmission=%zu stop=%zu gesture=%zu bytes;920 events continue beyond1h\n",
 sizeof(struct rc8_receiver),sizeof(struct rc8_export),sizeof(struct rc8_packet),sizeof(struct rc8_transmission),sizeof(struct rc8_stop),sizeof(struct rc8_gesture));return 0;
}
