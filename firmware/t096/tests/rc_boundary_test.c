/* SPDX-License-Identifier: Apache-2.0 */
#include "rc_boundary.h"
#include "rc_field_policy.h"
#include "rc_gm_adapter.h"
#include "rc_direct_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct rc5_routes routes={.count=4,.node={{.id=1001,.parent=0,.slot=0,.depth=0,.transmitter=true},{.id=9001,.parent=0,.slot=1,.depth=1,.transmitter=true},{.id=17001,.parent=1,.slot=2,.depth=2,.transmitter=true},{.id=25001,.parent=2,.slot=255,.depth=2}}};
static struct r2_group_config group={.id=7,.epoch=9,.psk={1,2,3}};
static struct rc5_origin origin={.root=1001,.boot=101,.epoch=7,.issued=0,.expires=1650000000};
static struct rc5_packet packet(uint32_t pub){struct rc5_packet p={.origin=origin,.sender=0,.boot=101,.train=1,.publication=pub,.epoch=1};assert(!rc5_origin_sign(p.proof,&p.origin,&routes,&group));return p;}
static uint64_t tick(uint64_t ns){return ns*16/1000;}
int main(void){

 /* Processing-delay discrimination on the actual matched-original path. */
 for(unsigned delay=0;delay<3;delay++){
  struct rc5_state d;rc5_init(&d,&routes,1);
  struct rc5_admission a={.parent_boot=101,.root_boot=101,.root_epoch=7,.parent_train=1,.parent_epoch=1,.now_tick=0,.root_lo_ns=0,.root_hi_ns=0,.root_expires_ns=1650000000};
  assert(rc5_bind(&d,&a));struct rc5_packet first=packet(1);
  uint64_t original=tick(100000000),first_available=original+160;
  assert(!rc5_receive(&d,&first,original,65535,5,first_available));
  struct rc5_packet follow=packet(2);follow.count=1;
  follow.correction[0]=(struct rc5_correction){.publication=1,.event=65535,.channel=5,.generation=3,.tx_ns=100000000,.bound=RC5_UNKNOWN};
  struct rc5_packet wrong=follow;wrong.correction[0].channel=6;
  assert(!rc5_receive(&d,&wrong,tick(162500000),0,5,tick(162500000)+160));
  /* Recreate the state because packet replay fencing intentionally consumed the wrong follow-up's publication. */
  rc5_init(&d,&routes,1);assert(rc5_bind(&d,&a));assert(!rc5_receive(&d,&first,original,65535,5,first_available));
  uint64_t follow_rx=tick(162500000),available=follow_rx+(delay==0?160:delay==1?16000:800000);
  assert(rc5_receive(&d,&follow,follow_rx,0,5,available));
  assert(d.reference.rx==original&&d.reference.parent_tx_ns==100000000&&d.reference.original_available==first_available);
  assert(d.reference.completed_at==available&&d.reference.followup_rx==follow_rx&&d.reference.publication==1&&d.reference.event==65535&&d.reference.channel==5);
 }
 /* Empirical offset is nanoseconds at the map anchor, never raw tick rewriting.
  * Unmeasured relay profiles are unchanged and bounds stay unknown. */
 assert(rc_direct_pps_correction(1)==RC_DIRECT_PPS_CORRECTION_NS&&rc_direct_pps_correction(2)==RC_DIRECT_PPS_CORRECTION_NS);
 assert(rc_direct_pps_correction(3)==RC_DIRECT_PPS_CORRECTION_NS);
 {
  struct rc5_state s={.bound=true,.context_until=8000000,.root_boot=1,.parent_boot=2,.root_epoch=1,.parent_epoch=1,
   .reference={.valid=true,.rx=1000000,.completed_at=1100000,.parent_tx_ns=1000000000,.bound=RC5_UNKNOWN}};
  struct rc5_rate_fit f={.valid=true};struct rc5_rate raw={.capture_ns=RC5_UNKNOWN,.bias_bound_ns=RC5_UNKNOWN,.link_bound=RC5_UNKNOWN,.uncertainty_ppb=RC5_UNKNOWN},shift=raw;
  shift.rx_tx_bias_ns=RC_DIRECT_PPS_CORRECTION_NS;struct rc5_clock a={0},b={0};struct rc5_stamp x,y;
  assert(rc5_clock_discipline(&a,&s,&f,&raw)&&rc5_clock_discipline(&b,&s,&f,&shift));
  assert(rc5_clock_capture(&a,&s,1200000,&x)&&rc5_clock_capture(&b,&s,1200000,&y));
  assert((int64_t)y.ns-(int64_t)x.ns==RC_DIRECT_PPS_CORRECTION_NS&&y.quality==RC5_UNQUALIFIED&&y.bound==RC5_UNKNOWN);
 }
 /* Real normal-GM wire/auth adapter: UNKNOWN association remains functional. */
 struct rc5_gm_context gm={0};struct rc5_state gm_state;rc5_init(&gm_state,&routes,1);
 struct r2_join_record joined={.kind=R2_JOIN_ACCEPT,.sender=1001,.receiver=9001,.sender_boot=101,.receiver_boot=201,
  .client_nonce=1,.server_nonce=2,.sequence=1,.created_us=1000000,.echo_us=1000000,.root=1001,.root_boot=101,
  .source_epoch=7,.publication=1,.utc_us=UINT64_C(1800000001000000),.remaining_us=1000000,.source_bound_us=40000,.source_flags=1,.name="GM"};
 assert(rc5_gm_accept(&gm,&joined,1000000,1100000,17600000));
 uint64_t gm_ids[4]={1001,0,0,0};uint8_t gm_wire[190],gm_proof[32];
 struct r2_relay_packet normal={.origin={.root=0,.relay=1,.boot=101,.source_epoch=7,.created_ns=900000000,.expires_ns=2400000000,.frame_ns=900000000,.utc_offset_ns=INT64_C(1800000000000000000)},
  .route={.boot=101,.train=1,.publication=2,.capture_epoch=1,.map_generation=1,.bound_ns=UINT32_MAX}};
 assert(!r2_group_origin_sign3(gm_proof,&normal.origin,&group,gm_ids));
 assert(!r2_group_relay_encode3(gm_wire,&normal,gm_proof,&group,gm_ids));
 struct rc5_packet translated;
 assert(rc5_gm_packet(&gm,&gm_state,&translated,gm_wire,sizeof gm_wire,&group,17600000));
 assert(translated.origin.issued==normal.origin.created_ns&&translated.origin.expires==normal.origin.expires_ns&&translated.origin.frame==normal.origin.frame_ns);
 /* A late RF dropout must not confuse the original short source deadline
  * with the existing authenticated coarse scope. No authority is renewed. */
 uint64_t reacquire_tick=gm.at_tick+UINT64_C(1600000000);
 assert(reacquire_tick/16>gm.time.until);
 assert(rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));
 assert(!rc5_gm_reacquire(&gm,&gm_state,9001,reacquire_tick));
 assert(!rc5_gm_reacquire(&gm,&gm_state,1001,gm.at_tick-1));
 assert(!rc5_gm_reacquire(&gm,&gm_state,1001,gm.at_tick+RC5_CONTEXT_TICKS));
 uint64_t context_until=gm_state.context_until;
 gm_state.context_until=reacquire_tick;
 assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));
 gm_state.context_until=context_until;
 gm.authenticated=false;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm.authenticated=true;
 gm_state.bound=false;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm_state.bound=true;
 gm_state.parent_boot++;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm_state.parent_boot--;
 gm_state.root_boot++;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm_state.root_boot--;
 gm_state.root_epoch++;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm_state.root_epoch--;
 gm_state.parent_train++;assert(!rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));gm_state.parent_train--;
 assert(rc5_gm_reacquire(&gm,&gm_state,1001,reacquire_tick));
 assert(gm.at_tick==17600000&&gm_state.context_until==context_until);
 gm_wire[50]^=1;assert(!rc5_gm_packet(&gm,&gm_state,&translated,gm_wire,sizeof gm_wire,&group,17600000));gm_wire[50]^=1;
 gm.source.root_boot++;assert(!rc5_gm_packet(&gm,&gm_state,&translated,gm_wire,sizeof gm_wire,&group,17600000));gm.source.root_boot--;
 assert(!rc5_gm_packet(&gm,&gm_state,&translated,gm_wire,sizeof gm_wire,&group,gm.at_tick+RC5_CONTEXT_TICKS));
 joined.control=1;assert(!rc5_gm_accept(&gm,&joined,1000000,1100000,17600000));
 /* Slow publication must advance a whole frame, not shift requested phase.
  * Vary preparation latency across the target and multiple missed frames. */
 const uint64_t work_ticks[]={0,3200,100000,1000001,3200000};
 for(unsigned i=0;i<sizeof work_ticks/sizeof work_ticks[0];i++){
  uint64_t target=10000000,ready=target+work_ticks[i];
  assert(rc5_field_advance_start(&target,ready));
  assert(target>=ready&&target%1000000==0&&target-ready<1000000);
 }
 /* Completion immediately after any old aperture still produces a bounded
  * one-shot wait, including realistic 5/26 ms work and multiple late frames. */
 const uint64_t delays[]={1,3201,80000,416000,1000001,10000001};
 for(unsigned i=0;i<sizeof delays/sizeof delays[0];i++){
  uint64_t target=10000000,now=target+delays[i];uint32_t wait;
  assert(rc5_field_start_wait(&target,now,&wait));
  assert(target>=now&&target-now<1000000&&target%1000000==0&&wait<=62500);
  assert((uint64_t)wait*16>=target-now);
 }
 uint64_t future=12000000;uint32_t wait;
 assert(rc5_field_start_wait(&future,10001000,&wait)&&future==11000000&&wait==62438);
 uint64_t impossible=UINT64_MAX-100;assert(!rc5_field_advance_start(&impossible,UINT64_MAX));
 struct rc5_packet own={.sender=1,.boot=103,.train=9,.epoch=7};
 assert(rc5_field_tx_current(&own,1,103,9,7,true,200,100));
 assert(!rc5_field_tx_current(&own,1,103,10,7,true,200,100)); /* queued old train */
 assert(!rc5_field_tx_current(&own,1,103,9,7,true,99,100)); /* pre-request capture */
 assert(!rc5_field_tx_current(&own,1,103,9,7,false,200,100));
 assert(!rc5_field_tx_current(&own,1,104,9,7,true,200,100));
 assert(!rc5_field_tx_current(&own,1,103,9,8,true,200,100));
 uint64_t deadline=0;
 assert(rc5_field_deadline(1000,2,1500000,&deadline)&&deadline==3000000);
 assert(!rc5_field_deadline(1000,2,3000000,&deadline));
 assert(!rc5_field_deadline(2000,2,1500000,&deadline));
 assert(!rc5_field_deadline(UINT64_MAX,1,UINT64_MAX,&deadline));
 assert(!rc5_field_deadline(UINT64_MAX/1000,1,UINT64_MAX,&deadline));
 assert(rc5_routes_valid(&routes));
 /* Authenticated discovery is a role filter, not prebound address authority. */
 struct rc5_state unbound;rc5_init(&unbound,&routes,3);
 struct rc5_control cap={.type=RC5_CAPABILITY,.sender=0,.target=255,.sender_boot=101,.next_challenge=42},decoded_cap;
 uint8_t capability[RC5_CONTROL_BYTES];assert(!rc5_control_encode(capability,&cap,&routes,&group));
 assert(!rc5_control_decode(&decoded_cap,capability,sizeof capability,&routes,&group));
 assert(!rc5_field_candidate(&routes,3,decoded_cap.sender)); /* GM is audible, refused. */
 cap.sender=1;assert(!rc5_control_encode(capability,&cap,&routes,&group));
 assert(!rc5_control_decode(&decoded_cap,capability,sizeof capability,&routes,&group));
 assert(rc5_field_candidate(&routes,3,decoded_cap.sender)&&!unbound.bound);
 assert(rc5_field_candidate(&routes,1,0)&&!rc5_field_candidate(&routes,1,2));
 capability[40]^=1;assert(rc5_control_decode(&decoded_cap,capability,sizeof capability,&routes,&group));

 struct rc5_packet p=packet(2),decoded;p.count=2;
 for(unsigned i=0;i<2;i++)p.correction[i]=(struct rc5_correction){.publication=1,.event=65535-i,.channel=36,.generation=100,.age_us=100,.tx_ns=100,.bound=20};
 uint8_t ad[190];assert(!rc5_encode(ad,&p,&routes,&group));printf("RC5_VECTOR ");for(unsigned i=0;i<190;i++)printf("%02x",ad[i]);puts("");assert(!rc5_decode(&decoded,ad,190,&routes,&group));assert(decoded.correction[0].generation==100);
 for(unsigned i=0;i<190;i++){ad[i]^=1;assert(rc5_decode(&decoded,ad,190,&routes,&group));ad[i]^=1;}
 struct rc5_state state;rc5_init(&state,&routes,1);
 struct rc5_admission grant={.parent_boot=101,.root_boot=101,.root_epoch=7,.parent_train=1,.parent_epoch=1,.root_hi_ns=1000000,.root_expires_ns=1650000000};assert(rc5_bind(&state,&grant));
 struct rc5_clock clock={0};struct rc5_rate_fit fit={0};
 struct rc5_rate rate={.capture_ns=RC5_UNKNOWN,.bias_bound_ns=RC5_UNKNOWN,.uncertainty_ppb=RC5_UNKNOWN,.link_bound=RC5_UNKNOWN};
 struct rc5_stamp stamp;
 assert(!rc5_clock_capture(&clock,&state,0,&stamp)&&stamp.state==RC5_UNAVAILABLE&&stamp.quality==RC5_UNQUALIFIED);
 for(unsigned i=0;i<16;i++){
  uint64_t ns=(uint64_t)i*62500000;p=packet(i+1);
  if(i){p.count=1;p.correction[0]=(struct rc5_correction){.publication=i,.event=i-1,.channel=5,.generation=1,.tx_ns=ns-62500000,.bound=RC5_UNKNOWN};}
  bool accepted=rc5_receive(&state,&p,tick(ns),i,5,tick(ns+2000000));
  if(accepted){(void)rc5_rate_add(&fit,&state.reference);(void)rc5_clock_discipline(&clock,&state,&fit,&rate);}
 }
 assert(fit.valid&&clock.count==4);
 uint64_t captured=tick(900000000);struct rc5_stamp saved;
 assert(rc5_clock_capture(&clock,&state,captured,&saved));assert(saved.ns==900000000&&saved.quality==RC5_UNQUALIFIED&&saved.bound==RC5_UNKNOWN);
 /* Later discipline cannot retimestamp an already captured event. */
 p=packet(17);p.count=1;p.correction[0]=(struct rc5_correction){.publication=16,.event=15,.channel=5,.generation=2,.tx_ns=937500000,.bound=RC5_UNKNOWN};assert(rc5_receive(&state,&p,tick(1000000000),16,5,tick(1002000000)));
 (void)rc5_rate_add(&fit,&state.reference);assert(rc5_clock_discipline(&clock,&state,&fit,&rate));
 assert(rc5_clock_capture(&clock,&state,captured,&stamp));assert(stamp.ns==saved.ns&&stamp.generation==saved.generation);
 assert(rc5_clock_read(&clock,&state,tick(1300000000),&stamp));assert(stamp.state==RC5_HELD&&stamp.ns==1300000000&&stamp.bound==RC5_UNKNOWN);
 assert(rc5_clock_capture(&clock,&state,tick(1700000000),&stamp)&&stamp.state==RC5_HELD);
 /* Even a one-nanosecond negative phase update is continuous between reads. */
 struct rc5_clock backwards=clock;state.reference.completed_at=tick(1300000000);state.reference.rx=tick(1250000000);state.reference.parent_tx_ns=1249999999;
 assert(rc5_clock_discipline(&backwards,&state,&fit,&rate));assert(!backwards.step_fences);
 assert(rc5_clock_capture(&backwards,&state,tick(1300000000),&stamp)&&stamp.ns==1300000000);
 assert(rc5_clock_capture(&backwards,&state,tick(1301000000),&stamp)&&stamp.ns>=1300000000);
 struct rc5_rate_fit invalid=fit;invalid.ppb=-1000000000;assert(!rc5_clock_discipline(&backwards,&state,&invalid,&rate));
 uint32_t generation=backwards.generation;state.parent_boot++;state.reference.completed_at=tick(1310000000);assert(rc5_clock_discipline(&backwards,&state,&fit,&rate));assert(backwards.generation==generation+1);state.parent_boot--;
 /* Functional reacquisition reports an unqualified discontinuity. */
 struct rc5_clock stepped=clock;state.reference.completed_at=tick(1310000000);state.reference.rx=tick(1250000000);state.reference.parent_tx_ns=1250010000;
 assert(rc5_clock_discipline(&stepped,&state,&fit,&rate));assert(stepped.step_fences==1&&stepped.count);
 assert(rc5_clock_capture(&stepped,&state,tick(1310000000),&stamp)&&stamp.quality==RC5_UNQUALIFIED);
 state.root_epoch++;assert(!rc5_clock_capture(&clock,&state,tick(1310000000),&stamp));state.root_epoch--;
 /* A new source envelope can authenticate a completed independent local TX
  * from its original packet without inheriting an outgoing root-event link. */
 struct rc5_state transition;rc5_init(&transition,&routes,1);assert(rc5_bind(&transition,&grant));
 p=packet(1);assert(!rc5_receive(&transition,&p,tick(100000000),1,5,tick(102000000)));
 p=packet(2);p.origin.issued=150000000;p.origin.expires=1800000000;assert(!rc5_origin_sign(p.proof,&p.origin,&routes,&group));p.count=1;p.correction[0]=(struct rc5_correction){.publication=1,.event=1,.channel=5,.generation=8,.tx_ns=100000000,.bound=RC5_UNKNOWN};
 assert(rc5_receive(&transition,&p,tick(162500000),2,5,tick(164500000)));assert(transition.reference.origin.issued==150000000&&transition.reference.parent_tx_ns==100000000);
 uint64_t until=transition.valid_until;assert(!rc5_receive(&transition,&p,tick(200000000),2,5,tick(202000000)));assert(transition.valid_until==until);
 /* A held RC's own TX remains usable after the old root envelope expires;
  * current authenticated parent context and event/replay association remain. */
 p=packet(3);assert(!rc5_receive(&transition,&p,tick(2000000000),3,5,tick(2002000000)));
 p=packet(4);p.count=1;p.correction[0]=(struct rc5_correction){.publication=3,.event=3,.channel=5,.generation=9,.age_us=2000000,.tx_ns=2000000000,.bound=RC5_UNKNOWN};
 assert(!rc5_encode(ad,&p,&routes,&group));
 assert(rc5_receive(&transition,&p,tick(2062500000),4,5,tick(2064500000)));
 struct rc5_clock after_expiry={0};
 assert(rc5_clock_discipline(&after_expiry,&transition,&fit,&rate));
 assert(rc5_clock_capture(&after_expiry,&transition,tick(2070000000),&stamp)&&stamp.state==RC5_HELD);
 /* A finite excessive uncertainty is still functional, explicitly
  * functional/unqualified and cannot satisfy a physical precision gate. */
 struct rc5_clock uncertain=clock;
 for(unsigned i=0;i<4;i++){uncertain.map[i].bound=1001;uncertain.map[i].capture_bound=0;uncertain.map[i].rate_bound=0;}
 assert(rc5_clock_capture(&uncertain,&state,tick(1320000000),&stamp)&&stamp.quality==RC5_UNQUALIFIED&&stamp.bound==1001);
 /* Positive and negative phase segments end after exactly one second even
  * without another parent packet. Base rate resumes; no50% overcorrection. */
 for(int sign=-1;sign<=1;sign+=2){
  struct rc5_state hold;rc5_init(&hold,&routes,1);assert(rc5_bind(&hold,&grant));
  struct rc5_clock held={0};struct rc5_rate_fit stable={.valid=true};
  hold.reference=(struct rc5_reference){.valid=true,.rx=tick(100000000),.completed_at=tick(200000000),.parent_tx_ns=100000000,.bound=RC5_UNKNOWN};
  assert(rc5_clock_discipline(&held,&hold,&stable,&rate));
  hold.reference.rx=tick(200000000);hold.reference.completed_at=tick(300000000);hold.reference.parent_tx_ns=200000000+sign*500;
  assert(rc5_clock_discipline(&held,&hold,&stable,&rate));
  assert(rc5_clock_capture(&held,&hold,tick(300000000),&stamp)&&stamp.ns==300000000);
  assert(rc5_clock_capture(&held,&hold,tick(800000000),&stamp)&&stamp.ns==(uint64_t)(800000000+sign*250));
  assert(rc5_clock_capture(&held,&hold,tick(1300000000),&stamp)&&stamp.ns==(uint64_t)(1300000000+sign*500));
  assert(rc5_clock_capture(&held,&hold,tick(1500000000),&stamp)&&stamp.ns==(uint64_t)(1500000000+sign*500));
  assert(rc5_clock_capture(&held,&hold,tick(1650000000),&stamp));
 }
 /* New field policy is local and independent of root admission/quality.
  * Historical unrestricted simulations below retain their explicit policy. */
 {
  struct rc5_state local;rc5_init(&local,&routes,1);assert(rc5_bind(&local,&grant));
  struct rc5_clock finite={.hold_ticks=8000000};struct rc5_rate_fit stable={.valid=true};
  local.reference=(struct rc5_reference){.valid=true,.rx=tick(100000000),.completed_at=tick(200000000),.parent_tx_ns=100000000,.bound=RC5_UNKNOWN};
  assert(rc5_clock_discipline(&finite,&local,&stable,&rate));
  assert(rc5_clock_capture(&finite,&local,tick(200000000),&stamp)&&!stamp.pll_tracking);
  local.reference.rx=tick(200000000);local.reference.completed_at=tick(300000000);local.reference.parent_tx_ns=200000000;
  assert(rc5_clock_discipline(&finite,&local,&stable,&rate));
  assert(rc5_clock_capture(&finite,&local,tick(799999000),&stamp)&&stamp.pll_tracking);
  assert(!rc5_clock_capture(&finite,&local,tick(800000000),&stamp)&&!stamp.pll_tracking);
  assert(!rc5_clock_capture(&finite,&local,tick(800001000),&stamp));
  /* A replayed discipline and fresh admission cannot extend the deadline. */
  assert(!rc5_clock_discipline(&finite,&local,&stable,&rate));
  struct rc5_admission renew=grant;renew.now_tick=tick(700000000);assert(rc5_bind(&local,&renew));
  assert(!rc5_clock_capture(&finite,&local,tick(800000000),&stamp));
  local.reference.rx=tick(900000000);local.reference.completed_at=tick(1000000000);local.reference.parent_tx_ns=900010000;
  assert(rc5_clock_discipline(&finite,&local,&stable,&rate));
  assert(rc5_clock_capture(&finite,&local,tick(1000000000),&stamp)&&!stamp.pll_tracking);
  local.reference.rx=tick(1000000000);local.reference.completed_at=tick(1100000000);local.reference.parent_tx_ns=1000010000;
  assert(rc5_clock_discipline(&finite,&local,&stable,&rate));
  assert(rc5_clock_capture(&finite,&local,tick(1100000000),&stamp)&&stamp.pll_tracking);
  /* A second RC has its own accepted-update deadline, not its parent's. */
  struct rc5_clock downstream={.hold_ticks=8000000};struct rc5_state child=local;
  child.reference.completed_at=tick(1400000000);child.reference.rx=tick(1300000000);child.reference.parent_tx_ns=1300010000;
  assert(rc5_clock_discipline(&downstream,&child,&stable,&rate));
  assert(!rc5_clock_capture(&finite,&local,tick(1600000000),&stamp));
  assert(rc5_clock_capture(&downstream,&child,tick(1600000000),&stamp));
  assert(!rc5_clock_capture(&downstream,&child,tick(1900000000),&stamp));
 }
 /* Profile3 functional holdover has no short age gate. Authentication expiry
  * remains distinct; old short signed permissions above remain short. */
 assert(RC5_SOURCE_NS==UINT64_C(360000000000));
 struct rc5_origin budget_origin=origin;budget_origin.expires=RC5_SOURCE_NS;
 uint8_t budget_proof[16];assert(!rc5_origin_sign(budget_proof,&budget_origin,&routes,&group));
 ++budget_origin.expires;assert(rc5_origin_sign(budget_proof,&budget_origin,&routes,&group));
 struct rc5_state age_state;rc5_init(&age_state,&routes,1);
 struct rc5_admission long_grant=grant;long_grant.root_expires_ns=RC5_SOURCE_NS;assert(rc5_bind(&age_state,&long_grant));
 struct rc5_clock age_clock={.count=1,.head=1,.root_boot=age_state.root_boot,.root_epoch=age_state.root_epoch,.parent_boot=age_state.parent_boot,.parent_epoch=age_state.parent_epoch};
 age_clock.map[0]=(struct rc5_map){.effective=tick(100000000),.anchor_tick=tick(100000000),.anchor_ns=100000000,.bound=RC5_UNKNOWN,.rate_bound=RC5_UNKNOWN};
 for(int rate_ppb=-300000;rate_ppb<=300000;rate_ppb+=300000){
  age_clock.map[0].ppb=rate_ppb;
  for(unsigned seconds=2;seconds<=7200;seconds+=17){
   uint64_t now=tick((uint64_t)seconds*1000000000);
   uint64_t nominal=(now-age_clock.map[0].anchor_tick)*125/2;
   uint64_t expected=(uint64_t)(((__uint128_t)nominal*1000000000)/(1000000000+(int64_t)rate_ppb))+100000000;
   assert(rc5_clock_capture(&age_clock,&age_state,now,&stamp));
   assert(stamp.ns==expected&&stamp.quality==RC5_UNQUALIFIED&&stamp.state==RC5_HELD);
  }
 }
 age_clock.map[0].ppb=0;age_clock.map[0].bound=0;age_clock.map[0].rate_bound=1000000000;
 assert(rc5_clock_capture(&age_clock,&age_state,tick(UINT64_C(359000000000)),&stamp)&&stamp.bound==RC5_UNKNOWN);
 assert(rc5_clock_capture(&age_clock,&age_state,age_state.context_until,&stamp));
 assert(rc5_clock_capture(&age_clock,&age_state,tick(UINT64_C(5000000000000)),&stamp)&&stamp.age_us==UINT32_MAX);
 /* Context expiry rejects new samples but does not stop the held local map. */
 assert(!rc5_receive(&age_state,&p,age_state.context_until,7,5,age_state.context_until));
 /* Ordered boot-scoped challenges support repeated five-minute renewals;
  * a consumed/replayed challenge and failed second client cannot shorten lease. */
 struct rc5_demand demand={0};
 for(unsigned i=0;i<3;i++){uint64_t now=(uint64_t)i*300000000;
  assert(rc5_demand_begin(&demand,81,91,100+i,now));
  assert(rc5_demand_accept(&demand,81,91,100+i,i+1,RC5_LEASE_US,now));
  assert(demand.deadline==now+RC5_LEASE_US);
  assert(!rc5_demand_begin(&demand,81,91,100+i,now+1));
 }
 uint64_t lease_end=demand.deadline;
 assert(rc5_demand_begin(&demand,81,92,103,600000010));
 assert(!rc5_demand_accept(&demand,81,92,104,1,1,600000011));assert(demand.deadline==lease_end);
 assert(rc5_demand_accept(&demand,81,92,103,1,1000000,600000012));assert(demand.deadline==lease_end);
 assert(rc5_demand_live(&demand,lease_end-1)&&!rc5_demand_live(&demand,lease_end));
 uint32_t upstream=rc5_upstream_lease(RC5_LEASE_US);
 assert((uint64_t)upstream*1000300<=(uint64_t)(RC5_LEASE_US-220100)*999700);
 /* Relative capture residual diagnostic: constant offset/rate are not RMS. */
 struct rc5_rate_fit metric={.count=4,.head=4,.valid=true,.ppb=1000};
 for(unsigned i=0;i<4;i++){metric.ns[i]=UINT64_C(100000000000)+i*UINT64_C(1000000000);metric.rx[i]=UINT64_C(200000000000)+i*UINT64_C(16000016);}
 uint32_t rms;assert(rc5_rate_rms(&metric,&rms)&&rms==0);
 metric.rx[1]+=16;metric.rx[2]-=16;assert(rc5_rate_rms(&metric,&rms)&&rms==707);
 metric.valid=false;assert(!rc5_rate_rms(&metric,&rms)&&rms==RC5_UNKNOWN);metric.valid=true;
 metric.count=3;assert(!rc5_rate_rms(&metric,&rms));metric.count=4;
 metric.rx[3]=UINT64_MAX;assert(!rc5_rate_rms(&metric,&rms));
 puts("RC5_CONTRACT_PASS own_tx_map_snapshot held_unqualified source_epoch step_fence replay envelope_independence");return 0;
}
