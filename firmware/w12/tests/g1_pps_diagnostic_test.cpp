// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "g1_pps_diagnostic.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace v12;
namespace {
R2Status current(){
 R2Status s{};s.requested=s.clock_valid=s.following=true;
 s.g1_clock_origin=s.root_identity=11;s.root_boot=22;s.source_epoch=s.publication=1;
 s.g1_anchor_deadline_us=s.service_until_us=s.g1_corrected_until_us=10000000;
 s.g1_clock_model={20000000,1000000250,0,0,INT64_C(1700000000000000000),0};
 s.g1_relative_quality.scope_generation=1;return s;
}
G1GpsShadow::Reference reference(uint64_t edge){return {edge-10000000,1700000000,1,true};}
void observe(G1PpsDiagnostic& d,const R2Status& s){d.observe(s,20001000,20002000,1000100);}
}
int main(){
 constexpr uint64_t edge=40000000;auto s=current();
 assert(!s.precision_fit&&!s.precision_model.tick);
 G1PpsDiagnostic d;observe(d,s);auto score=d.edge(edge,1,2500,true,reference(edge));
 assert(score.map_valid&&score.row.permission&&score.row.following&&score.row.reference&&score.row.phase_ns==250&&score.utc_ns>0);
 assert(score.map.root==11&&score.map.boot==22&&score.map.source==1&&score.map.train==1&&score.map.scope==1&&score.row.enclosure_ns==2500);
 assert(d.view().samples==1);
 // Missing/future/stale empirical GNSS association never enters the reference
 // population. The read-only production model can still be inspected locally.
 G1GpsShadow::Reference no{};auto missing=d.edge(60000000,2,2500,true,no);
 assert(missing.map_valid&&!missing.row.reference&&missing.row.reason==G1GpsShadow::NoFix);
 assert(d.view().samples==1&&d.view().invalid_reference==1);
 for(auto ref:{G1GpsShadow::Reference{edge+1,1,1,true},G1GpsShadow::Reference{edge-30000000,1,1,true},G1GpsShadow::Reference{edge-1,1,0,true}}){
  G1PpsDiagnostic n;observe(n,s);assert(!n.edge(edge,1,100,true,ref).row.reference&&n.view().samples==0);
 }
 for(unsigned k=0;k<15;k++){
  auto bad=current();
  if(k==0)bad.requested=false;if(k==1)bad.fault=true;if(k==2)bad.clock_valid=false;
  if(k==3)bad.g1_retained_estimate=true;if(k==4)bad.following=false;if(k==5)bad.root_identity=12;
  if(k==6)bad.root_boot=0;if(k==7)bad.source_epoch=0;if(k==8)bad.publication=0;
  if(k==9)bad.g1_anchor_deadline_us=1000100;if(k==10)bad.g1_corrected_until_us=1000100;
  if(k==11)bad.service_until_us=1000100;if(k==12)bad.g1_clock_model.tick=0;
  if(k==13)bad.g1_clock_origin=0;if(k==14)bad.g1_relative_quality.scope_generation=0;
  G1PpsDiagnostic n;observe(n,bad);assert(!n.edge(edge,1,2500,true,reference(edge)).map_valid);
 }
 G1PpsDiagnostic causal;observe(causal,s);auto later=s;
 later.g1_clock_model={80000000,4000000900,0,0,INT64_C(1700000000000000000),0};
 causal.observe(later,80001000,80002000,4000100);
 auto prior=causal.edge(60000000,1,100,true,reference(60000000));
 assert(prior.map_valid&&prior.row.phase_ns==250&&prior.map.clock.tick==20000000);
 auto race=causal.edge(80001000,2,100,true,reference(80001000));assert(!race.map_valid&&race.row.reason==G1GpsShadow::Race);
 G1PpsDiagnostic late;late.observe(s,edge+1000,edge+2000,2000100);
 assert(late.edge(edge,1,100,true,reference(edge)).row.reason==G1GpsShadow::Race);
 G1PpsDiagnostic unmapped;observe(unmapped,s);
 auto raw=unmapped.edge(edge,1,UINT32_MAX,false,reference(edge));
 assert(!raw.map_valid&&raw.row.reason==G1GpsShadow::Unmapped&&unmapped.view().missing==1&&unmapped.view().samples==0);
 G1PpsDiagnostic expiry;s.g1_corrected_until_us=2100000;observe(expiry,s);
 assert(!expiry.edge(50000000,1,100,true,reference(50000000)).map_valid);
 G1PpsDiagnostic revoke;s=current();observe(revoke,s);auto no_source=s;
 // Authoritative status withdraws root/boot/source when following is lost.
 no_source.following=false;no_source.root_identity=no_source.root_boot=0;no_source.source_epoch=no_source.publication=0;
 revoke.observe(no_source,30001000,30002000,1500100);
 assert(!revoke.edge(edge,1,100,true,reference(edge)).map_valid);
 // A same-model service deadline update must not retain the old longer cap.
 G1PpsDiagnostic service;s=current();observe(service,s);s.service_until_us=1700000;
 service.observe(s,30001000,30002000,1500100);assert(!service.edge(edge,1,100,true,reference(edge)).map_valid);
 // Exact current scope metadata accompanies the model after a scope change.
 G1PpsDiagnostic scopes;s=current();observe(scopes,s);s.root_identity=s.g1_clock_origin=33;s.root_boot=44;
 s.source_epoch=5;s.publication=6;s.g1_relative_quality.scope_generation=7;s.g1_clock_model.tick=30000000;s.g1_clock_model.epoch=1500000250;
 scopes.observe(s,30001000,30002000,1500100);auto changed=scopes.edge(edge,1,100,true,reference(edge));
 assert(changed.map_valid&&changed.map.root==33&&changed.map.boot==44&&changed.map.source==5&&changed.map.train==6&&changed.map.scope==7);
 G1PpsDiagnostic correction;s=current();s.g1_clock_model.output_correction_ns=1234;observe(correction,s);
 auto c=correction.edge(edge,1,100,true,reference(edge));assert(c.row.phase_ns==250&&G1PpsDiagnostic::phase(c.utc_ns)==1484);
 assert(G1PpsDiagnostic::phase(c.utc_ns-c.map.clock.output_correction_ns)==250);
 assert(G1PpsDiagnostic::phase(500000000)==-500000000&&G1PpsDiagnostic::phase(-500000001)==499999999);
 assert(s.g1_clock_model.output_correction_ns==1234&&s.g1_clock_model.epoch==1000000250&&s.g1_corrected_until_us==10000000);
 G1PpsDiagnostic arithmetic;s=current();s.g1_clock_model.rate=std::numeric_limits<double>::quiet_NaN();observe(arithmetic,s);
 assert(arithmetic.edge(edge,1,100,true,reference(edge)).row.reason==G1GpsShadow::Arithmetic);
 std::printf("PASS production G1 PPS with strict R2 fit absent;15 permission/scope negatives, prior/late/race/withdrawal, same-model service expiry, raw unmapped, empirical reference separation and unchanged correction;bytes=%zu\n",sizeof(G1PpsDiagnostic));
}
