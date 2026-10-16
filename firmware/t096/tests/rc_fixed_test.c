/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_fixed.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct rc4_routes routes={.count=4,.node={{.id=1001,.parent=0,.slot=0,.depth=0,.transmitter=true},{.id=9001,.parent=0,.slot=1,.depth=1,.transmitter=true},{.id=17001,.parent=1,.slot=2,.depth=2,.transmitter=true},{.id=25001,.parent=2,.slot=255,.depth=2}}};
static struct r2_group_config group={.id=7,.epoch=9,.psk={1,2,3}};
static struct rc4_origin origin={.root=1001,.boot=101,.epoch=7,.issued=0,.expires=1650000000,.frame=0};
static struct rc4_packet packet(unsigned sender,uint32_t pub){struct rc4_packet p={.origin=origin,.sender=sender,.boot=101+sender,.train=1,.publication=pub,.epoch=1};assert(!rc4_origin_sign(p.proof,&p.origin,&routes,&group));return p;}
static uint64_t tick(uint64_t ns,int32_t ppb){return (ns*16/1000)*(uint64_t)(1000000000+(int64_t)ppb)/1000000000;}
static void receive(struct rc4_state *s,struct rc4_packet *p,uint64_t ns,int32_t ppb,unsigned event){uint8_t ad[190];struct rc4_packet q;assert(!rc4_encode(ad,p,&routes,&group));assert(!rc4_decode(&q,ad,sizeof ad,&routes,&group));(void)rc4_receive(s,&q,tick(ns,ppb),event,5,tick(ns+2000000,ppb));}
static bool bind_initial(struct rc4_state *s,uint64_t parent){
 struct rc4_admission a={.parent_boot=parent,.root_boot=101,.root_epoch=7,.parent_train=1,.parent_epoch=1,.root_lo_ns=0,.root_hi_ns=1000000,.root_expires_ns=1650000000};return rc4_bind(s,&a);
}
int main(void){
 assert(rc4_routes_valid(&routes));struct rc4_routes bad=routes;bad.node[2].parent=2;assert(!rc4_routes_valid(&bad));bad=routes;bad.node[2].depth=3;assert(!rc4_routes_valid(&bad));bad=routes;bad.node[2].slot=1;assert(!rc4_routes_valid(&bad));
 struct rc4_packet p=packet(0,2);p.count=2;p.correction[0]=(struct rc4_correction){.publication=1,.event=65535,.channel=36,.root_ns=100,.tx_ns=100,.bound=20};p.correction[1]=p.correction[0];p.correction[1].event=65534;
 uint8_t ad[190];struct rc4_packet q;assert(!rc4_encode(ad,&p,&routes,&group));printf("RC4_VECTOR ");for(unsigned i=0;i<190;i++)printf("%02x",ad[i]);puts("");assert(!rc4_decode(&q,ad,190,&routes,&group));assert(q.correction[1].event==65534);
 for(unsigned i=0;i<190;i++){uint8_t save=ad[i];ad[i]^=1;assert(rc4_decode(&q,ad,190,&routes,&group));ad[i]=save;}
 bad=routes;bad.node[3].id++;assert(rc4_decode(&q,ad,190,&bad,&group));assert(rc4_decode(&q,ad,189,&routes,&group));
 struct rc4_state a,b,c;rc4_init(&a,&routes,1);rc4_init(&b,&routes,2);rc4_init(&c,&routes,3);
 p=packet(0,1);assert(!rc4_receive(&a,&p,0,0,5,100));assert(!a.reference.valid);
 assert(bind_initial(&a,101)&&bind_initial(&b,102)&&bind_initial(&c,103));
 /* Exact causal two-step timeline: 0 ->62.5 ->78.125 ->140.625
  * ->156.25 ->218.75ms. Actual captured receipt, never scheduled UTC. */
 p=packet(0,1);receive(&a,&p,0,0,0);
 p=packet(0,2);p.count=1;p.correction[0]=(struct rc4_correction){.publication=1,.event=0,.channel=5,.root_ns=0,.tx_ns=0,.bound=10};receive(&a,&p,62500000,0,1);assert(a.accepted==1);
 struct rc4_rate rate={.uncertainty_ppb=100,.capture_ns=63,.link_ns=0,.link_bound=5};struct rc4_correction ca,cb,cc;
 assert(!rc4_forward(&a,tick(63000000,0),0,&rate,1,0,5,&ca));
 assert(rc4_forward(&a,tick(78125000,0),0,&rate,1,0,5,&ca));assert(ca.tx_ns==78125000&&ca.root_ns==0&&ca.bound==149);
 p=packet(1,1);receive(&b,&p,78125000,50000,0);
 p=packet(1,2);p.count=1;p.correction[0]=ca;receive(&b,&p,140625000,50000,1);assert(b.accepted==1);
 rate.ppb=50000;assert(rc4_forward(&b,tick(156250000,50000),0,&rate,1,0,5,&cb));assert(cb.tx_ns>=156249937&&cb.tx_ns<=156250063&&cb.root_ns==0&&cb.bound>=280);
 p=packet(2,1);receive(&c,&p,156250000,-50000,0);p=packet(2,2);p.count=1;p.correction[0]=cb;receive(&c,&p,218750000,-50000,1);assert(c.accepted==1);
 rate.ppb=-50000;assert(!rc4_forward(&c,tick(221000000,-50000),0,&rate,1,0,5,&cc));
 assert(rc4_read(&c,tick(221000000,-50000),&rate,&cc.tx_ns,&cc.bound));assert(cc.tx_ns>=220999875&&cc.tx_ns<=221000125);
 printf("RC4_PIPELINE completion_ns=218750000 read_ns=%llu error_ns=%lld bound_ns=%u\n",(unsigned long long)cc.tx_ns,(long long)cc.tx_ns-221000000,cc.bound);
 rate.bias_bound_ns=RC4_UNKNOWN;assert(rc4_read(&c,tick(221000000,-50000),&rate,&cc.tx_ns,&cc.bound)&&cc.bound==RC4_UNKNOWN);rate.bias_bound_ns=0;
 rate.uncertainty_ppb=RC4_UNKNOWN;assert(rc4_read(&c,tick(221000000,-50000),&rate,&cc.tx_ns,&cc.bound)&&cc.bound==RC4_UNKNOWN);
 assert(!rc4_forward(&c,tick(500000000,-50000),0,&rate,1,0,5,&cc));
 /* Same-timebase train recreation preserves already accepted held time only. */
 struct rc4_state maintained=c;struct rc4_packet recreated=packet(2,1);recreated.train=2;
 uint64_t held_root=maintained.reference.root_ns,held_rx=maintained.reference.rx;
 assert(!rc4_receive(&maintained,&recreated,tick(300000000,-50000),0,5,tick(302000000,-50000)));
 assert(maintained.reference.valid&&maintained.reference.rx==held_rx&&maintained.reference.root_ns==held_root);
 assert(rc4_read(&maintained,tick(303000000,-50000),&rate,&cc.tx_ns,&cc.bound));
 assert(!rc4_receive(&maintained,&p,tick(310000000,-50000),3,5,tick(312000000,-50000)));
 assert(maintained.reference.rx==held_rx);
 assert(!rc4_read(&maintained,tick(1660000000,-50000),&rate,&cc.tx_ns,&cc.bound));
 recreated.epoch=2;assert(!rc4_receive(&maintained,&recreated,tick(320000000,-50000),1,5,tick(322000000,-50000)));assert(!maintained.reference.valid);
 unsigned accepted=c.accepted;receive(&c,&p,218750000,-50000,1);assert(c.accepted==accepted);
 q=p;q.train=0;assert(!rc4_receive(&c,&q,4000000,2,5,4000000));q=p;q.origin.epoch=6;assert(!rc4_receive(&c,&q,4000000,2,5,4000000));q=p;q.boot=100;assert(!rc4_receive(&c,&q,4000000,2,5,4000000));
 p=packet(2,1);receive(&c,&p,300000000,-50000,0);p=packet(2,2);p.count=1;p.correction[0]=cb;receive(&c,&p,362500000,-50000,1);assert(c.accepted==accepted); /* consumed-original replay */
 assert(!rc4_receive(&c,&p,tick(1700000000,0),1,5,tick(1700000000,0)));assert(!rc4_read(&c,tick(1700000000,0),&rate,&cc.tx_ns,&cc.bound));
 rc4_withdraw(&c);assert(!c.reference.valid);p=packet(2,2);p.count=1;p.correction[0]=cb;receive(&c,&p,218750000,-50000,1);assert(!c.reference.valid); /* missing original */
 for(unsigned i=0;i<RC4_HISTORY+1;i++){p=packet(2,10+i);receive(&c,&p,300000000+i*1000,-50000,i);}assert(c.evicted>=1);
 struct rc4_demand d={0};assert(rc4_demand_begin(&d,7,9,123,100));assert(!rc4_demand_begin(&d,7,9,124,101));assert(!rc4_demand_accept(&d,7,9,124,1,30000000,200));assert(rc4_demand_accept(&d,7,9,123,1,30000000,200));assert(rc4_demand_live(&d,30000199)&&!rc4_demand_live(&d,30000200));
 assert(!rc4_demand_accept(&d,7,9,123,1,30000000,300));assert(rc4_demand_begin(&d,7,9,124,400));assert(!rc4_demand_accept(&d,7,9,124,1,30000000,500));assert(!rc4_demand_accept(&d,7,9,124,2,30000000,2000400));assert(rc4_demand_begin(&d,7,9,125,2000400));assert(rc4_demand_accept(&d,7,9,125,2,1,2000401));
 uint64_t held=d.deadline;assert(rc4_demand_begin(&d,7,99,777,2000402));assert(d.deadline==held);assert(!rc4_demand_accept(&d,7,99,778,1,10,2000403));assert(d.deadline==held);assert(rc4_demand_accept(&d,7,99,777,1,10,2000403));assert(d.deadline==held);
 /* A new uncompleted envelope must not evict accepted service. Corrections
  * cannot cross origins; promotion occurs only for a matching completed event. */
 struct rc4_state envelope=a;struct rc4_packet transition=packet(0,3);
 transition.origin.issued=80000000;transition.origin.expires=1730000000;
 assert(!rc4_origin_sign(transition.proof,&transition.origin,&routes,&group));
 assert(!rc4_receive(&envelope,&transition,1600000,3,5,1632000));
 assert(envelope.reference.origin.issued==0&&envelope.reference.valid);
 transition.publication=4;transition.count=1;transition.correction[0]=(struct rc4_correction){.publication=2,.event=1,.channel=5,.root_ns=100000000,.tx_ns=100000000,.bound=0};
 assert(!rc4_receive(&envelope,&transition,2600000,4,5,2632000));assert(envelope.reference.origin.issued==0);
 transition.publication=5;transition.correction[0].publication=3;transition.correction[0].event=3;
 assert(rc4_receive(&envelope,&transition,3600000,5,5,3632000));assert(envelope.reference.origin.issued==80000000&&envelope.reference.parent_tx_ns==100000000);
 /* Cold reboot, same root epoch: fresh coarse time and stream attestation
  * reject ancient originals, including packets above an empty publication floor. */
 struct rc4_state cold;rc4_init(&cold,&routes,3);
 struct rc4_admission grant={.parent_boot=103,.root_boot=101,.root_epoch=7,.parent_train=1,.parent_epoch=1,.publication_floor=100,.root_lo_ns=20000000000ULL,.root_hi_ns=20001000000ULL,.root_expires_ns=21650000000ULL};
 assert(rc4_bind(&cold,&grant));p=packet(2,1);assert(!rc4_receive(&cold,&p,0,0,5,32000));assert(!cold.pending[0].used);
 p=packet(2,101);assert(!rc4_receive(&cold,&p,0,0,5,32000));p=packet(2,102);p.count=1;p.correction[0]=(struct rc4_correction){.publication=101,.event=0,.channel=5,.root_ns=0,.tx_ns=0,.bound=0};assert(!rc4_receive(&cold,&p,1000000,1,5,1032000));assert(!cold.reference.valid);
 uint64_t remaining=grant.root_expires_ns-grant.root_hi_ns;
 for(int ppm=-300;ppm<=300;ppm+=50){
  /* Independent physical duration bound for the locally ticked permission. */
  assert(cold.valid_until*1000000ULL<=remaining*16/1000*(uint64_t)(1000000+ppm));
 }
 struct rc4_state renewed=a;grant=(struct rc4_admission){.parent_boot=101,.root_boot=101,.root_epoch=7,.parent_train=1,.parent_epoch=1,.publication_floor=2,.now_tick=4800000,.root_lo_ns=299000000,.root_hi_ns=301000000,.root_expires_ns=1650000000};
 assert(rc4_bind(&renewed,&grant));assert(renewed.reference.valid&&renewed.reference.rx==a.reference.rx);
 rate.ppb=0;assert(rc4_read(&renewed,4848000,&rate,&cc.tx_ns,&cc.bound));
 assert(rc4_demand_begin(&d,8,9,1,0));assert(!d.deadline); /* local reboot */
 /* Five-minute power contract: conservative360s context and independently
  * bounded source expiry. True oscillator values appear only in this test. */
 for(int32_t ppb=-300000;ppb<=300000;ppb+=100000){
  struct rc4_state long_context;rc4_init(&long_context,&routes,1);assert(bind_initial(&long_context,101));
  uint64_t lo,hi,at_ns=UINT64_C(359000000000),at_tick=tick(at_ns,ppb);
  assert(rc4_coarse_interval(&long_context,at_tick,&lo,&hi));assert(lo<=at_ns&&hi>=at_ns);
  assert(long_context.context_until<=tick(UINT64_C(360000000000),ppb));
  struct rc4_packet fresh=packet(0,200);fresh.origin.issued=at_ns;fresh.origin.expires=at_ns+RC4_SOURCE_NS;
  assert(!rc4_receive(&long_context,&fresh,at_tick,200,5,at_tick+32000));
  assert(long_context.valid_until<at_tick); /* Raw packet cannot extend source. */
  fresh.publication=201;fresh.count=1;fresh.correction[0]=(struct rc4_correction){.publication=200,.event=200,.channel=5,.root_ns=at_ns,.tx_ns=at_ns,.bound=0};
  uint64_t complete=tick(at_ns+62500000,ppb);
  assert(rc4_receive(&long_context,&fresh,complete,201,5,complete+32000));
  assert(long_context.valid_until>complete&&long_context.valid_until<=tick(fresh.origin.expires,ppb));
  uint64_t deadline=long_context.valid_until;
  assert(!rc4_receive(&long_context,&fresh,complete+1000000,201,5,complete+1000000));assert(long_context.valid_until==deadline);
  assert(!rc4_receive(&long_context,&fresh,long_context.context_until,202,5,long_context.context_until));
  rate.ppb=ppb;rate.rx_tx_bias_ns=0;
  assert(rc4_read(&long_context,complete+48000,&rate,&cc.tx_ns,&cc.bound));
  assert(!rc4_read(&long_context,long_context.context_until,&rate,&cc.tx_ns,&cc.bound));
 }
 for(uint64_t remaining_us=200000;remaining_us<=RC4_LEASE_US;remaining_us+=999983){
  uint32_t lease=rc4_upstream_lease(remaining_us);
  long double parent_seconds=(long double)lease/999700.0L;
  long double child_seconds=(long double)remaining_us/1000300.0L;
  assert(parent_seconds+0.150046L<=child_seconds);
 }
 assert(!rc4_upstream_lease(150000));
 struct rc4_state originals;rc4_init(&originals,&routes,1);assert(bind_initial(&originals,101));struct rc4_rate_fit cold_fit={0};
 for(unsigned event=0;event<12;event++){
  struct rc4_packet only=packet(0,event+1);uint64_t when=(uint64_t)event*62500000;
  assert(!rc4_receive(&originals,&only,tick(when,0),event,5,tick(when+2000000,0)));
  assert(!originals.reference.valid&&!cold_fit.valid);
 }
 for(unsigned event=12;event<23;event++){
  struct rc4_packet follow=packet(0,event+1);uint64_t when=(uint64_t)event*62500000;
  follow.count=1;follow.correction[0]=(struct rc4_correction){.publication=event,.event=event-1,.channel=5,.root_ns=when-62500000,.tx_ns=when-62500000,.bound=0};
  assert(rc4_receive(&originals,&follow,tick(when,0),event,5,tick(when+2000000,0)));
  (void)rc4_rate_add(&cold_fit,&originals.reference);
 }
 assert(cold_fit.valid&&originals.reference.valid); /* No reboot/recreation. */
 struct rc4_demand five_minute={0};assert(rc4_demand_begin(&five_minute,1,2,1,0));assert(rc4_demand_accept(&five_minute,1,2,1,1,RC4_LEASE_US,1));
 assert(rc4_demand_live(&five_minute,300000000));assert(rc4_demand_begin(&five_minute,1,2,2,300000000));assert(!rc4_demand_accept(&five_minute,1,2,3,2,RC4_LEASE_US,300000100));assert(rc4_demand_live(&five_minute,359999999));assert(!rc4_demand_live(&five_minute,360000001));
 struct rc4_state huge=a;huge.admission.root_lo_ns=UINT64_MAX-100000000;huge.admission.root_hi_ns=UINT64_MAX-100000000;huge.reference.parent_tx_ns=UINT64_MAX-100000000;huge.reference.root_ns=UINT64_MAX-200000000;huge.reference.origin.expires=UINT64_MAX;rate.ppb=0;rate.rx_tx_bias_ns=9000;assert(rc4_read(&huge,huge.reference.rx+1250000,&rate,&cc.tx_ns,&cc.bound));assert(cc.tx_ns>INT64_MAX);
 struct rc4_control request={.type=RC4_REQUEST,.sender=3,.target=2,.sender_boot=104,.target_boot=103,.challenge=1,.nonce=2,.sequence=1,.lease_us=30000000},decoded;
 uint8_t ctl[RC4_CONTROL_BYTES];assert(!rc4_control_encode(ctl,&request,&routes,&group));assert(!rc4_control_decode(&decoded,ctl,sizeof ctl,&routes,&group));assert(decoded.nonce==2);
 for(unsigned i=0;i<sizeof ctl;i++){ctl[i]^=1;assert(rc4_control_decode(&decoded,ctl,sizeof ctl,&routes,&group));ctl[i]^=1;}
 request.target=0;assert(rc4_control_encode(ctl,&request,&routes,&group));
 puts("RC4_CONTRACT_PASS codec190_mutation190 fixed_routes event_residence two_step219ms rate_bounds cold_demand replay expiry loss history");return 0;
}
