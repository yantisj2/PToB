#pragma once
// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "native_r2.hpp"
#include "g1_gps_shadow.hpp"
namespace v12 {
// Read-only main-task witness of the production G1 actuator. No UTC authority,
// RF/clock writes, source expiry renewal, GNSS fitting or receiver commands.
class G1PpsDiagnostic {
 G1GpsShadow history_;
 public:
 void observe(const R2Status& s,uint64_t before20,uint64_t seen20,uint64_t observed_us){
  const auto deadline20=[&](uint64_t until){return until>observed_us&&until-observed_us<(UINT64_MAX-before20)/20?before20+(until-observed_us)*20:before20;};
  G1GpsShadow::Map m;m.clock=s.g1_clock_model;m.root=s.g1_clock_origin;m.boot=s.root_boot;
  m.source=s.source_epoch;m.train=s.publication;m.scope=s.g1_relative_quality.scope_generation;
  m.valid=r2_clock_witness(s)&&s.following&&s.root_identity==m.root&&m.boot&&m.source&&m.train&&m.scope&&
   s.g1_anchor_deadline_us>observed_us&&s.g1_corrected_until_us>observed_us&&s.service_until_us>observed_us;
  m.following=s.following;m.permission=deadline20(s.g1_corrected_until_us);
  m.until=std::min({deadline20(s.g1_anchor_deadline_us),m.permission,deadline20(s.service_until_us)});
  history_.observe(m,before20,seen20,s.g1_anchor_deadline_us,std::min(s.g1_corrected_until_us,s.service_until_us));
 }
 G1GpsShadow::Score edge(uint64_t edge20,uint32_t number,uint32_t enclosure,bool mapped,const G1GpsShadow::Reference& ref){return history_.edge(edge20,number,enclosure,mapped,ref);}
 G1GpsShadow::View view()const{return history_.view();}
 static int32_t phase(int64_t ns){
  int64_t p=ns%INT64_C(1000000000);if(p>=500000000)p-=1000000000;if(p< -500000000)p+=1000000000;
  return int32_t(p);
 }
};
static_assert(sizeof(G1PpsDiagnostic)<=G1GpsShadowRamBudget,"Fixed diagnostic memory");
}
