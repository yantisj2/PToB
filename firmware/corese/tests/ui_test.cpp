// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include "status_scan.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
using namespace corese;
Model live(){
 Model m;m.sampled_us=m.utc_at_us=m.offset_sampled_us=1000000;
 m.until_us=m.precision_until_us=m.offset_until_us=1300000;
 m.utc_us=10000000;m.utc_valid=m.precision_valid=m.offset_valid=true;
 m.state=State::Locked;m.source=Source::Relay1;m.rms_ns=72;
 m.root=m.offset_root=1;m.boot=m.offset_boot=2;m.source_epoch=m.offset_epoch=3;
 return m;
}
int main(){
 char interval[40];duration_text(interval,sizeof interval,300);assert(!std::strcmp(interval,"5M 0S"));
 duration_text(interval,sizeof interval,2*86400+6*3600+10*60);assert(!std::strcmp(interval,"2D 6H 10M"));
 duration_text(interval,sizeof interval,0);assert(!std::strcmp(interval,"0S"));
 duration_text(interval,sizeof interval,UINT64_MAX);assert(interval[sizeof interval-1]==0||std::strlen(interval)<sizeof interval);

 {
  StatusScan scan;
  static_assert(StatusScan::WindowUs==12000&&StatusScan::IntervalUs==200000&&StatusScan::CaptureAgeUs==4000);
  assert(!scan.poll(100000,false,1,0));
  assert(!scan.poll(100000,true,0,0));
  assert(!scan.poll(100000,true,1,4000));
  assert(scan.poll(100000,true,1,3999));
  const auto deadline=scan.deadline();assert(deadline==112000&&scan.windows()==1);
  assert(scan.poll(111999,true,1,20000)&&scan.deadline()==deadline);
  assert(!scan.poll(112000,true,1,0)&&!scan.allows(112000));
  assert(!scan.observed(99999)&&scan.observed(100000)&&scan.observed(111999)&&!scan.observed(112000));
  assert(!scan.poll(299999,true,2,0));
  assert(!scan.poll(300000,true,1,0)); // One captured PA cannot reopen a window.
  assert(scan.poll(300000,true,2,0)&&scan.windows()==2);
  assert(!scan.poll(300001,false,2,0)&&!scan.allows(300001));
  assert(!scan.poll(300002,true,3,0)&&scan.deadline()==312000);
  assert(scan.observed(300000)&&scan.observed(311999)&&!scan.observed(312000));
  assert(!scan.poll(500000,true,3,4000));
  assert(scan.poll(500001,true,3,0));
  assert(!scan.poll(500000,true,4,0)); // Reversal cannot open a window.
  StatusScan edge;assert(!edge.poll(UINT64_MAX-1,true,1,0));
 }
 auto m=live();uint64_t utc=0;assert(time(m,1000100,utc)&&utc==10000100);
 assert(precision(m,1000100)&&offset(m,1000100));
 {
  DisplayMemory memory;auto sample=live();sample.mesh.path.known=true;sample.mesh.path.until_us=1300000;
  sample.mesh.link.known=true;sample.mesh.link.until_us=1300000;
  sample.mesh.capabilities.known=true;sample.mesh.capabilities.until_us=1200000;
  memory.retain(sample);Model expired;expired.sampled_us=5000000;memory.retain(expired);
  assert(expired.observed.rms_ns==72&&expired.observed.rms_at==1000000);
  assert(expired.mesh.path.known&&expired.mesh.path.until_us==1300000);
  assert(expired.mesh.link.until_us==1300000&&expired.mesh.capabilities.until_us==1200000);
  assert(!expired.utc_valid&&!time(expired,5000000,utc)&&!precision(expired,5000000)&&!offset(expired,5000000));
  assert(!source_pulse(expired,5000000));
 }
 assert(source_pulse(m,1000000)&&source_pulse(m,1124999));
 assert(!source_pulse(m,1125000));
 m.utc_us=10999999;assert(!source_pulse(m,1000000)&&source_pulse(m,1000001));
 m=live();m.source=Source::Rc9;assert(source_pulse(m,1000000));
 m.until_us=1000100;assert(!source_pulse(m,1000100));
 m=live();assert(!source_pulse(m,1250000));
 assert(!time(m,999999,utc));assert(!time(m,1250000,utc));
 m=live();m.until_us=1100000;assert(!time(m,1100000,utc));
 m=live();m.precision_until_us=1000100;assert(!precision(m,1000100));
 m=live();m.source=Source::Rc9;
 assert(time(m,1000100,utc));assert(!precision(m,1000100)&&!offset(m,1000100));
 assert(!std::strcmp(status(m,1000100),"RC9 UTC"));
 // Regression: history seeded by a fine lock must never color the compact
 // register's withdrawn precision green or expose prior values as current.
 {
  DisplayMemory history;auto locked=live();locked.offset_ns=17;history.retain(locked);
  for(unsigned state=0;state<3;++state){
   auto lost=locked;uint64_t at=1000100;
   if(state==0)lost.source=Source::Rc9;
   else if(state==1){lost.state=State::Search;lost.utc_valid=false;}
   else at=1250000;
   history.retain(lost);assert(lost.observed.rms_at&&lost.observed.offset_at);
   View face;face.screen=Screen::Clock;face.face=Face::UtcRegister;
   std::array<uint16_t,Pixels> with_history{},without_history{};
   render(with_history.data(),lost,face,at);lost.observed={};render(without_history.data(),lost,face,at);
   constexpr uint16_t green=((182>>3)<<11)|((239>>2)<<5)|(176>>3);
   for(int y=140;y<163;++y)for(int x=12;x<308;++x){
    assert(with_history[y*Width+x]==without_history[y*Width+x]);
    assert(with_history[y*Width+x]!=green);
   }
  }
 }
 m=live();m.offset_boot=9;assert(!offset(m,1000100));
 m=live();m.offset_epoch=9;assert(!offset(m,1000100));
 m=live();m.offset_sampled_us=1000101;assert(!offset(m,1000100));
 m=live();m.state=State::Hold;assert(!time(m,1000100,utc)&&!precision(m,1000100));
 m=live();m.utc_us=UINT64_MAX;assert(!time(m,1000001,utc));
 // Exercise every pixel-producing surface/state with guard words and sanitizers.
 std::array<uint16_t,Pixels+2> buffer{};buffer.front()=0x1234;buffer.back()=0xabcd;
 for(unsigned state=0;state<=unsigned(State::Fault);++state)
  for(unsigned source=0;source<=unsigned(Source::Rc9);++source)
   for(unsigned screen=0;screen<=unsigned(Screen::Diagnostics);++screen)
    for(unsigned face=0;face<=unsigned(Face::UtcRegister);++face)
     for(unsigned page=0;page<unsigned(mesh::Page::Count);++page){
     m=live();m.state=State(state);m.source=Source(source);m.offset_ns=INT64_MIN;m.rms_ns=UINT32_MAX;
     std::memset(m.parent,'W',24);m.parent[24]=0;
     View v;v.screen=Screen(screen);v.face=Face(face);v.diagnostic=mesh::Page(page);render(buffer.data()+1,m,v,1000100);
     assert(buffer.front()==0x1234&&buffer.back()==0xabcd);
    }
 View v;Touch touch;
 assert(touch.update(v,true,20,130)==Action::None&&v.pressed==Action::OpenClock);
 assert(touch.update(v,true,20,130)==Action::None);
 auto action=touch.update(v,false,20,130);assert(action==Action::OpenClock);apply(v,action);
 assert(v.screen==Screen::Clock);assert(touch.update(v,false,20,130)==Action::None);
 touch.update(v,true,20,190);touch.update(v,true,100,190);
 assert(touch.update(v,false,20,190)==Action::None); // Drag cancellation latches.
 touch.update(v,true,20,190);assert(touch.update(v,false,319,239)==Action::None);
 assert(hit(v,77,190)==Action::None&&hit(v,307,239)==Action::None);
 apply(v,Action::Home);assert(v.screen==Screen::Launcher);
 apply(v,Action::NextFace);assert(v.face==Face::UtcRegister);
 apply(v,Action::OpenDiagnostics);assert(v.screen==Screen::Diagnostics);
 assert(hit(v,100,190)==Action::PreviousDiagnostic&&hit(v,200,190)==Action::NextDiagnostic);
 assert(hit(v,160,190)==Action::None&&hit(v,260,190)==Action::OpenClock);
 apply(v,Action::NextDiagnostic);assert(v.diagnostic==mesh::Page::Path);
 apply(v,Action::PreviousDiagnostic);assert(v.diagnostic==mesh::Page::Mesh);
 apply(v,Action::PreviousDiagnostic);assert(v.diagnostic==mesh::Page::Wire);
 apply(v,Action::NextDiagnostic);assert(v.diagnostic==mesh::Page::Mesh);
 // Diagnostics navigation never modifies clock/face selection.
 const auto face=v.face;apply(v,Action::NextDiagnostic);assert(v.face==face&&v.screen==Screen::Diagnostics);
 mesh::Roles roles;roles.known=true;roles.received_us=100;roles.until_us=200;
 assert(mesh::live(roles,100)&&mesh::live(roles,199)&&!mesh::live(roles,200)&&!mesh::live(roles,99));
 mesh::Capabilities caps{6,7,100,200,2,true};
 assert(mesh::current(caps,6,7,199)&&!mesh::current(caps,1,7,199)&&!mesh::current(caps,6,8,199)&&!mesh::current(caps,6,7,200));
 {
  mesh::Reference ref;ref.reported=true;ref.scope={1,2,3};ref.received_us=100;ref.until_us=30000100;
  assert(mesh::current(ref,{1,2,3},100)&&!mesh::current(ref,{1,2,3},99)&&!mesh::current(ref,{1,2,3},30000100));
  assert(!mesh::current(ref,{1,2,4},100)&&!mesh::current(ref,{1,3,3},100));
  char hdop[16];mesh::reference_hdop(hdop,sizeof hdop,80,true);assert(!std::strcmp(hdop,"0.80"));
  mesh::reference_hdop(hdop,sizeof hdop,0,true);assert(!std::strcmp(hdop,"0.00"));
  mesh::reference_hdop(hdop,sizeof hdop,65534,true);assert(!std::strcmp(hdop,"655.34"));
  mesh::reference_hdop(hdop,sizeof hdop,UINT16_MAX,true);assert(!std::strcmp(hdop,"--"));
  mesh::reference_hdop(hdop,sizeof hdop,80,false);assert(!std::strcmp(hdop,"--"));
  char ptp[80];ref.fields=mesh::Ptp;mesh::reference_ptp(ptp,sizeof ptp,ref,true);
  assert(std::strstr(ptp,"VAR NONE")&&std::strstr(ptp,"ACC UNKNOWN")&&std::strstr(ptp,"C248"));
  ref.variance=65534;ref.accuracy=253;mesh::reference_ptp(ptp,sizeof ptp,ref,true);
  assert(std::strstr(ptp,"VAR FFFE")&&std::strstr(ptp,"ACC FD")&&!std::strstr(ptp,"NONE"));
  mesh::reference_ptp(ptp,sizeof ptp,ref,false);assert(std::strstr(ptp,"VAR --")&&!std::strstr(ptp,"FFFE"));
  DisplayMemory memory;auto sample=live();sample.mesh.reference=ref;memory.retain(sample);
  Model expired;expired.sampled_us=40000000;memory.retain(expired);
  assert(expired.mesh.reference.reported&&expired.mesh.reference.until_us==30000100);
  assert(!time(expired,40000000,utc)&&!precision(expired,40000000)&&!offset(expired,40000000));
  // Same-scope descriptive history survives expiry/stale model publication in
  // amber; a different selected scope must still hide the prior root data.
  View reference;reference.screen=Screen::Diagnostics;reference.diagnostic=mesh::Page::Reference;
  std::array<uint16_t,Pixels> absent{},withdrawn{};
  auto model=live();model.mesh.reference=ref;model.mesh.reference.fields=31;model.mesh.reference.values=7;
  model.mesh.reference.sats=14;model.mesh.reference.clock_class=6;
  for(unsigned reason=0;reason<3;++reason){
   auto hidden=model;if(reason==0)hidden.mesh.reference.until_us=1000100;
   else if(reason==1)++hidden.mesh.reference.scope.epoch;else hidden.sampled_us=1;
   render(withdrawn.data(),hidden,reference,1000100);hidden.mesh.reference={};render(absent.data(),hidden,reference,1000100);
   constexpr uint16_t green=((182>>3)<<11)|((239>>2)<<5)|(176>>3);
   bool different=false;
   for(int y=79;y<166;++y)for(int x=12;x<308;++x){
    if(reason==1)assert(withdrawn[y*Width+x]==absent[y*Width+x]);
    else{assert(withdrawn[y*Width+x]!=green);different=different||withdrawn[y*Width+x]!=absent[y*Width+x];}
   }
   if(reason!=1)assert(different);
  }
  // Receiver arrival time is separate from the GM sample. Changing the three
  // source sample ages must not change the Reference payload at all.
  auto ages_a=model;ages_a.mesh.reference.rmc_age_ms=100;ages_a.mesh.reference.pps_age_ms=200;ages_a.mesh.reference.gga_age_ms=300;
  auto ages_b=ages_a;ages_b.mesh.reference.rmc_age_ms=500;ages_b.mesh.reference.pps_age_ms=700;ages_b.mesh.reference.gga_age_ms=900;
  render(withdrawn.data(),ages_a,reference,1000100);render(absent.data(),ages_b,reference,1000100);assert(withdrawn==absent);
  auto unselected=model;unselected.root=unselected.boot=0;unselected.source_epoch=0;
  unselected.utc_valid=unselected.precision_valid=false;unselected.source=Source::None;
  render(withdrawn.data(),unselected,reference,1000100);unselected.mesh.reference={};render(absent.data(),unselected,reference,1000100);
  bool last_gm=false;constexpr uint16_t green=((182>>3)<<11)|((239>>2)<<5)|(176>>3);
  for(int y=82;y<176;++y)for(int x=12;x<308;++x){assert(withdrawn[y*Width+x]!=green);last_gm=last_gm||withdrawn[y*Width+x]!=absent[y*Width+x];}
  assert(last_gm);const auto original_until=model.mesh.reference.until_us;
  assert(!mesh::current(model.mesh.reference,{1,2,3},original_until));
  assert(model.mesh.reference.until_us==original_until); // Reacquisition never renews old report.
  model.mesh.reference.until_us=1000100;model.mesh.reference.values=0;model.mesh.reference.sats=0;
  render(withdrawn.data(),model,reference,1000100);auto unknown=model;unknown.mesh.reference.fields=0;
  unknown.mesh.reference.rmc_age_ms=unknown.mesh.reference.pps_age_ms=unknown.mesh.reference.gga_age_ms=UINT16_MAX;
  render(absent.data(),unknown,reference,1000100);
  bool no_retained=false;for(int y=79;y<102;++y)for(int x=12;x<308;++x)no_retained=no_retained||withdrawn[y*Width+x]!=absent[y*Width+x];
  assert(no_retained);
 }
 return 0;
}
