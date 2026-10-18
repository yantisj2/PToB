// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc7_relay_estimate.hpp"
#include "rc7_client.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
static const r2_group_config Group={{1,2,3},7,9};
static rc7_path root(){rc7_path p{};p.source_epoch=9;p.node[0]={11,22,1,1,0,0};return p;}
static const rc5_origin Origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
static G1ClockPrediction model(){return {20000100,1000005000-46940,0,0,Origin.utc_offset};}
static Rc7ProvisionalPath profile(int32_t offset=47000){return {1,100,11,22,9,offset};}
static int64_t phase(uint64_t n){const auto f=n%1000000000;return f>=500000000?int64_t(f)-1000000000:int64_t(f);}
static int64_t receive_phase(const Rc7ProvisionalPath& p,bool reference=true,uint32_t actual_epoch=9){
 Rc7RelayEstimate relay;const auto m=model();auto admitted_origin=Origin;admitted_origin.epoch=actual_epoch;auto admitted_path=root();admitted_path.source_epoch=actual_epoch;
 assert(relay.input(admitted_path,admitted_origin,nullptr,m,20000000,1,20000100,0,100,200,Group,p,reference));
 uint8_t a[190];assert(relay.packet(a,20400000,Group));
 assert(relay.selected(a,1,12,20400000,20400000,Group));
 auto cap=relay.capability(100,200,7,true);Rc7Client client;
 assert(client.make_request(cap,300,400,5,1020000,false));rc7_control response{};
 assert(relay.response(response,client.request,8,20400000));assert(client.bind(response,1020000,20400000));
 uint8_t b[190];assert(relay.packet(b,21600000,Group));rc7_packet packet{};assert(!rc7_decode(&packet,b,190,&relay.path,&Group));
 periodic_rx::Stamp stamp{};stamp.actual=stamp.association_observed=true;stamp.epoch=1;stamp.sequence=1;
 stamp.tick20=stamp.available20=21600000;stamp.map_enclosure_ns=100;stamp.event=2;stamp.channel=13;
 assert(!client.receive(packet,stamp,stamp.available20));assert(relay.selected(b,2,13,21600000,21600000,Group));
 const auto* frozen=relay.selected_snapshot();assert(frozen&&frozen->map.epoch==m.epoch);
 assert(frozen->map.output_correction_ns==0&&frozen->map.relative_at(21600000)==1080000000-46940);
 const auto applied=p.select(100,0,11,22,9,reference);
 assert(relay.sent[0].tx_ns==uint64_t(1080000000-46940+applied)&&relay.sent[0].bound==UINT32_MAX);
 uint8_t c[190];assert(relay.packet(c,22800000,Group));assert(!rc7_decode(&packet,c,190,&relay.path,&Group));
 stamp.sequence=2;stamp.tick20=stamp.available20=22800000;stamp.event=3;stamp.channel=14;
 const auto* completed=client.receive(packet,stamp,stamp.available20);assert(completed&&completed->stamp.event==2);
 const auto& state=client.receiver.clock;assert(state.reference.parent_tx_ns==relay.sent[0].tx_ns);
 // Execute the production receiver clock mapping at a synthetic PPS 200ms
 // after the matched ADDRESS. This tests sign/once-only arithmetic, not RF.
 rc5_clock clock{};rc5_rate_fit fit{};fit.valid=true;fit.ppb=0;
 rc5_rate rate{};rate.uncertainty_ppb=rate.capture_ns=rate.link_bound=rate.bias_bound_ns=UINT32_MAX;
 assert(rc5_clock_discipline(&clock,&state,&fit,&rate));rc5_stamp out{};
 const auto edge=state.reference.rx+3200000;assert(rc5_clock_capture(&clock,&state,edge,&out));
 assert(out.bound==UINT32_MAX&&out.quality==RC5_UNQUALIFIED);
 // Synthetic PPS lies at 1.280000000s; phase around that known reference.
 assert(m.epoch==model().epoch&&relay.map.epoch==m.epoch);
 return phase(out.ns-280000000);
}
int main(){
 assert(receive_phase({})==-46940); // Zero default retains byte-level estimate.
 assert(receive_phase(profile())==60); // +47000 advances a negative shadow phase.
 assert(receive_phase(profile(-47000))==-93940); // Wrong sign doubles bias.
 assert(receive_phase(profile(),false)==-46940);
 for(unsigned gate=0;gate<7;gate++){
  auto p=profile();switch(gate){case 0:p.id=0;break;case 1:p.publisher=101;break;case 2:p.root=12;break;case 3:p.root_boot=23;break;case 4:p.source_epoch=10;break;case 5:p.offset_ns=100001;break;case 6:p.offset_ns=-100001;break;}
  assert(receive_phase(p)==-46940);
 }
 auto transfer=profile();transfer.source_epoch=2617;transfer.allow_source_epoch_transfer=true;
 assert(receive_phase(transfer,true,2618)==60); // Explicitly unmeasured transfer after fresh source admission.
 transfer.allow_source_epoch_transfer=false;assert(receive_phase(transfer,true,2618)==-46940);
 transfer.allow_source_epoch_transfer=true;transfer.root_boot=23;assert(receive_phase(transfer,true,2618)==-46940);
 // An offset profile never authenticates or repairs mismatched old/new source scopes.
 {Rc7RelayEstimate x;auto old=root();old.source_epoch=2617;auto fresh=Origin;fresh.epoch=2618;
  assert(!x.input(old,fresh,nullptr,model(),20000000,1,20000100,0,100,200,Group,transfer,true));
  auto current=old;current.source_epoch=2618;auto stale=Origin;stale.epoch=2617;
  assert(!x.input(current,stale,nullptr,model(),20000000,1,20000100,0,100,200,Group,transfer,true));}
 assert(profile().select(100,1,11,22,9,true)==0); // RC2 cannot apply the RC1 profile again.
 int64_t output=0;assert(!Rc7ProvisionalPath::apply(INT64_MAX,47000,output));
 assert(!Rc7ProvisionalPath::apply(46940,-47000,output));assert(!Rc7ProvisionalPath::apply(0,47000,output));
 assert(Rc7ProvisionalPath::apply(1,0,output)&&output==1);
 // A prepared map owns its immutable applied profile even if a newer map
 // disables that profile. No callback uses the current profile for old bytes.
 Rc7RelayEstimate relay;auto m=model();assert(relay.input(root(),Origin,nullptr,m,20000000,1,20000100,0,100,200,Group,profile(),true));
 uint8_t old[190];assert(relay.packet(old,20400000,Group));m.tick=20500100;m.epoch+=25000000;
 assert(relay.input(root(),Origin,nullptr,m,20500000,1,20500100,0,100,200,Group,{},false));
 assert(relay.selected(old,1,12,20400000,20500100,Group));assert(relay.sent[0].tx_ns==1020000060);
 assert(relay.selected_snapshot()->provisional_offset_ns==47000);
 uint8_t recent[190];assert(relay.packet(recent,20800000,Group));assert(relay.selected(recent,2,12,20800000,20800000,Group));
 assert(relay.selected_snapshot()->provisional_offset_ns==0);
 // Adding an offset cannot cross the original upper envelope, while a
 // negative offset cannot make an expired raw projection live again.
 for(int sign:{1,-1}){
  Rc7RelayEstimate x;auto o=Origin;auto n=model();n.epoch=sign>0?int64_t(o.expires)-10000:int64_t(o.expires)+10000;
  assert(x.input(root(),o,nullptr,n,20000000,1,20000100,0,100,200,Group,profile(sign*47000),true));
  uint8_t wire[190];assert(x.packet(wire,20000100,Group));
  assert(!x.selected(wire,1,12,20000100,20000100,Group));assert(!x.sent[0].publication);
 }
 assert(!relay.live(33000000));assert(relay.withdraw_time());assert(!relay.ready(33000000));
 static_assert(RC_DYNAMIC_RENEW_US==300000000&&RC_DYNAMIC_LEASE_US==360000000);
 std::puts("PASS production RC7 emitted offset -> codec -> matched ADDRESS -> receiver map: default, positive, wrong sign, scope, once-only frozen map, original expiry, UNKNOWN precision");
}
