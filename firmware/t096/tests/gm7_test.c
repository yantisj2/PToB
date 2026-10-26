#include "gm7_phase.h"
#include "gm7_promote.h"
#include "gm7_bridge.h"
/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_codec.h"
#include "gm7_control.h"
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
 printf("PASS GM7 ordinary-only independent vectors, %u bit corruptions, exact lengths, AD wrappers, foreign group/key, decode nonmutation\n",mutated);
}
static void phase_policy(void){
 uint64_t start;
 for(uint64_t phase=0;phase<250000;phase++)assert(gm7_phase_plan(1000000+phase,1000000+phase,62499,2500,3000,2000000,&start)&&start>=1000000+phase+3000&&start%62500==62499);
 assert(!gm7_phase_plan(100,UINT64_MAX,62499,2500,3000,UINT64_MAX,&start));assert(!gm7_phase_plan(100,UINT64_MAX-1,62499,2500,3000,UINT64_MAX,&start));
 for(uint64_t receipt_phase=0;receipt_phase<GM7_FRAME_US;receipt_phase++){
  uint64_t receipt=1000000+receipt_phase,cause=receipt+GM7_BLACKOUT_US;
  uint64_t nominal=(cause/GM7_FRAME_US)*GM7_FRAME_US+GM7_PRIMARY_US;
  if(nominal<cause)nominal+=GM7_FRAME_US;
  assert(nominal>=cause&&nominal+104000<=receipt+GM7_BLACKOUT_BOUND_US);
 }
 uint64_t sample=16000000,last=0;unsigned successes=0;
 for(uint64_t t=sample;t<=sample+4000000;t+=128000){int step=gm7_phase_nudge(1000,sample,t,last);if(step){last=sample;++successes;}}
 assert(successes==1&&!gm7_phase_nudge(1000,sample+999359,sample+999359,last));
 int64_t shift=0;for(unsigned n=1;n<=1000;n++){sample=16000000+(uint64_t)n*1000000;int step=gm7_phase_nudge(1000,sample,sample,last);assert(step==-1);last=sample;shift+=step*8;}
 assert(shift==-8000); /*128ppm max deliberate displacement, not1024ppm stale repeats*/
 puts("PASS all250000 prospective phases and overflow boundaries, one nudge per distinct captured G1,250ms stale-sample nonrepeat and bounded128ppm interval displacement");
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

static int admit(struct gm7_peer_control *s,struct gm7_packet p,uint64_t rx,uint64_t now){
 uint64_t original,until;int rc=gm7_control_check(s,&p,rx,now,&original,&until);
 if(!rc){s->control=p.body.control;s->last_sequence=p.body.control.sequence;s->last_sent=p.body.control.sent_mono_us;s->control_until=until;}return rc;
}
static void receipt_horizons(void){
 struct gm7_peer_control s={0};struct gm7_packet p=fixture("announce");
 p.body.control.sequence=1;p.body.control.sent_mono_us=100000000;
 p.body.control.max_age_us=750000;p.body.control.source_valid_us=1000000;
 assert(!admit(&s,p,10000000,10200000)&&s.control_until==10750000);
 struct gm7_peer_control saved=s;
 assert(admit(&s,p,10500000,10500000)==-EALREADY&&!memcmp(&s,&saved,sizeof s));
 assert(admit(&s,p,12000000,12000000)==-EALREADY&&!memcmp(&s,&saved,sizeof s));
 p.body.control.sequence++;p.body.control.source_gen++;
 /* A control observation is not a short source permission session. */
 assert(!admit(&s,p,12000000,12000100));
 saved=s;p.body.control.sequence++;p.body.control.sent_mono_us--;
 assert(admit(&s,p,12500000,12500000)==-ESTALE&&!memcmp(&s,&saved,sizeof s));
 p.body.control.sent_mono_us++;
 assert(admit(&s,p,12500000,13250000)==-ESTALE&&!memcmp(&s,&saved,sizeof s));
 assert(admit(&s,p,UINT64_MAX-1,UINT64_MAX-1)==-ESTALE);
 puts("PASS receipt-bounded ordinary observations, immutable duplicate/reordered floors, processing expiry/overflow; no ANN source lease");
}
static int receive(struct gm7_owner *o,struct gm7_packet p,uint64_t now){
 uint8_t ad[GM7_MAX_AD_BYTES];assert(!gm7_ad_encode(ad,sizeof ad,&p,&o->group));return gm7_owner_receive(o,ad,gm7_wire_size(p.kind)+4,NULL,now,now);
}
static void ordinary_inventory(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,81,97,true,1000000));
 struct gm7_packet p=fixture("announce");p.body.control.sequence=10;p.body.control.term=2;p.body.control.sent_mono_us=10000000;
 o.serving=true;o.scope=(struct gm7_scope){.birth=p.body.control.birth_us+1000000,.founder=81,.term=5,.active=81,.boot=97};
 assert(!receive(&o,p,1000000)&&!o.serving&&o.reason==GM7_R_MERGE);
 struct gm7_preparation prep={0};struct gm7_source source={0};gm7_owner_update(&o,&source,&prep,1000001);
 assert(o.scope.active==p.body.control.sender&&o.scope.timing_epoch==22);
 struct gm7_peer *peer=NULL;for(unsigned i=0;i<GM7_PEERS;i++)if(o.peer[i].used)peer=&o.peer[i];assert(peer);
 struct gm7_peer_control saved=peer->received;
 o.reserved=o.armed=true;o.reserved_scope=o.scope;
 struct gm7_packet wrong=p;wrong.kind=GM7_WITHDRAW;wrong.flags=0;wrong.body.control.sequence++;wrong.body.control.source_valid_us=0;wrong.body.control.max_age_us=1000000;wrong.body.control.term++;
 assert(receive(&o,wrong,1000100)==-EXDEV&&o.reserved&&!memcmp(&saved,&peer->received,sizeof saved));
 wrong=p;wrong.body.control.sequence++;wrong.body.control.term--;
 assert(receive(&o,wrong,1000200)==-ESTALE&&o.reserved&&!memcmp(&saved,&peer->received,sizeof saved));
 struct gm7_packet status=fixture("eligible-candidate-status");status.body.control.sender=900;status.body.control.boot=901;
 assert(!receive(&o,status,1000300)&&o.reserved); /* Routine unrelated STATUS cannot cancel promotion. */
 struct gm7_packet inferior=p;inferior.body.control.sender=inferior.body.control.active=902;inferior.body.control.boot=inferior.body.control.active_boot=903;inferior.body.control.founder=902;inferior.body.control.birth_us++;
 assert(!receive(&o,inferior,1000350)&&o.reserved);
 assert(receive(&o,p,1000400)==-EALREADY&&o.reserved);
 p.body.control.sequence++;p.body.control.sent_mono_us+=1000;
 assert(!receive(&o,p,1000500)&&!o.reserved); /* Current positive incumbent cancels. */
 puts("PASS first ordinary ANN immediate older-mesh stop, timing identity present, transactional wrong-scope negative/retired term, unrelated STATUS preserves reservation, current ANN cancels");
}
static void ordinary_schedule(void){
 struct r2_group_config g=group();struct gm7_packet p=fixture("announce");uint32_t air;
 assert(GM7_CONTROL_BYTES==178&&GM7_MAX_AD_BYTES==182);
 assert(!gm7_complete_airtime(&air,182,37,10,1,2)&&air<=GM7_JOB_AIRTIME_US);
 struct gm7_scheduler s;gm7_scheduler_init(&s,0);uint64_t ticket;
 struct gm7_window w={.nominal=38000,.latest_start=40000,.latest_end=42000,.channel=37,.physical=true};
 assert(!gm7_scheduler_submit(&s,GM7_PRIMARY,&p,&g,w,1500000,0,0,0,&ticket));
 assert(!gm7_scheduler_dispatch(&s,GM7_PRIMARY,true,38000));
 assert(!gm7_scheduler_started(&s,GM7_PRIMARY,ticket,s.job[0].generation,s.job[0].buffer_version,38000));
 assert(gm7_scheduler_aux_allowed(&s,GM7_PRIMARY,ticket,s.job[0].generation,s.job[0].buffer_version,38500,1000,true));
 assert(!gm7_scheduler_complete(&s,GM7_PRIMARY,ticket,s.job[0].generation,s.job[0].buffer_version,39400,true));
 w.nominal=45000;w.latest_start=47000;w.latest_end=49000;
 assert(!gm7_scheduler_submit(&s,GM7_SECONDARY,&p,&g,w,1500000,0,0,42000,&ticket));
 gm7_scheduler_cancel_positive(&s,43000);assert(gm7_scheduler_dispatch(&s,GM7_SECONDARY,true,45000));
 p.kind=4;assert(gm7_validate(&p)&&!gm7_wire_size(4));p.kind=5;assert(gm7_validate(&p)&&!gm7_wire_size(5));
 puts("PASS178B control/182B AD complete-airtime and immutable copy cancellation; dedicated exchange kinds absent");
}

