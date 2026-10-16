#include "r2_discipline.hpp"
#include "native_dashboard.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace v12;
void capture_model(){
  uint8_t pdu[159]{};pdu[1]=157;pdu[2]=pdu[3]=pdu[4]=255;unsigned offset=0;
  assert(periodic_rx::payload(pdu,sizeof(pdu),offset)&&offset==1);
  assert(periodic_rx::descriptor_payload(0,pdu+1,159,offset)&&offset==0);
  uint8_t em[161]{};em[0]=8;std::memcpy(em+3,pdu+1,158);
  assert(periodic_rx::descriptor_payload(3,em,162,offset)&&offset==3);
  em[0]=0x10;assert(!periodic_rx::descriptor_payload(4,em,163,offset));
  assert(!periodic_rx::descriptor_payload(0x40,pdu+1,159,offset));
  assert(!periodic_rx::descriptor_payload(0,pdu+1,158,offset));
  pdu[0]=0x40;assert(!periodic_rx::payload(pdu,sizeof(pdu),offset));pdu[0]=0;
  uint8_t chained[163]{};chained[0]=4;chained[1]=0x10;assert(!periodic_rx::payload(chained,sizeof(chained),offset));
  periodic_rx::Record rows[2]{};auto& r=rows[0];std::memcpy(r.bytes,pdu+1,158);
  r.anchor={20000000,20000002,{10000,300}};r.stamp.epoch=1;r.stamp.sequence=1;r.stamp.coarse=10000;r.stamp.fine=324;
  r.stamp.actual=r.stable=r.single=true;r.observed_us=1000000;
  r.stamp.event=65535;r.stamp.channel=36;r.stamp.association_observed=true;
  periodic_rx::Stamp result;uint32_t seq=0;bool ambiguous=false;
  assert(!periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  r.stamp.radio_reference_valid=true;r.stamp.phy=0; // Explicit synthetic1M reference; missing stays refused.
  assert(periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  assert(result.tick20==20000001&&seq==1&&!result.event_valid&&!result.channel_valid&&!result.edge_calibrated);
  assert(result.association_observed&&result.event==65535&&result.channel==36);
  const auto raw_tick=result.tick20;const auto raw_enclosure=result.map_enclosure_ns;
  r.stamp.refinement_ticks=-7;r.stamp.refinement_applied=true;
  assert(periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  assert(result.refinement_applied&&result.tick20==raw_tick-7&&result.map_enclosure_ns==raw_enclosure+350);
  assert(!result.event_valid&&!result.channel_valid&&!result.edge_calibrated);
  r.stamp.refinement_ticks=11;
  assert(periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  assert(!result.refinement_applied&&result.tick20==raw_tick);
  r.stamp.refinement_ticks=7;
  assert(periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,raw_tick,result,seq,ambiguous));
  assert(!result.refinement_applied&&result.tick20==raw_tick);
  r.stamp.refinement_ticks=0;r.stamp.refinement_applied=false;
  assert(!periodic_rx::select(rows,1,8,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  assert(!periodic_rx::select(rows,1,9,9,2,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  assert(!periodic_rx::select(rows,1,9,9,1,r.bytes,158,1250001,25000200,result,seq,ambiguous));
  rows[1]=r;rows[1].stamp.sequence=2;assert(!periodic_rx::select(rows,2,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous)&&ambiguous);
  r.stable=false;assert(!periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  uint8_t gr[195]{};gr[0]=4;gr[1]=0x48;gr[5]=189;gr[6]=gr[7]=gr[8]=255;
  assert(periodic_rx::payload(gr,sizeof(gr),offset)&&offset==5);
  assert(periodic_rx::descriptor_payload(4,gr+1,195,offset)&&offset==4);
  r.length=190;r.stable=true;std::memcpy(r.bytes,gr+5,190);
  assert(periodic_rx::select(rows,1,9,9,1,r.bytes,190,1000010,20000200,result,seq,ambiguous));
  assert(!periodic_rx::select(rows,1,9,9,1,r.bytes,158,1000010,20000200,result,seq,ambiguous));
  gr[1]|=0x10;assert(!periodic_rx::payload(gr,sizeof(gr),offset));
}
void corrected_servo(){
  // Synthetic source/capture witness at the production assembler boundary.
  // A +10ppm raw20MHz crystal must learn a -10ppm rate correction. This is
  // an algorithm test, never RF/PPS qualification or a driver calibration.
  static R2Discipline d;d.scope({1,11,200000000,1,1,1,0,true});
  constexpr int64_t utc_offset=INT64_C(1791586531000000000);
  constexpr uint64_t step16=1000000,step20=1250000;
  uint64_t last_available=0;
  for(unsigned i=1;i<=2000;i++){
    r2_precision_packet p{};p.boot=11;p.source_epoch=p.train=p.capture_epoch=1;p.root=0;p.publication=i;p.flags=1;
    const uint64_t remote=UINT64_C(16000000)+uint64_t(i)*step16;
    p.created_ns=remote*125/2-1000;p.expires_ns=p.created_ns+1500000000;p.utc_offset_ns=utc_offset;
    if(i>2){p.count=2;for(unsigned k=0;k<2;k++){const unsigned before=i-2+k;
      const uint64_t prior=UINT64_C(16000000)+uint64_t(before)*step16;
      p.correction[k]={before,uint16_t(before),uint8_t(before%37),prior,1,prior*125/2+1000000000};}}
    periodic_rx::Stamp c;c.actual=c.event_valid=c.channel_valid=c.edge_calibrated=true;c.epoch=1;c.event=i;c.channel=i%37;c.map_enclosure_ns=100;
    c.tick20=UINT64_C(20000000)+uint64_t(i)*step20+uint64_t(i)*step20/100000;
    c.available20=c.tick20+100000;last_available=c.available20;
    d.authenticated_receive(p,c,c.available20,uint64_t(i)*62500+1000000,utc_offset+int64_t(p.created_ns));
  }
  auto v=d.view(last_available,126000000);
  assert(v.fit&&v.accepted>600&&v.rms_ns<100);
  assert(v.trim_mppb>-10051000&&v.trim_mppb<-9949000);
  assert(v.available20<=last_available&&v.prediction.tick<v.available20);
  uint64_t delivered=0;assert(d.delivered(v,last_available,delivered));
  assert(delivered==uint64_t(v.prediction.at(last_available))/1000);
  assert(!d.delivered(d.view(last_available,200000000),last_available,delivered));
  const auto old=v.accepted;
  r2_relay_packet gr{};gr.origin={0,1,11,1,1000000,1651000000,1000000,0,0,0};gr.route={0,0,0,0,11,1,2,1,1,UINT32_MAX};
  periodic_rx::Stamp unknown;unknown.actual=true;
  assert(!d.authenticated_gr3(gr,unknown,last_available+1,126000001,1000000));
  assert(d.view(last_available+1,126000001).accepted==old);

  r2_precision_packet invalid{};periodic_rx::Stamp callback_only{};
  assert(!d.authenticated_receive(invalid,callback_only,last_available+1,126000001,utc_offset));
  assert(d.view(last_available+1,126000001).accepted==old);
  // Intentional radio quiet and a replacement receiver incarnation retain
  // estimator history only when the same local20MHz clock is continuous.
  d.scope({1,11,200000000,1,1,2,0,false,true});
  assert(!d.view(last_available+1,126000001).fit);
  d.scope({1,11,200000000,1,1,3,0,true,true});
  assert(d.view(last_available+1,126000001).fit);
  d.scope({1,11,300000000,1,1,3,0,true,true});
  assert(!d.view(last_available+40000000,128000000).fit);
  // Fresh direct bootstrap permission cannot revive the old corrected model.
  assert(!d.delivered(d.view(last_available+40000000,128000000),last_available+40000000,delivered));
  // New GM or capture incarnation withdraws the old fitted/holdover model.
  d.scope({2,12,200000000,1,1,2,1,true});v=d.view(last_available+1,126000001);
  assert(!v.fit&&v.rms_ns==UINT32_MAX&&!v.hold_ms&&!v.available20);
  assert(!d.delivered(v,last_available+1,delivered));
  std::printf("PASS corrected16Hz raw20MHz servo: accepted=%u; no callback/GPS training; full-scope reset\n",old);
}
void ui_gate(){
  R2Dashboard dashboard;R2Status mesh;DisplayState state;
  mesh.requested=mesh.clock_valid=mesh.following=true;mesh.root_identity=UINT64_C(0x0000000000000001);mesh.root_boot=mesh.source_epoch=mesh.publication=1;
  mesh.model_available20=100;mesh.rms_ns=321;mesh.precision_fit=true;
  dashboard.apply(state,mesh);assert(state.rms_ns==321&&!state.hold_1us_ms);
  mesh.hold_ms=1;mesh.hold_samples=99;dashboard.apply(state,mesh);assert(!state.hold_1us_ms);
  mesh.hold_samples=100;dashboard.apply(state,mesh);assert(state.hold_1us_ms==1&&state.hold_percentile==99);
  mesh.model_available20=0;dashboard.apply(state,mesh);assert(state.rms_ns==UINT32_MAX&&!state.hold_1us_ms);
  mesh.model_available20=20000000;mesh.precision_model={20000000,INT64_C(1791586531000000321),0,0,0};
  auto score=r2_shadow_pps(mesh,40000000,40001000,true);assert(score.valid&&score.modulo_ns==321);
  mesh.model_available20=40000001;assert(!r2_shadow_pps(mesh,40000000,40001000,true).valid);
  mesh.model_available20=20000000;assert(!r2_shadow_pps(mesh,40000000,40001000,false).valid);
  mesh.following=false;assert(!r2_shadow_pps(mesh,40000000,40001000,true).valid);
  dashboard.apply(state,mesh);assert(!std::strcmp(state.followed_clock,"NONE")&&!std::strcmp(state.role,"HOLD")&&state.valid&&!state.fit&&state.rms_ns==UINT32_MAX&&!state.hold_1us_ms);
  mesh.root_identity=mesh.root_boot=0;mesh.source_epoch=mesh.publication=0;
  mesh.g1_clock_origin=UINT64_C(0x0000000000000001);
  dashboard.apply(state,mesh);assert(!std::strcmp(state.followed_clock,"TC-01")&&!std::strcmp(state.role,"HOLD")&&state.valid&&!state.fit);
  mesh.g1_clock_origin=UINT64_C(0x0000000000000003);assert(dashboard.needs_authority_update(mesh));
  dashboard.apply(state,mesh);assert(!std::strcmp(state.followed_clock,"TC-03")&&!mesh.following&&!mesh.root_identity);
  mesh.following=true;mesh.root_identity=UINT64_C(0x0000000000000003);
  mesh.root_boot=mesh.source_epoch=mesh.publication=1;
  dashboard.apply(state,mesh);assert(!std::strcmp(state.followed_clock,"TC-03"));
  mesh.root_identity=UINT64_C(0x0000000000000002);
  dashboard.apply(state,mesh);assert(!std::strcmp(state.followed_clock,"TC-02"));
  mesh.clock_valid=false;dashboard.apply(state,mesh);assert(!state.valid&&!std::strcmp(state.followed_clock,"NONE"));
  assert(!std::strcmp(state.role,"SEARCH"));
  mesh.connecting=true;dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"JOIN"));
  mesh.connecting=false;mesh.group_member=true;dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"ACQUIRE"));
  mesh.fault=true;dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"FAULT")&&!state.valid);
  mesh.fault=false;mesh.clock_valid=mesh.following=true;mesh.root_identity=UINT64_C(0x0000000000000001);mesh.root_boot=mesh.source_epoch=mesh.publication=1;
  mesh.precision_fit=false;mesh.model_available20=0;mesh.g1_clock_model={40000000,2000000000,0,2000,0};
  mesh.g1_relative_quality.original20=38000000;mesh.g1_relative_quality.evaluated20=40000000;
  mesh.g1_relative_quality.samples=16;mesh.g1_relative_quality.rms_ns=100;mesh.g1_relative_quality.max_ns=200;mesh.g1_relative_quality.rate_guard_ppb=20;
  mesh.follower_snapshot_us=2000000;mesh.g1_anchor_deadline_us=4000000;
  dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"FOLLOW")&&!state.r2_relative_lock&&state.hold_estimated&&!state.hold_1us_ms);
  mesh.g1_relative_quality.evaluated20+=40000000;
  dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"LOCK_EST")&&state.r2_relative_lock&&state.hold_estimated);
  assert(!state.fit&&state.precision_ns==UINT32_MAX&&state.relative_prediction_ns<1000&&state.hold_1us_ms<=2000&&!state.hold_trials&&!state.hold_percentile);
  mesh.g1_relative_quality.evaluated20=0;dashboard.apply(state,mesh);assert(!state.hold_estimated&&!state.r2_relative_lock);
  mesh.g1_relative_quality.evaluated20=80000000;
  dashboard.apply(state,mesh);mesh.g1_relative_quality.evaluated20=120000000;dashboard.apply(state,mesh);
  mesh.following=false;mesh.root_identity=mesh.root_boot=mesh.source_epoch=mesh.publication=0;mesh.g1_clock_origin=UINT64_C(0x0000000000000001);
  dashboard.apply(state,mesh);assert(!std::strcmp(state.role,"HOLD")&&state.r2_relative_lock&&state.hold_estimated&&state.hold_1us_ms);
  mesh.g1_relative_quality.samples=15;dashboard.apply(state,mesh);assert(!state.hold_estimated&&!state.r2_relative_lock&&!state.hold_1us_ms);
  mesh.clock_valid=false;dashboard.apply(state,mesh);assert(!state.valid&&!state.hold_estimated&&state.relative_prediction_ns==UINT32_MAX);
}
int main(){
  {v12::R2Dashboard d;v12::R2Status s;DisplayState out;
    assert(d.needs_authority_update(s));d.apply(out,s);assert(!d.needs_authority_update(s));
    s.requested=s.clock_valid=s.following=true;s.root_identity=UINT64_C(0x0000000000000001);s.root_boot=1;s.source_epoch=s.publication=1;
    assert(d.needs_authority_update(s));d.apply(out,s);assert(out.valid&&!std::strcmp(out.followed_clock,"TC-01"));
    s.following=false;s.root_identity=s.root_boot=0;s.source_epoch=s.publication=0;
    assert(d.needs_authority_update(s));d.apply(out,s);assert(out.valid&&!std::strcmp(out.role,"HOLD")&&!std::strcmp(out.followed_clock,"NONE"));
    s.fault=true;assert(d.needs_authority_update(s));d.apply(out,s);assert(!out.valid&&!std::strcmp(out.role,"FAULT")&&!std::strcmp(out.followed_clock,"NONE"));
  }
capture_model();corrected_servo();ui_gate();std::puts("PASS physical association refusal and RMS/P99 fresh population gates");}
