#include "relay_precision.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_group_config g={.id=123,.epoch=1};for(unsigned i=0;i<32;i++)g.psk[i]=(uint8_t)(i+1);
 uint64_t ids[4]={11,22,33,0};uint8_t proof[32],wire[190],bad[190];
 struct r2_relay_packet p={.origin={.root=0,.relay=1,.boot=10,.source_epoch=2,
  .created_ns=1000000000,.expires_ns=2500000000,.frame_ns=1000000000,.utc_offset_ns=1000000000000,
  .hold_horizon_us=10000000,.hold_budget_ns=1000},
  .route={.transmitter=0,.parent=0,.boot=10,.train=1,.publication=1,.capture_epoch=1,.map_generation=1,.bound_ns=50}};
 assert(!r2_group_origin_sign3(proof,&p.origin,&g,ids));assert(!r2_group_relay_encode3(wire,&p,proof,&g,ids));
 struct r2_relay_packet q;
 for(unsigned client=0;client<5;client++)assert(!r2_group_relay_decode3(&q,wire,sizeof wire,&g,ids));
 assert(q.origin.created_ns==p.origin.created_ns&&q.origin.hold_horizon_us==10000000&&q.origin.hold_budget_ns==1000);
 assert(r2_group_relay_decode2(&q,wire,sizeof wire,&g,ids));
 assert(r2_group_relay_decode(&q,wire,sizeof wire,&g,ids));
 for(unsigned i=0;i<sizeof wire;i++){memcpy(bad,wire,sizeof bad);bad[i]^=1;assert(r2_group_relay_decode3(&q,bad,sizeof bad,&g,ids));}
 /* Updating only the outer MAC cannot widen the signed root horizon.
  * Shared-PSK holders can also forge root signatures; this is not key isolation. */
 memcpy(bad,wire,sizeof bad);r2_group_put(bad+22,11000000,4);assert(!r2_group_mac(bad+174,&g,"PTOBG3R3",bad,174));assert(r2_group_relay_decode3(&q,bad,sizeof bad,&g,ids));
 /* The 150 ms permission margin changes GR3 only; legacy decoders reject it.
  * No held budget or map freshness is changed by the signed permission. */
 struct r2_relay_packet permit=p;permit.origin.hold_horizon_us=permit.origin.hold_budget_ns=0;
 permit.origin.expires_ns=permit.origin.created_ns+1650000000;
 assert(!r2_relay_validate3(&permit));assert(r2_relay_validate(&permit));
 assert(!r2_group_origin_sign3(proof,&permit.origin,&g,ids));
 assert(!r2_group_relay_encode3(wire,&permit,proof,&g,ids));
 assert(!r2_group_relay_decode3(&q,wire,sizeof wire,&g,ids));
 --permit.origin.expires_ns;assert(!r2_relay_validate3(&permit));
 permit.origin.expires_ns+=2;assert(r2_relay_validate3(&permit));
 struct r2_relay_precision s;r2_relay_precision_init(&s,1,0,1,20,1);
 s.origin_valid=true;s.origin=p.origin;s.root_boot=10;s.source_epoch=2;
 s.hold_bound=(struct r2_hold_bound){.reference_ns=100,.transfer_ns=50,.quantization_ns=0,.rate_ppb=100,.provenance=R2_BOUND_FIXTURE};
 s.map[0]=(struct r2_relay_map_record){.map={.delivery=16000000,.anchor=1000000000,.valid=true},.origin=p.origin,.accepted=16000000,.bound=50,.used=true};
 int64_t ns,utc;
 struct r2_capture now={.epoch=1,.tick=16000000+16*6000000,.kind=R2_CAPTURE_NOW};
 assert(r2_relay_precision_read(&s,now,7000000,&ns,&utc));assert(ns==7000000000); /* >5s, original reference unchanged */
 now.tick=16000000+16*8000000;assert(r2_relay_precision_read(&s,now,9000000,&ns,&utc));
 now.tick+=16;assert(!r2_relay_precision_read(&s,now,9000001,&ns,&utc));
 /* A newer count0 envelope cannot make the selected old-origin map cover
  * the next admission window. Future check reads map provenance itself. */
 struct r2_relay_precision leaf=s;leaf.self=2;leaf.hold_bound.provenance=R2_BOUND_UNKNOWN;
 leaf.map[0].origin.hold_horizon_us=0;leaf.origin.expires_ns=9000000000;
 leaf.origin_until_tick=16000000*9;
 struct r2_capture horizon={.epoch=1,.tick=16000000*2,.kind=R2_CAPTURE_NOW};
 assert(r2_relay_precision_read(&leaf,horizon,2000000,&ns,&utc));
 horizon.tick=16000000*3;assert(!r2_relay_precision_read(&leaf,horizon,3000000,&ns,&utc));
 /* Expiry < recovery grace: 1000ppb consumes the same budget at0.8s. */
 s.hold_bound.rate_ppb=1000;now.tick=16000000+16*800000;assert(r2_relay_precision_read(&s,now,1800000,&ns,&utc));
 now.tick+=16;assert(!r2_relay_precision_read(&s,now,1800001,&ns,&utc));
 s.hold_bound.provenance=R2_BOUND_UNKNOWN;now.tick=16000000+16*250000;assert(!r2_relay_precision_read(&s,now,1250000,&ns,&utc));
 now.tick--;assert(r2_relay_precision_read(&s,now,1249999,&ns,&utc)); /* legacy experimental fresh unaffected */
 /* Rev2 parent exception is500ms TOTAL, not250+500; no new correction
  * means no refresh. Root permission and capture epoch still clip output. */
 s.experimental_fresh_us=500000;now.tick=16000000+16*499999;
 assert(r2_relay_precision_read(&s,now,1499999,&ns,&utc));
 now.tick+=16;assert(!r2_relay_precision_read(&s,now,1500000,&ns,&utc));
 s.map[0].origin.expires_ns=1300000000;now.tick=16000000+16*300000;
 assert(!r2_relay_precision_read(&s,now,1300000,&ns,&utc));
 /* A capable but idle passive observer uses the leaf display policy;
  * switching to actual relay service immediately restores the500ms cutoff. */
 s.map[0].origin=p.origin;s.map[0].origin.hold_horizon_us=s.map[0].origin.hold_budget_ns=0;
 s.passive_receiver=true;now.tick=16000000+16*700000;
 assert(r2_relay_precision_read(&s,now,1700000,&ns,&utc));
 struct r2_relay_prepared disabled;assert(!r2_relay_precision_prepare(&s,now,1700000,false,&disabled));
 s.passive_receiver=false;assert(!r2_relay_precision_read(&s,now,1700000,&ns,&utc));
 s.experimental_fresh_us=0;s.map[0].origin=p.origin;
 s.hold_bound.provenance=R2_BOUND_FIXTURE;now.epoch=2;assert(!r2_relay_precision_read(&s,now,1249999,&ns,&utc));
 r2_relay_precision_withdraw(&s);now.epoch=1;assert(!r2_relay_precision_read(&s,now,1249999,&ns,&utc));
 puts("V14 GRv3 corruption/mixed-version/five-clients, immutable held age, <5s/>5s error expiry, unknown calibration and domain fence PASS");
}