static void capacity_pressure(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,1,2,true,1000000));
 struct gm7_packet status=fixture("eligible-candidate-status");
 for(unsigned i=0;i<GM7_PEERS;i++){status.body.control.sender=100+i;status.body.control.boot=200+i;assert(!receive(&o,status,1000000+i));}
 struct gm7_owner live=o;status.body.control.sender=999;status.body.control.boot=888;
 assert(receive(&o,status,1000100)==-ENOSPC&&!o.peer_evictions);
 o.scope.active=100;o.scope.boot=200;o.nominee=101;o.nominee_boot=201;o.selected.hot=102;o.selected.hot_boot=202;
 assert(!receive(&o,status,7000000)&&o.peer_evictions==1);
 for(unsigned i=0;i<3;i++)assert(o.peer[i].identity==100+i);
 live.scope.active=100;live.scope.boot=200;live.nominee=101;live.nominee_boot=201;live.selected.hot=102;live.selected.hot_boot=202;
 for(unsigned i=3;i<GM7_PEERS;i++)live.peer[i].radio_refs=1;
 assert(receive(&live,status,7000000)==-ENOSPC&&!live.peer_evictions);
 // Never-selected inferior lineages do not exhaust the separate term ledger.
 struct gm7_owner older;assert(!gm7_owner_init(&older,&g,1,2,true,1000000));
 struct gm7_packet ann=fixture("announce");older.serving=true;
 older.scope=(struct gm7_scope){.birth=ann.body.control.birth_us-1000000,.founder=1,.term=1,.active=1,.boot=2};
 for(unsigned i=0;i<12;i++){ann.body.control.sender=ann.body.control.active=100+i;ann.body.control.boot=ann.body.control.active_boot=200+i;ann.body.control.founder=100+i;ann.body.control.birth_us+=1000000;ann.body.control.sent_mono_us+=5000000;ann.body.control.sent_utc_us+=5000000;assert(!receive(&older,ann,1000000+(uint64_t)i*5000000));}
 unsigned floors=0;for(unsigned i=0;i<GM7_RETAINED;i++)floors+=older.retained[i].used;
 assert(!floors&&older.serving&&older.peer_evictions==4);
 puts("PASS bounded8-peer pressure cache recycles only expired unprotected incarnation, preserves selected/nominee/radio refs; all-live/protected fail closed;12inferior lineages do not spendshared8-record term/terminal ledger");
}

