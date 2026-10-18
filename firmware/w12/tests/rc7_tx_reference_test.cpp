// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc7_relay_estimate.hpp"
#include "rc7_tx_estimate.hpp"
#include "rc7_client.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
static const r2_group_config Group={{1,2,3},7,9};
static rc7_path root(){rc7_path p{};p.source_epoch=9;p.node[0]={11,22,1,1,0,0};return p;}
static const rc5_origin Origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
static G1ClockPrediction model(){return {20000000,1000000000,0,0,Origin.utc_offset};}
// Full publisher/codec/matched receiver path. The synthetic receiver records
// ADDRESS40us after programmed packet start. No RF delay or UTC claim follows.
static void path(uint64_t self,int32_t legacy_total=0,unsigned parent_depth=0){
  auto parent=root();if(parent_depth){parent.depth=1;parent.node[1]={50,60,1,1,1,1};}
  Rc7ProvisionalPath profile{legacy_total?1u:0u,self,11,22,9,legacy_total};
  Rc7RelayEstimate publisher;const auto m=model();
  assert(publisher.input(parent,Origin,nullptr,m,20000000,1,20000100,0,self,200,Group,profile,true));
  uint8_t a[190];assert(publisher.packet(a,20400000,Group));
  assert(publisher.selected(a,1,12,20400000,20400800,Group));
  const auto cap=publisher.capability(self,200,7,true);Rc7Client client;
  assert(client.make_request(cap,300,400,5,1020040,false));rc7_control response{};
  assert(publisher.response(response,client.request,8,20400800));assert(client.bind(response,1020040,20400800));
  uint8_t b[190];assert(publisher.packet(b,21600000,Group));rc7_packet packet{};
  assert(!rc7_decode(&packet,b,190,&publisher.path,&Group));
  periodic_rx::Stamp stamp{};stamp.actual=stamp.association_observed=true;stamp.epoch=1;stamp.sequence=1;
  stamp.tick20=stamp.available20=21600800;stamp.map_enclosure_ns=100;stamp.event=2;stamp.channel=13;
  assert(!client.receive(packet,stamp,stamp.available20));
  assert(publisher.selected(b,2,13,21600000,21600800,Group));
  const int32_t total=legacy_total&&parent_depth==0?legacy_total:40000;
  assert(publisher.sent[0].tx_ns==uint64_t(1080000000+int64_t(total)));
  uint8_t c[190];assert(publisher.packet(c,22800000,Group));assert(!rc7_decode(&packet,c,190,&publisher.path,&Group));
  stamp.sequence=2;stamp.tick20=stamp.available20=22800800;stamp.event=3;stamp.channel=14;
  assert(client.receive(packet,stamp,stamp.available20));const auto& state=client.receiver.clock;
  rc5_clock clock{};rc5_rate_fit fit{};fit.valid=true;rc5_rate rate{};
  rate.uncertainty_ppb=rate.capture_ns=rate.link_bound=rate.bias_bound_ns=UINT32_MAX;
  assert(rc5_clock_discipline(&clock,&state,&fit,&rate));rc5_stamp out{};
  // PPS is1.2s,119960us beyond the original1.080040s ADDRESS.
  assert(rc5_clock_capture(&clock,&state,state.reference.rx+1919360,&out));
  assert(int64_t(out.ns)-1200000000==total-40000);
  assert(out.bound==UINT32_MAX&&out.quality==RC5_UNQUALIFIED);
}
int main(){
  // Identity, alias and GNSS configuration never select a different conversion.
  for(uint64_t self:{100u,101u,102u,103u})for(int32_t offset:{0,47000,-47000,40000})path(self,offset);
  path(104,47000,1); // RC2 normalizes its own TX, never re-applies the RC1 profile.
#ifndef REFERENCE_LEGACY_NEGATIVE
  Rc7TxReference::Projection p;
  for(double rate:{-50000.0,-10000.0,0.0,10000.0,50000.0})for(int64_t phase:{-50000,-1000,0,1000,50000}){
    auto m=model();m.rate=rate;m.phase=phase;
    assert(Rc7TxReference::project(m,21600000,21600800,1,false,0,p));
    assert(p.address20==21600800&&p.address_ns==m.relative_at(21600800)&&p.emitted_ns==p.address_ns);
    assert(p.base_ns==p.address_ns-p.raw_ns&&p.total_ns==p.base_ns&&!p.residual_ns);
    for(int32_t total:{-100000,-47000,40000,47000,100000}){
      assert(Rc7TxReference::project(m,21600000,21600800,1,true,total,p));
      assert(p.emitted_ns==m.relative_at(21600000)+total&&p.residual_ns==total-p.base_ns&&p.total_ns==total);
    }
  }
  for(uint8_t phy:{0,2,3,4,255})assert(!Rc7TxReference::project(model(),21600000,21600800,phy,false,0,p));
  assert(!Rc7TxReference::project(model(),21600000,21600799,1,false,0,p));
  assert(!Rc7TxReference::project(model(),UINT64_MAX-799,UINT64_MAX,1,false,0,p));
  auto top=model();top.epoch=INT64_MAX-40000;
  assert(Rc7TxReference::project(top,top.tick,top.tick+800,1,false,0,p)&&p.emitted_ns==INT64_MAX);
  assert(!Rc7TxReference::project(top,top.tick,top.tick+800,1,true,47000,p));
  top.epoch=INT64_MAX-39999;assert(!Rc7TxReference::project(top,top.tick,top.tick+800,1,false,0,p));
  auto low=model();low.epoch=47000;
  assert(!Rc7TxReference::project(low,low.tick,low.tick+800,1,true,-47000,p));
  // The28-bit controller clock wraps independently of the unsigned64 local
  // clock. Cover both rollover directions, signed64 crossing and overflow.
  uint64_t tick=0;tx_estimate::ClockAnchor a{20000000,20000000,1,0};
  assert(tx_estimate::programmed_tick(a,tx_estimate::ClockMask,19995000,tick)&&tick==19987500);
  a={20000000,20000000,0,0};assert(tx_estimate::programmed_tick(a,tx_estimate::ClockMask,20000000,tick)&&tick==19993750);
  a={UINT64_C(1)<<63,UINT64_C(1)<<63,10,0};assert(tx_estimate::programmed_tick(a,10,a.before,tick)&&tick==a.before);
  a={UINT64_MAX-100,UINT64_MAX-100,10,0};assert(!tx_estimate::programmed_tick(a,11,UINT64_MAX-101,tick));
  a={100,100,10,0};assert(!tx_estimate::programmed_tick(a,9,99,tick));
  // Future ADDRESS, unknown PHY, source envelope and500ms age remain refusals.
  for(unsigned which=0;which<5;which++){
    Rc7RelayEstimate r;auto o=Origin;auto m=model();
    if(which==2)o.expires=1020020000;
    assert(r.input(root(),o,nullptr,m,20000000,1,20000100,0,100,200,Group));uint8_t wire[190];
    const auto raw=which>=3?29999200u:20400000u;
    assert(r.packet(wire,raw,Group));
    assert(!r.selected(wire,1,12,raw,raw+(which==0?799:800),Group,which==1?2:1));
    assert(!r.sent[0].publication);
  }
#endif
  std::puts("PASS shared W12 nominal1M ADDRESS reference: four identities, default/legacy totals, RC2, physical-address receiver mapping, slope, source/age/PHY refusal, wrap/overflow; RF UNKNOWN");
}
