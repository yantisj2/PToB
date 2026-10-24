#include "gm7_phase.h"
#include "gm7_promote.h"
#include "gm7_bridge.h"
/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_codec.h"
#include "gm7_session.h"
#include "gm7_owner.h"
#include "gm7_schedule.h"
#include "gm7_air.h"
#include "gm7_source.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "gm7_vectors.h"
static struct r2_group_config group(void){struct r2_group_config g={.id=UINT64_C(0x0102030405060708),.epoch=1};for(unsigned i=0;i<32;i++)g.psk[i]=(uint8_t)i;return g;}
static struct gm7_packet fixture(const char *name){struct r2_group_config g=group();struct gm7_packet p={0};for(unsigned i=0;i<sizeof fixtures/sizeof fixtures[0];i++)if(!strcmp(name,fixtures[i].name)){assert(!gm7_decode(&p,fixtures[i].wire,fixtures[i].size,&g));return p;}assert(0);return p;}
static struct gm7_packet authenticated(struct gm7_packet p){struct r2_group_config g=group();uint8_t raw[GM7_MAX_BYTES];assert(!gm7_encode(raw,sizeof raw,&p,&g));struct gm7_packet result={0};assert(!gm7_decode(&result,raw,gm7_wire_size(p.kind),&g));return result;}
static int random_bytes(void *context,uint8_t *out,size_t n){if(context)return -EIO;memset(out,0x5a,n);return 0;}
static void codec(void){
 struct r2_group_config g=group();unsigned mutated=0;
 for(unsigned i=0;i<sizeof fixtures/sizeof fixtures[0];i++){
  const struct fixture *f=&fixtures[i];struct gm7_packet p={0};uint8_t raw[GM7_MAX_BYTES],ad[GM7_MAX_AD_BYTES];
  assert(!gm7_decode(&p,f->wire,f->size,&g));assert(!gm7_encode(raw,sizeof raw,&p,&g));assert(!memcmp(raw,f->wire,f->size));
  assert(!gm7_ad_encode(ad,sizeof ad,&p,&g));assert(ad[0]==f->size+3&&ad[1]==255&&ad[2]==255&&ad[3]==255);struct gm7_packet q={0};assert(!gm7_ad_decode(&q,ad,f->size+4,&g));
  for(size_t j=0;j<f->size;j++)for(unsigned b=0;b<8;b++){
   memcpy(raw,f->wire,f->size);raw[j]^=(uint8_t)(1u<<b);memset(&q,0xa5,sizeof q);struct gm7_packet before=q;
   assert(gm7_decode(&q,raw,f->size,&g));assert(!memcmp(&q,&before,sizeof q));++mutated;
  }
  for(size_t j=0;j<=GM7_MAX_BYTES;j++)if(j!=f->size){memset(raw,0,sizeof raw);memcpy(raw,f->wire,f->size);assert(gm7_decode(&q,raw,j,&g));}
  struct r2_group_config foreign=g;foreign.id++;assert(gm7_decode(&q,f->wire,f->size,&foreign));foreign=g;foreign.epoch++;assert(gm7_decode(&q,f->wire,f->size,&foreign));foreign=g;memset(foreign.psk,0,32);assert(gm7_decode(&q,f->wire,f->size,&foreign));
 }
 printf("PASS GM7 exact10 independent vectors, %u bit corruptions, exact lengths, AD wrappers, foreign group/key, decode nonmutation\n",mutated);
}
static struct gm7_packet request(struct gm7_requester *r,uint64_t id,uint64_t boot,uint64_t now){struct gm7_packet p={0};assert(!gm7_request_begin(r,&p,id,boot,now,random_bytes,NULL));return authenticated(p);}
static struct gm7_packet anchor(struct gm7_packet req,bool candidate,uint64_t sampled){
 struct gm7_packet a=fixture(candidate?"candidate-anchor":"serving-anchor");a.body.anchor.requester=req.body.request.requester;a.body.anchor.requester_boot=req.body.request.requester_boot;a.body.anchor.responder=req.body.request.target;a.body.anchor.responder_boot=req.body.request.target_boot;a.body.anchor.nonce_hi=req.body.request.nonce_hi;a.body.anchor.nonce_lo=req.body.request.nonce_lo;a.body.anchor.sampled_mono_us=sampled;return authenticated(a);
}
static void sessions(void){
 struct r2_group_config g=group();struct gm7_requester r={0};struct gm7_session s={0};assert(!gm7_requester_init(&r,&g,81,97,1000000));
 struct gm7_packet req=request(&r,17,33,1000000),a=anchor(req,false,10000000);
 assert(gm7_anchor_accept(&r,&s,&a,1050000,1050000)); /* HCI enqueue is not request TX. */
 assert(!gm7_request_sent(&r,&req.body.request,1010000,1060000));assert(gm7_request_sent(&r,&req.body.request,1011000,1060000)==-EALREADY);
 assert(!gm7_anchor_accept(&r,&s,&a,1050000,1060000));assert(s.source_until==2210000&&s.until==361050000&&s.offset_lo==-8990000&&s.offset_hi==-8950000);
 struct gm7_session first=s;assert(gm7_anchor_accept(&r,&s,&a,1051000,1060000));assert(!memcmp(&s,&first,sizeof s));
 struct gm7_packet c=fixture("announce");c.body.control.sent_mono_us=10010000;c=authenticated(c);uint64_t original,until;
 assert(!gm7_session_control(&s,&c,1070000,1070000,&original,&until));assert(original<=1020000&&until<=3770000&&s.source_until<=2220000);
 uint64_t deadline=s.source_until;assert(gm7_session_control(&s,&c,1080000,1080000,&original,&until)==-EALREADY&&s.source_until==deadline);
 struct gm7_packet d=fixture("role-diagnostic");d.body.role.sampled_mono_us=10010000;d=authenticated(d);assert(!gm7_session_diagnostic(&s,&d,1070000,1070000,&until));
 for(unsigned i=0;i<3;i++){struct gm7_packet stale=d;stale.body.role.sequence=2;if(i==0)stale.body.role.source_gen++;if(i==1)stale.body.role.capture_gen++;if(i==2)stale.body.role.train_gen++;stale=authenticated(stale);assert(gm7_session_diagnostic(&s,&stale,1080000,1080000,&until));assert(s.last_diag[0]==1);}
 struct gm7_packet old=a;req=request(&r,17,33,1100000);assert(req.body.request.nonce_hi!=old.body.anchor.nonce_hi||req.body.request.nonce_lo!=old.body.anchor.nonce_lo);assert(!gm7_request_sent(&r,&req.body.request,1110000,1110000));assert(gm7_anchor_accept(&r,&s,&old,1120000,1120000));
 a=anchor(req,false,10110000);assert(!gm7_anchor_accept(&r,&s,&a,1130000,1130000));
 assert(s.last_sequence==7&&s.last_diag[0]==1);assert(gm7_session_control(&s,&c,1140000,1140000,&original,&until)==-EALREADY);
 struct gm7_packet wrong=fixture("withdraw");wrong.body.control.sent_mono_us=10120000;wrong.body.control.term++;wrong=authenticated(wrong);deadline=s.source_until;assert(gm7_session_control(&s,&wrong,1150000,1150000,&original,&until)==-EXDEV&&s.serving&&s.source_until==deadline);
 wrong=fixture("withdraw");wrong.body.control.sent_mono_us=10120000;wrong.body.control.train_gen++;wrong=authenticated(wrong);assert(gm7_session_control(&s,&wrong,1150000,1150000,&original,&until)==-EXDEV&&s.serving&&s.source_until==deadline);
 req=request(&r,17,33,200000000);assert(!gm7_request_sent(&r,&req.body.request,200000000,200000000));a=anchor(req,false,209000000);assert(!gm7_anchor_accept(&r,&s,&a,200020000,200020000));assert(s.valid);
 req=request(&r,17,33,201000000);assert(!gm7_request_sent(&r,&req.body.request,201000000,201000000));a=anchor(req,false,215000000);assert(gm7_anchor_accept(&r,&s,&a,201020000,201020000)==-ERANGE);assert(!s.valid&&!s.source_until);
 struct gm7_requester failure={0};assert(!gm7_requester_init(&failure,&g,81,97,0));struct gm7_packet out={0};assert(gm7_request_begin(&failure,&out,17,33,1,random_bytes,(void *)1));assert(failure.failed);
 assert(!gm7_requester_init(&failure,&g,81,97,100));req=request(&failure,17,33,101);gm7_requester_expire(&failure,99);assert(failure.failed&&!failure.pending[0].used);
 puts("PASS actualRF-only nonce admission, immutable250ms RTT, duplicate/consumed nonce, repeated RNG counter separation, source-vs-control expiry, stale diagnostic scopes, renewal replay watermarks, old-scope WITHDRAW refusal, overlapping renewal and same-boot step fence, entropy/timebase failure");
}
static struct gm7_source local_source(uint64_t now){return (struct gm7_source){.utc_us=UINT64_C(1792800000000000)+now,.sampled_us=now,.until_us=now+1500000,.pps_at=now-now%1000000,.source_gen=3,.capture_gen=4,.association_gen=1,.quality={128,2,255,128,65535},.gnss_healthy=true,.pps_healthy=true,.utc_available=true,.physical_ready=true};}
static void update(struct gm7_owner *o,uint64_t now,const struct gm7_preparation *prep){struct gm7_source source=local_source(now);struct gm7_preparation empty={0};gm7_owner_update(o,&source,prep?prep:&empty,now);}
static void dwell(struct gm7_owner *o){for(uint64_t now=0;now<=60000000;now+=1000000)update(o,now,NULL);assert(o->eligible&&!o->serving);}
static int receive(struct gm7_owner *o,struct gm7_packet p,uint64_t rx){struct r2_group_config g=group();uint8_t ad[GM7_MAX_AD_BYTES];assert(!gm7_ad_encode(ad,sizeof ad,&p,&g));return gm7_owner_receive(o,ad,gm7_wire_size(p.kind)+4,NULL,rx,rx);}
static void causal(struct gm7_owner *server,struct gm7_owner *client,uint64_t now){
 struct gm7_packet locator;assert(!gm7_owner_control(server,&locator,now));assert(receive(client,locator,now+10)==-EALREADY||client->peer[0].used);
 struct gm7_packet req;assert(!gm7_owner_next_request(client,&req,now+20,random_bytes,NULL));req=authenticated(req);assert(!gm7_request_sent(&client->requester,&req.body.request,now+30,now+30));
 struct gm7_packet reply;assert(!gm7_owner_anchor(server,&reply,&req,now+40,now+50));assert(!receive(client,reply,now+60));
 struct gm7_packet current;assert(!gm7_owner_control(server,&current,now+80));assert(!receive(client,current,now+90));
}
static void owner(void){
 struct r2_group_config g=group();struct gm7_owner a,b;assert(!gm7_owner_init(&a,&g,17,33,true,0));assert(!gm7_owner_init(&b,&g,49,65,true,0));dwell(&a);dwell(&b);
 for(uint64_t now=61000000;now<=66000000;now+=1000000){update(&a,now,NULL);update(&b,now,NULL);}assert(a.initial_due&&b.initial_due);
 assert(!gm7_owner_activate(&a,66000000,66004000,66000000));assert(a.scope.birth==UINT64_C(1792800066000000)&&a.scope.timing_epoch==1&&a.scope.birth!=a.scope.timing_epoch);
 causal(&a,&b,66100000);update(&b,66100200,NULL);assert(b.scope.active==17&&!b.armed&&!b.serving);
 struct gm7_preparation prep={.scope=b.scope,.shadow_gen=9,.correction_age_us=20000,.phase_enclosure_us=500,.retained_enclosure_us=500,.map_valid=true,.fit_valid=true,.locked=true,.phase_available=true,.retained_prediction=true};
 /* An ANCHOR never sets any of these physical inputs or arms HOT. */
 for(uint64_t now=66200000;now<=72400000;now+=200000){update(&a,now,NULL);struct gm7_packet c;assert(!gm7_owner_control(&a,&c,now));assert(!receive(&b,c,now+10));update(&b,now+20,&prep);}
 assert(b.prepared&&!b.armed);causal(&b,&a,72500000);
 update(&a,72600000,NULL);assert(a.nominee==49&&a.nominee_boot==65);struct gm7_packet named;assert(!gm7_owner_control(&a,&named,72600000));assert(!receive(&b,named,72600010));update(&b,72600020,&prep);assert(b.armed&&b.role==GM7_ARMED);
 /* Production sender -> exact codec -> causal cache -> scoped consumer. */
 {struct gm7_owner active=a,hot=b;struct gm7_packet status,role;
  assert(!gm7_owner_control(&hot,&status,72600100));assert(!receive(&active,status,72600110));
  assert(!gm7_role_snapshot(&hot,&role,72600120));assert(!receive(&active,role,72600130));
  struct gm7_reported_role reported;bool current=false;assert(gm7_peer_role(&active,0,active.now,&reported,&current)&&current&&reported.role==GM7_W_HOT&&reported.hot_ms==0);
  uint64_t expiry=reported.until;active.nominee=0;assert(gm7_peer_role(&active,0,active.now,&reported,&current)&&!current);active.nominee=49;
  assert(!gm7_role_snapshot(&hot,&role,72600140));hot.armed=false;
  assert(!gm7_owner_control(&hot,&status,72600150));assert(!receive(&active,status,72600160));assert(!receive(&active,role,72600170));
  assert(gm7_peer_role(&active,0,active.now,&reported,&current)&&!current&&reported.until>=expiry);
  uint64_t unchanged=reported.until;assert(receive(&active,role,72600180));assert(gm7_peer_role(&active,0,active.now,&reported,&current)&&!current&&reported.until==unchanged);
  active.peer[0].session.control_until=active.now;assert(gm7_peer_role(&active,0,active.now,&reported,&current)&&!current);
  active.peer[0].used=false;assert(!gm7_peer_role(&active,0,active.now,&reported,&current));
 }
 {struct gm7_owner yielding=a,successor=b;struct gm7_packet event,copy;
  yielding.yield_ready=true;assert(!gm7_owner_control(&yielding,&event,72601000));assert(event.flags&GM7_YIELD);uint64_t deadline=yielding.yield_deadline;assert(!receive(&successor,event,72601010));
  assert(!gm7_owner_control(&yielding,&copy,72851000));assert(!memcmp(&event,&copy,sizeof event));assert(receive(&successor,copy,72851010)==-EALREADY);
  assert(!gm7_owner_control(&yielding,&copy,73101000));assert(!memcmp(&event,&copy,sizeof event)&&yielding.yield_deadline==deadline);
  struct gm7_preparation mapped=prep;mapped.yield_phase_valid=true;mapped.yield_sequence=event.body.control.sequence;mapped.yield_before_us=deadline-500;mapped.yield_after_us=deadline+500;
  update(&successor,deadline-1,&mapped);assert(successor.armed&&!successor.takeover_due);
  update(&yielding,deadline,NULL);assert(!yielding.serving&&!gm7_owner_output(&yielding,deadline));
  update(&successor,deadline+500,&mapped);assert(successor.armed&&successor.takeover_due);assert(!gm7_owner_activate(&successor,deadline+500,deadline+4500,deadline+500));assert(successor.scope.birth==a.scope.birth&&successor.scope.term==2);
 }
 uint64_t armed=b.armed_since,original=b.selected_original,birth=b.scope.birth;prep.map_valid=prep.locked=false;
 for(uint64_t now=b.now+200000;now<original+1600000;now+=200000)update(&b,now,&prep);
 update(&b,original+1600000,&prep);assert(b.armed&&b.armed_since==armed&&!b.prepared&&b.selected_source_until<original+1600000);
 update(&b,original+2750000,&prep);assert(b.takeover_due&&b.armed&&b.armed_since==armed);
 /* Receive/activation races execute production entry points with no update
  * between the fresh contact and the reserved action. */
 struct gm7_owner resumed=a,race=b;
 for(uint64_t t=a.now+200000;t<=b.now+200000;t+=200000)update(&resumed,t,NULL);
 uint64_t fresh_at=resumed.now+10;struct gm7_packet fresh;assert(!gm7_owner_control(&resumed,&fresh,fresh_at));
 assert(!receive(&race,fresh,fresh_at+10));assert(!race.takeover_due&&race.armed&&race.armed_since==armed);
 assert(gm7_owner_activate(&race,fresh_at+20,fresh_at+4020,fresh_at+20)==-EACCES);
 race=b;resumed.nominee=resumed.nominee_boot=0;assert(!gm7_owner_control(&resumed,&fresh,fresh_at+30));
 assert(!receive(&race,fresh,fresh_at+40));assert(!race.armed&&!race.takeover_due);
 assert(gm7_owner_activate(&race,fresh_at+50,fresh_at+4050,fresh_at+50)==-EACCES);

 assert(!gm7_owner_activate(&b,original+2750000,original+2754000,original+2750000));assert(b.serving&&b.scope.birth==birth&&b.scope.term==2&&b.scope.active==49&&!b.armed);
 struct gm7_scope before=b.scope;struct gm7_source fault=local_source(b.now+10);fault.pps_healthy=false;struct gm7_preparation empty={0};gm7_owner_update(&b,&fault,&empty,b.now+10);assert(!b.serving&&b.event.valid&&b.event.negative&&b.event.packet.kind==GM7_WITHDRAW&&b.scope.term==before.term);
 struct gm7_packet negative=b.event.packet;gm7_owner_update(&b,&fault,&empty,b.now);assert(b.event.packet.body.control.sequence==negative.body.control.sequence&&b.event.deadline==negative.body.control.sent_mono_us+1000000);
 /* Any entry-point regression retires ALL sessions, nonces and positive work. */
 uint64_t earlier=a.now-1;update(&a,earlier,NULL);assert(a.failed&&!a.serving&&!a.armed&&!a.event.valid&&a.requester.failed);for(unsigned i=0;i<GM7_PEERS;i++)assert(!a.peer[i].session.valid);
 assert(!gm7_calendar_clear(UINT64_C(1790812800000000))); /*2026-10-01T00:00*/
 assert(gm7_calendar_clear(UINT64_C(1790812860000000)));
 assert(!gm7_calendar_clear(UINT64_C(1790812799000000))); /*September final minute*/
 puts("PASS production owner independent60s dwell, projected birth/separate token, causal follow, physical5s preparation, local HOT arm and blackout timer continuity, term-preserving takeover, immediate immutable WITHDRAW, global timebase fence and month-edge calendar");
}
static void host_endpoints(void){
 struct r2_group_config g=group();struct gm7_requester r={0};struct gm7_session s={0};assert(!gm7_requester_init(&r,&g,81,97,1000000));
 struct gm7_packet req=request(&r,17,33,1000000);assert(!gm7_request_host_bound(&r,&req.body.request,1,1000100));assert(r.pending[0].tx==999999&&r.pending[0].deadline==1249999);
 assert(gm7_request_sent(&r,&req.body.request,1100000,1100000)==-EALREADY);assert(r.pending[0].deadline==1249999);
 struct gm7_packet a=anchor(req,false,10000000);assert(!gm7_anchor_accept(&r,&s,&a,1100000,1150000));assert(s.endpoint==GM7_HOST_CAUSAL_BOUND_NOT_RF&&s.endpoint_lower==999999&&s.endpoint_upper==1150001&&s.conservative_elapsed_us==150002&&s.source_until==2199999);
 assert(s.offset_lo<=INT64_C(1000000)-INT64_C(10000000)&&s.offset_hi>=INT64_C(1150000)-INT64_C(10000000));
 uint64_t source_until=s.source_until;assert(gm7_anchor_accept(&r,&s,&a,1160000,1160000)&&s.source_until==source_until);
 req=request(&r,17,33,1200000);assert(!gm7_request_host_bound(&r,&req.body.request,1,1200100));a=anchor(req,false,10200000);assert(gm7_anchor_accept(&r,&s,&a,1210000,1450000));assert(!r.pending[0].used); /* post-auth U, not early queued callback */
 puts("PASS B1 outward host causal endpoints, immutable prequeue250ms deadline, full enqueue/auth delay subtraction, repeated TX nonrenewal, complete-response upper and consume-once");
}
static void schedule(void){
 for(uint64_t cycle=0;cycle<32;cycle++){uint32_t lo,hi;assert(!gm7_status_offset(&lo,17,33,cycle,-250));assert(!gm7_status_offset(&hi,17,33,cycle,250));assert(hi-lo==500&&lo>=89750&&hi<=107750);}
 for(unsigned bit=0;bit<64;bit++){uint64_t a=1,b=UINT64_C(1)<<bit;if(a==b)b|=2;bool differs=false;for(uint64_t n=4;n<=256;n+=4){enum gm7_discovery x=gm7_discovery_policy(a,n,true,true,false,false),y=gm7_discovery_policy(b,n,true,true,false,false);if(x!=y)differs=true;assert(gm7_discovery_policy(a,n,true,true,true,false)==GM7_DISCOVERY_LISTEN);}assert(differs);}
 for(uint64_t attempt=0;attempt<32;attempt++){uint64_t next;assert(!gm7_retry_after(&next,10090000,attempt,90000));assert(next>=12090000&&next<13090000&&(next/250000)%4==attempt%4);}
 /* Production A1 slot selection precedes residue calculation. Same low ID
  * bits do not force collision; frozen offsets survive the target cycle. */
 for(unsigned n=0;n<64;n++)for(unsigned j=0;j<3;j++){
  uint64_t id=17+(UINT64_C(1)<<(16+j*16));uint64_t tx=UINT64_C(1792800000000000)+n*250000+90000;uint32_t offset;int jitter=j==0?-250:j==1?0:250;assert(!gm7_status_offset(&offset,id,33,(tx+2000000)/2000000,jitter));uint64_t next;assert(!gm7_retry_after(&next,tx,n,offset));assert(next>=tx+2000000&&next<tx+3000000&&next%250000==offset&&(next/250000)%4==n%4);
 }
 struct gm7_window w;for(uint64_t frame=0;frame<8;frame++){int rc=gm7_response_window(&w,frame*250000+90000,frame*250000,frame,2000);assert(!!rc==gm7_secondary_reserved(frame+1));}
 uint32_t airtime;assert(!gm7_complete_airtime(&airtime,190,13,10,1,2)&&airtime==1328);assert(gm7_complete_airtime(&airtime,190,13,10,1,1));assert(gm7_complete_airtime(&airtime,190,13,10,3,2));
 struct gm7_scheduler s;gm7_scheduler_init(&s,9000000);struct gm7_packet p=fixture("announce");struct r2_group_config g=group();uint64_t ticket;
 w=(struct gm7_window){.nominal=10038000,.latest_start=10040000,.latest_end=10042000,.enclosure_us=500,.channel=37,.physical=true};
 assert(!gm7_scheduler_submit(&s,GM7_PRIMARY,&p,&g,w,11200000,0,1,9000000,&ticket));uint32_t gen=s.job[0].generation,version=s.job[0].buffer_version;
 assert(gm7_scheduler_dispatch(&s,GM7_PRIMARY,true,10037999)==-EAGAIN);assert(!gm7_scheduler_dispatch(&s,GM7_PRIMARY,true,10038000));assert(!s.completed);
 assert(!gm7_scheduler_started(&s,GM7_PRIMARY,ticket,gen,version,10038500));assert(gm7_scheduler_aux_allowed(&s,GM7_PRIMARY,ticket,gen,version,10039000,1000,true));assert(!gm7_scheduler_complete(&s,GM7_PRIMARY,ticket,gen,version,10040000,true));
 assert(gm7_scheduler_release(&s,GM7_PRIMARY,ticket,false,10040001)==-EBUSY);assert(!gm7_scheduler_release(&s,GM7_PRIMARY,ticket,true,10040002));
 w.nominal+=250000;w.latest_start+=250000;w.latest_end+=250000;assert(!gm7_scheduler_submit(&s,GM7_PRIMARY,&p,&g,w,11200000,0,1,10050000,&ticket));gen=s.job[0].generation;version=s.job[0].buffer_version;assert(!gm7_scheduler_dispatch(&s,0,true,w.nominal));assert(!gm7_scheduler_started(&s,0,ticket,gen,version,w.nominal));gm7_scheduler_cancel_positive(&s,w.nominal+100);assert(!gm7_scheduler_aux_allowed(&s,0,ticket,gen,version,w.nominal+200,1000,true));assert(s.job[0].state==GM7_JOB_ABORTED);assert(gm7_scheduler_complete(&s,0,ticket,gen,version,w.nominal+1500,true)==-ESTALE);
 assert(!gm7_scheduler_release(&s,0,ticket,true,w.nominal+1600));
 p=fixture("withdraw");w.nominal+=250000;w.latest_start+=250000;w.latest_end+=250000;
 assert(!gm7_scheduler_submit(&s,0,&p,&g,w,0,0,1,w.nominal-10000,&ticket));gen=s.job[0].generation;version=s.job[0].buffer_version;
 gm7_scheduler_cancel_positive(&s,w.nominal-5000);assert(!gm7_scheduler_dispatch(&s,0,false,w.nominal));
 assert(!gm7_scheduler_started(&s,0,ticket,gen,version,w.nominal));assert(gm7_scheduler_aux_allowed(&s,0,ticket,gen,version,w.nominal+500,1000,false));assert(!gm7_scheduler_complete(&s,0,ticket,gen,version,w.nominal+1500,true));
 assert(!gm7_scheduler_release(&s,0,ticket,true,w.nominal+1501));
 p=fixture("eligible-candidate-status");assert(gm7_scheduler_submit(&s,0,&p,&g,w,11200000,0,1,w.nominal+1501,&ticket)==-EPROTO);
 puts("PASS production scheduler hashed STATUS slots, A1 full64ID diversity/reserved response/retry residues, completeLE1M+LE2M event airtime, actualSTART/AUXEND bounds, no HCI completion fiction, drain ownership and generation-fenced late AUX");
}
static void air(void){
 struct gm7_scheduler s;gm7_scheduler_init(&s,0);struct gm7_packet p=fixture("announce");struct r2_group_config g=group();
 struct gm7_window w={.nominal=100000,.latest_start=102000,.latest_end=104000,.enclosure_us=500,.channel=37,.physical=true};uint64_t ticket;
 assert(!gm7_scheduler_submit(&s,0,&p,&g,w,1200000,0,0,1,&ticket));uint8_t primary[15],address[6]={1,2,3,4,5,0xc6};
 assert(!gm7_air_build(primary,&s.job[0],address,18));
 const uint8_t expected[15]={0x47,13,12,0x19,1,2,3,4,5,0xc6,1,0xe0,18,17,0x20};assert(!memcmp(primary,expected,15));
 assert(s.job[0].pdu_prefix[1]==188&&s.job[0].pdu_prefix[2]==9&&s.job[0].pdu_prefix[3]==9);
 const uint8_t *ad;size_t length;uint8_t remote[6];assert(!gm7_air_received(s.job[0].pdu_prefix,12+s.job[0].length,&ad,&length,remote));
 assert(length==178&&!memcmp(ad,s.job[0].ad,178)&&!memcmp(remote,address,6));struct gm7_packet decoded;assert(!gm7_ad_decode(&decoded,ad,length,&g));
 for(unsigned bit=0;bit<8;bit++){s.job[0].pdu_prefix[3]^=(uint8_t)(1u<<bit);assert(gm7_air_received(s.job[0].pdu_prefix,12+s.job[0].length,&ad,&length,remote));s.job[0].pdu_prefix[3]^=(uint8_t)(1u<<bit);}
 assert(gm7_air_build(primary,&s.job[0],address,37));
 puts("PASS exact primary37/AuxPtr LE2M offset/address/ADI packet fixture, frozen AUX AD and strict unchained complete receive shape");
}
static void diagnostics(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,17,33,true,0));dwell(&o);for(uint64_t now=61000000;now<=66000000;now+=1000000)update(&o,now,NULL);assert(o.initial_due);assert(!gm7_owner_activate(&o,66000000,66004000,66000000));struct gm7_packet p;
 assert(!gm7_role_snapshot(&o,&p,o.now));p=authenticated(p);assert(p.body.role.role==GM7_W_ACTIVE&&(p.body.role.known&2)&&p.body.role.active_duration_ms==0&&p.body.role.timing_epoch==1);
 o.serving=false;o.armed=true;o.armed_since=o.now-2000000;o.role=GM7_ARMED;assert(!gm7_role_snapshot(&o,&p,o.now));p=authenticated(p);assert(p.body.role.role==GM7_W_HOT&&p.body.role.hot_duration_ms==2000&&p.body.role.timing_epoch==0);
 o.armed=false;o.selected.hot=17;o.selected.hot_boot=33;assert(!gm7_role_snapshot(&o,&p,o.now));assert(p.body.role.role==GM7_W_PREPARING);
 o.source.gnss_healthy=o.source.pps_healthy=false;o.blockers=GM7_B_SOURCE|GM7_B_DWELL|GM7_B_MAP|GM7_B_NOMINATION|GM7_B_SCOPE|GM7_B_PHASE|GM7_B_RESOURCE;assert(!gm7_role_snapshot(&o,&p,o.now));assert(p.body.role.blockers==(GM7_W_NO_GNSS|GM7_W_NO_PPS|GM7_W_DWELL|GM7_W_SHADOW_UNREADY|GM7_W_WRONG_NOMINEE|GM7_W_WRONG_BOOT_TERM|GM7_W_ALIGNMENT_UNTRUSTED|GM7_W_RESOURCE_LIMIT));
 o.source.pps_healthy=true;o.source.gnss_healthy=false;o.source.until_us=0;o.blockers=GM7_B_SOURCE;assert(!gm7_role_snapshot(&o,&p,o.now));assert((p.body.role.known&4)&&p.body.role.pps_duration_ms==66000);
 o.source.gnss_healthy=o.source.pps_healthy=true;o.blockers=GM7_B_CALENDAR;assert(!gm7_role_snapshot(&o,&p,o.now));assert(!p.body.role.blockers); /* calendar has no invented wire bit */
 struct r2_clock_view v={.gps_age_us=100000,.pps_age_us=20000,.gga_age_us=300000,.fix_active=true,.pps_present=true,.sats_used=12,.gga_hdop_centi=87};assert(!gm7_gnss_snapshot(&o,&p,&v,o.now));p=authenticated(p);assert(p.body.gnss.clock_class==2&&p.body.gnss.accuracy==255&&p.body.gnss.variance==65535&&p.body.gnss.hdop_centi==87&&p.body.gnss.utc_error_us==UINT32_MAX&&p.body.gnss.values==7);
 puts("PASS typed ROLE enum/blocker translation, owner HOT/PPS/ACTIVE known-duration semantics, calendar separation, and GNSS class2/unknown independent bounds through exact codec");
}
static void promote(void){
 const int32_t rates[]={-50000,-1,0,1,50000};const int64_t phases[]={-8000000,-1000,-1,0,1,1000,8000000};
 for(unsigned r=0;r<5;r++)for(unsigned p=0;p<7;p++){
  r2_servo_map_t m={.delivery=100000000,.anchor=100000000000,.phase=phases[p],.rate=rates[r],.valid=true};
  struct r2_capture at={m.delivery+12345,4,R2_CAPTURE_NOW};struct r2_source_clock c;
  assert(!gm7_promote_clock(&c,&m,INT64_C(1700000000000000000),at,3,0));
  for(uint64_t dt=0;dt<24000000;dt+=7919){struct r2_capture sample=at;sample.tick+=dt;int64_t expected;uint64_t actual,generation;
   assert(r2_servo_mapped(&m,sample.tick,&expected)&&r2_source_clock_read(&c,sample,&actual,&generation));
   uint64_t error=actual>(uint64_t)expected?actual-(uint64_t)expected:(uint64_t)expected-actual;assert(error<=4);
  }
 }
 uint32_t enclosure;assert(gm7_phase_grow(7999,100000000,100000001,&enclosure)&&enclosure==8000);
 assert(gm7_phase_grow(7999,100000000,100080001,&enclosure)&&enclosure>8000);
 puts("PASS retained affine promotion anchor/trajectory through signed1ppm slew completion, exact reciprocal rate extremes, <=4ns numerical enclosure and post-snapshot drain-age growth");
}
static void capacity_and_merge(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,17,33,true,0));
 struct gm7_packet p=fixture("eligible-candidate-status");for(unsigned i=0;i<GM7_PEERS;i++){p.body.control.sender=100+i;p.body.control.boot=1000+i;p.body.control.sequence=1;assert(receive(&o,p,i+1)!=0);assert(o.peer[i].used);}p.body.control.sender=999;p.body.control.boot=9999;assert(receive(&o,p,10)==-ENOSPC);update(&o,6000000,NULL);assert(receive(&o,p,6000010)!=0);assert(o.peer[0].used&&o.peer[0].identity==999);assert(!o.initial_due&&!o.serving);
 struct gm7_owner leader[3],rc;for(unsigned i=0;i<3;i++){assert(!gm7_owner_init(&leader[i],&g,17+i*32,33+i*32,true,0));dwell(&leader[i]);for(uint64_t t=61000000;t<=66000000;t+=1000000)update(&leader[i],t,NULL);}assert(!gm7_owner_init(&rc,&g,113,129,false,0));update(&rc,66000000,NULL);assert(!rc.eligible&&!rc.initial_due);
 assert(!gm7_owner_activate(&leader[0],66000000,66004000,66000000));for(unsigned i=1;i<3;i++){causal(&leader[0],&leader[i],66100000+i*1000);update(&leader[i],66103000+i*1000,NULL);assert(!leader[i].serving&&leader[i].scope.birth==leader[0].scope.birth);}causal(&leader[0],&rc,66110000);update(&rc,66110200,NULL);assert(!rc.serving&&!rc.armed&&rc.scope.active==17);
 struct gm7_owner older=leader[0];older.requester.identity=13;older.requester.boot=29;older.scope.active=13;older.scope.boot=29;older.scope.founder=13;older.scope.birth-=1000000;older.source.sampled_us=older.now;older.source.until_us=older.now+1500000;causal(&older,&leader[0],66200000);assert(!leader[0].serving&&leader[0].reason==GM7_R_MERGE);update(&leader[0],66200200,NULL);assert(leader[0].scope.birth==older.scope.birth&&leader[0].scope.active==13);
 struct gm7_packet out;older.sequence=UINT64_MAX;assert(gm7_owner_control(&older,&out,older.now+10)==-EOVERFLOW&&!older.serving); /* no wrapping authority */
 puts("PASS eight-current-peer stale locator saturation recovery, actual3GM+1RC causal cohort, unqualified RC refusal, immediate older-lineage merge fence and sequence exhaustion");
}
static void phase_policy(void){
 uint64_t start;
 for(uint64_t phase=0;phase<250000;phase++)assert(gm7_phase_plan(1000000+phase,1000000+phase,62499,2500,3000,2000000,&start)&&start>=1000000+phase+3000&&start%62500==62499);
 assert(!gm7_phase_plan(100,UINT64_MAX,62499,2500,3000,UINT64_MAX,&start));assert(!gm7_phase_plan(100,UINT64_MAX-1,62499,2500,3000,UINT64_MAX,&start));
 uint64_t sample=16000000,last=0;unsigned successes=0;
 for(uint64_t t=sample;t<=sample+4000000;t+=128000){int step=gm7_phase_nudge(1000,sample,t,last);if(step){last=sample;++successes;}}
 assert(successes==1&&!gm7_phase_nudge(1000,sample+999359,sample+999359,last));
 int64_t shift=0;for(unsigned n=1;n<=1000;n++){sample=16000000+(uint64_t)n*1000000;int step=gm7_phase_nudge(1000,sample,sample,last);assert(step==-1);last=sample;shift+=step*8;}
 assert(shift==-8000); /*128ppm max deliberate displacement, not1024ppm stale repeats*/
 puts("PASS all250000 prospective phases and overflow boundaries, one nudge per distinct captured G1,250ms stale-sample nonrepeat and bounded128ppm interval displacement");
}
static void reservation(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,17,33,true,0));dwell(&o);
 for(uint64_t n=61000000;n<=66000000;n+=1000000)update(&o,n,NULL);
 uint64_t at=o.now+10000;assert(!gm7_owner_reserve(&o,at,at+4000,o.now));
 assert(o.reserved&&!o.serving&&!o.scope.birth&&!gm7_owner_output(&o,at));
 assert(o.reserved_scope.birth==UINT64_C(1792800000000000)+o.now);
 struct gm7_packet p;assert(!gm7_owner_planned_control(&o,&p,o.now));uint64_t sequence=p.body.control.sequence;
 assert(p.kind==GM7_ANNOUNCE&&p.body.control.sent_mono_us==o.now&&p.body.control.birth_us==o.reserved_scope.birth);
 assert(gm7_owner_reservation_commit(&o,at,at+1500,at-1));
 gm7_owner_reservation_cancel(&o,o.now);assert(!o.reserved&&!o.serving&&!o.scope.birth);
 assert(!gm7_owner_reserve(&o,at+250000,at+254000,at));
 assert(!gm7_owner_planned_control(&o,&p,at));assert(p.body.control.sequence>sequence);
 assert(!gm7_owner_reservation_commit(&o,at+250500,at+251800,at+251900));
 assert(o.serving&&!o.reserved&&o.active_since==at+251900&&o.scope.term==1);
 assert(gm7_owner_output(&o,o.now));
 struct gm7_bridge b={.kernel_lo=1000000,.kernel_hi=1000030,.hf=99000000,.epoch=4};uint64_t end;
 assert(gm7_bridge_to_hf(b,2000000,&end)&&end<100000000&&end>99999000);
 assert(gm7_bridge_to_kernel(b,100000000,&end)&&end<2000000&&end>1999000);
 assert(!gm7_bridge_to_hf(b,1000031,&end));b.kernel_hi+=101;assert(!gm7_bridge_to_hf(b,2000000,&end));
 puts("PASS provisional future reservation, projected birth, no early serving/output, abort/retry sequence consumption, measured first complete ANN commit and conservative mixed-domain expiry");
}
int main(void){printf("sizeof owner=%zu peer=%zu session=%zu scheduler=%zu\n",sizeof(struct gm7_owner),sizeof(struct gm7_peer),sizeof(struct gm7_session),sizeof(struct gm7_scheduler));codec();sessions();owner();capacity_and_merge();phase_policy();reservation();promote();host_endpoints();schedule();air();diagnostics();return 0;}