static void terminal_dominance(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,81,97,true,1000000));
 struct gm7_packet ann=fixture("announce");struct gm7_preparation prep={0};struct gm7_source source={0};
 for(unsigned term=1;term<=16;term++){
  uint64_t t=1000000+(uint64_t)term*500000;
  ann.body.control.term=term;ann.body.control.sequence=term*2;ann.body.control.sent_mono_us=t;
  assert(!receive(&o,ann,t));gm7_owner_update(&o,&source,&prep,t+1);
  struct gm7_packet w=ann;w.kind=GM7_WITHDRAW;w.flags=0;w.body.control.sequence++;w.body.control.sent_mono_us++;
  w.body.control.max_age_us=1000000;w.body.control.source_valid_us=0;
  assert(!receive(&o,w,t+1000)&&o.withdrawn);
 }
 unsigned used=0;for(unsigned i=0;i<GM7_RETAINED;i++)used+=o.retained[i].used;
 assert(used==2&&!o.overflow);
 ann.body.control.term=1;ann.body.control.sequence=1000;ann.body.control.sent_mono_us=20000000;
 assert(receive(&o,ann,20000000)==-ESTALE);
 puts("PASS16same-mesh successive terminal terms retain only current terminal+stronger term floor; lower-term replay remains fenced, no failover-count exhaustion");
}
static struct gm7_source physical_source(uint64_t t){
 return (struct gm7_source){.utc_us=UINT64_C(1792800000000000)+t,.sampled_us=t,.until_us=t+1500000,.pps_at=t,.source_gen=3,.capture_gen=4,.association_gen=1,.quality={128,2,255,128,65535},.gnss_healthy=true,.pps_healthy=true,.utc_available=true,.physical_ready=true};
}
static void birth_and_ties(void){
 struct r2_group_config g=group();struct gm7_owner o;assert(!gm7_owner_init(&o,&g,17,33,true,0));
 struct gm7_preparation prep={0};
 for(uint64_t t=0;t<=66000000;t+=1000000){struct gm7_source source=physical_source(t);gm7_owner_update(&o,&source,&prep,t);}
 assert(o.initial_due&&!o.scope.birth);uint64_t t=66000000;
 assert(!gm7_owner_reserve(&o,t+10000,t+14000,t)&&!o.scope.birth);
 uint64_t failed_birth=o.reserved_scope.birth;gm7_owner_reservation_cancel(&o,t+1);assert(!o.scope.birth);
 assert(!gm7_owner_reserve(&o,t+20000,t+24000,t+2));
 assert(o.reserved_scope.birth==UINT64_C(1792800000000000)+t+2&&o.reserved_scope.birth!=failed_birth);
 struct gm7_packet packet;assert(!gm7_owner_planned_control(&o,&packet,t+3));
 assert(!gm7_owner_reservation_commit(&o,t+20000,t+21400,t+21400));
 assert(o.serving&&o.scope.birth==packet.body.control.birth_us&&o.scope.founder==17&&o.scope.term==1&&o.scope.boot==33);
 for(unsigned tie=0;tie<2;tie++){
  struct gm7_owner rival;assert(!gm7_owner_init(&rival,&g,81,97,true,1000000));rival.serving=true;
  struct gm7_packet ann=fixture("announce");rival.scope=(struct gm7_scope){.birth=ann.body.control.birth_us,.founder=tie?ann.body.control.founder:81,.term=ann.body.control.term,.active=81,.boot=97};
  assert(!receive(&rival,ann,1000000)&&!rival.serving&&rival.reason==GM7_R_MERGE);
 }
 puts("PASS full64bitUTC birth of successful firstGM initiation, cancelled unexposed election leaves no birth, founder/leader deterministic equalbirth ties and immediate loser stop before physical follow");
}
static void independent_role(void){
 struct r2_group_config g=group();struct gm7_packet r=fixture("role-diagnostic");
 struct gm7_role *v=&r.body.role;
 v->sender=v->active=501;v->boot=v->active_boot=777;v->role=GM7_W_ACTIVE;
 v->birth_us=UINT64_C(1792800000000000);v->founder=501;v->term=2;
 v->sequence=10;v->sampled_mono_us=1000000;
 for(unsigned tie=0;tie<3;tie++){
  struct gm7_owner o;assert(!gm7_owner_init(&o,&g,81,97,true,1000000));
  o.serving=true;o.eligible=true;o.scope=(struct gm7_scope){.birth=v->birth_us+(tie?0:1),.founder=tie==1?502:501,.term=1,.active=81,.boot=97};
  o.reserved=true;o.event.valid=true;o.event.negative=false;
  assert(!receive(&o,r,1000100)&&!o.serving&&!o.reserved&&!o.event.valid&&o.reason==GM7_R_MERGE);
  assert(!o.selected.active&&!o.armed&&o.scope.active==501);
  uint64_t until=o.peer[0].reported.until;
  assert(receive(&o,r,1000200)==-ESTALE&&o.peer[0].reported.until==until);
  v->sequence++;v->sampled_mono_us++;v->source_gen++;v->capture_gen++;v->train_gen++;
  assert(!receive(&o,r,1000300)&&o.peer[0].reported.until>until);
  struct gm7_reported_role report;bool current;
  assert(gm7_peer_role(&o,0,1000300,&report,&current)&&!current);
 }
 struct gm7_owner o;assert(!gm7_owner_init(&o,&g,81,97,true,1000000));
 o.serving=true;o.scope=(struct gm7_scope){.birth=v->birth_us-1,.founder=81,.term=1,.active=81,.boot=97};
 assert(!receive(&o,r,1000100)&&o.serving); /* Older local stays ACTIVE. */
 v->sender=502;assert(receive(&o,r,1000200)==-EINVAL&&o.serving);v->sender=501;
 v->role=GM7_W_STANDBY;v->sequence++;assert(!receive(&o,r,1000300)&&o.serving);
 v->role=GM7_W_ACTIVE;v->sequence++;v->boot=v->active_boot=778;
 assert(!receive(&o,r,1000400)&&o.peer[0].quarantined&&o.peer[1].quarantined&&o.serving);
 puts("PASS ANN-free self-ACTIVE ROLE suppresses only newer lineage, exact-birth founder/term ties, independent generation churn, duplicate TTL, nonself/follower negatives and simultaneous reboot quarantine");
}
int main(void){printf("sizeof owner=%zu peer=%zu control=%zu scheduler=%zu\n",sizeof(struct gm7_owner),sizeof(struct gm7_peer),sizeof(struct gm7_peer_control),sizeof(struct gm7_scheduler));codec();receipt_horizons();ordinary_inventory();ordinary_schedule();capacity_pressure();terminal_dominance();birth_and_ties();independent_role();phase_policy();promote();return 0;}
