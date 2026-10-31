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
  StatusScan scan;constexpr uint64_t utc_base=1800000000000000;
  static_assert(StatusScan::CaptureAgeUs==25000&&StatusScan::LastEndUs==56000&&StatusScan::MaxWindowUs==30000);
  assert(!scan.poll(1006000,false,1,6000,utc_base+6000));
  assert(!scan.poll(1006000,true,0,6000,utc_base+6000));
  assert(!scan.poll(1006000,true,1,25000,utc_base+25000));
  assert(!scan.poll(1006000,true,1,6000,utc_base+6000));
  assert(scan.deadline()==1055000&&scan.windows()==0&&!scan.allows(1040999));
  assert(!scan.poll(1040999,true,1,40999,utc_base+40999));
  assert(scan.poll(1041000,true,1,41000,utc_base+41000)&&scan.windows()==1);
  assert(scan.poll(1054999,true,1,54999,utc_base+54999)&&scan.deadline()==1055000);
  assert(!scan.poll(1055000,true,1,0,utc_base+55000));
  assert(!scan.observed(1040999)&&scan.observed(1041000)&&scan.observed(1054999)&&!scan.observed(1055000));
  assert(!scan.poll(1068500,true,2,6000,utc_base+68500));
  assert(scan.deadline()==1113000&&!scan.allows(1084999));
  assert(scan.poll(1085000,true,2,22500,utc_base+85000)&&scan.windows()==2);
  assert(scan.poll(1112999,true,2,50499,utc_base+112999));
  assert(!scan.poll(1113000,true,2,0,utc_base+113000));
  assert(!scan.poll(1131000,true,3,6000,utc_base+131000)); // No management slot here.
  assert(!scan.poll(1256000,true,4,6000,utc_base+256000));
  assert(!scan.poll(1260000,false,4,10000,utc_base+260000)&&!scan.allows(1286000));
  const auto windows=scan.windows();scan.reset();
  assert(!scan.observed(1286000)&&scan.deadline()==0&&scan.windows()==windows);
  assert(!scan.poll(2006000,true,5,6000,utc_base+1006000));
  assert(!scan.poll(2005999,true,5,6000,utc_base+1006000)&&!scan.allows(2032000));
  StatusScan edge;assert(!edge.poll(UINT64_MAX-1,true,1,0,utc_base));
  StatusScan phase;assert(!phase.poll(1000,true,1,6000,utc_base+62000)); // Would cross next physical PA.
  // Use the real250ms calendar and complete2ms events. Cover every jittered
  // STATUS slot in the2s producer cycle, every ROLE frame and the45/50ms ANN discovery copies.
  StatusScan calendar;unsigned ann=0,diag[8]{},status=0,expected_ann=0,expected_status=0;
  uint64_t capture=0,physical=0;
  // Physical PA reception can precede the UTC quarter boundary. Its guard
  // must still admit complete ROLE events, not only the later STATUS slot.
  for(uint64_t at=1000000;at<21000000;at+=250){
   const auto pa=(at+1000)/62500*62500-1000;
   if(at-pa>=18000){capture=pa+1;physical=pa;}
   calendar.poll(at,true,capture,capture?at-physical:UINT64_MAX,utc_base+at);
   const auto quarter=at%250000,cycle=at/2000000;
   const auto discovery=45000+(at/250000%3)*5000;
   if(quarter==discovery&&discovery<=50000){++expected_ann;if(calendar.allows(at)&&calendar.observed(at+4000))++ann;}
   if(quarter==43000&&calendar.allows(at)&&calendar.observed(at+4000))++diag[at/250000%8];
   const uint64_t status_phase=uint64_t(90000+int64_t(cycle%8)*2500+(int64_t(cycle%3)-1)*250);
   if(at%2000000==status_phase){++expected_status;if(calendar.allows(at)&&calendar.observed(at+4000))++status;}
   if(calendar.allows(at))assert(at-pa>=8000&&at-pa<56000);
  }
  assert(ann==expected_ann&&expected_ann==53&&status==expected_status&&status==10);
  for(auto n:diag)assert(n==10);
  assert(calendar.primary_windows()==80&&calendar.status_windows()==80&&calendar.guard_refused()==0);
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
 // Sustained loss and RC9 must never expose prior compact precision. Brief
 // cached readouts have their separate, explicitly LAST500ms contract below.
 {
  DisplayMemory history;auto locked=live();locked.offset_ns=17;history.retain(locked);
  for(unsigned state=0;state<3;++state){
   auto lost=locked;uint64_t at=1000100;
   if(state==0)lost.source=Source::Rc9;
   else if(state==1){lost.state=State::Search;lost.utc_valid=false;at=1500000;}
   else at=1500000;
   history.retain(lost);
   if(state==0)assert(!lost.observed.rms_at&&!lost.observed.offset_at);
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
 // Repeated invalid publications must not restart the500ms numeric grace.
 // Source/lock/pulse still withdraw immediately, and a new scope never borrows
 // a prior source's statistics, even within that grace.
 {
  DisplayMemory memory;auto good=live();good.holdover.generation=7;good.delivery_observed_us=1000000;
  good.tz={2,-300};good.tz_until_us=1600000;
  good.holdover.last20=20000000;good.holdover.witness_age_us=0;
  good.holdover.ready=true;good.holdover.duration_ms=24000;good.holdover.samples=128;
  good.holdover.p95_ns=80;good.holdover.p99_ns=99;good.holdover.max_ns=120;
  memory.retain(good);bool visible=false;
  for(uint64_t delta:{1ull,250000ull,499999ull,500000ull,750000ull}){
   auto gap=good;gap.sampled_us+=delta;gap.utc_valid=gap.precision_valid=false;
   gap.root=gap.boot=gap.source_epoch=0;gap.source=Source::None;gap.state=State::Acquire;
   memory.retain(gap);const auto& shown=shadow_readout(gap,gap.sampled_us,visible);
   assert(visible==(delta<500000));
   if(visible)assert(shown.duration_ms==24000&&shown.p99_ns==99&&shown.samples==128);
   assert(!time(gap,gap.sampled_us,utc)&&!precision(gap,gap.sampled_us)&&!source_pulse(gap,gap.sampled_us));
   assert(readout_time(gap,gap.sampled_us,utc)==(delta<500000));
   if(delta<500000){assert(utc==10000000+delta);int64_t shown=0;assert(!std::strcmp(clock_zone(gap,gap.sampled_us,shown),"CDT"));}
   assert(readout_recent(gap,gap.sampled_us,gap.observed.rms_at)==(delta<500000));
  }
  // Restored live data replaces the cache without a wait.
  good.sampled_us=good.utc_at_us=good.offset_sampled_us=2000000;
  good.delivery_observed_us=2000000;
  good.until_us=good.precision_until_us=good.offset_until_us=2300000;
  good.holdover.duration_ms=26000;memory.retain(good);
  assert(shadow_readout(good,2000001,visible).duration_ms==26000&&visible);
  for(unsigned change=0;change<6;++change){
   DisplayMemory fence;auto seed=good;fence.retain(seed);auto other=good;
   other.sampled_us+=100;other.utc_valid=other.precision_valid=false;
   if(change==0)++other.root;else if(change==1)++other.boot;else if(change==2)++other.source_epoch;
   else if(change==3)++other.holdover.generation;else if(change==4)other.state=State::Fault;else other.source=Source::Rc9;
   fence.retain(other);shadow_readout(other,other.sampled_us,visible);assert(!visible&&!other.observed.rms_at);
  }
  auto backwards=good;backwards.utc_valid=false;
  shadow_readout(backwards,1999999,visible);assert(!visible);
 }
 m=live();m.offset_boot=9;assert(!offset(m,1000100));
 m=live();m.offset_epoch=9;assert(!offset(m,1000100));
 m=live();m.offset_sampled_us=1000101;assert(!offset(m,1000100));
 m=live();m.state=State::Hold;assert(!time(m,1000100,utc)&&!precision(m,1000100));
 {
  auto held=live();assert(retained_time(held,1000000,1100000,10100000,1100000));
  assert(time(held,1100000,utc)&&utc==10100000);
  assert(held.until_us==1500000&&held.root==0&&held.boot==0&&held.source_epoch==0);
  assert(!precision(held,1100000)&&!offset(held,1100000)&&!source_pulse(held,1100000));
  assert(!std::strcmp(status(held,1100000),"HOLD")&&!std::strcmp(source_name(held,1100000),"HOLD"));
  // New snapshots cannot renew the last actual delivery's finite limit.
  assert(retained_time(held,1000000,1499999,10549999,1499999));
  assert(held.until_us==1500000&&time(held,1499999,utc));
  assert(!time(held,1500000,utc)&&!retained_time(held,1000000,1500000,10550000,1500000));
  assert(!retained_time(held,1100001,1100000,10000000,1100000));
  assert(!retained_time(held,0,1100000,10000000,1100000));
  assert(!retained_time(held,UINT64_MAX-100,UINT64_MAX-100,10000000,UINT64_MAX-100));
  held=live();held.source=Source::Retained;assert(!time(held,1000100,utc)&&!precision(held,1000100));
 }
 m=live();m.utc_us=UINT64_MAX;assert(!time(m,1000001,utc));

 {char number[20];
  assert(!std::strcmp(hold_error(number,sizeof number,999,true),"ns")&&!std::strcmp(number,"999"));
  assert(!std::strcmp(hold_error(number,sizeof number,1000,true),"us")&&!std::strcmp(number,"1.00"));
  assert(!std::strcmp(hold_error(number,sizeof number,999999,true),"us")&&!std::strcmp(number,"999.99"));
  assert(!std::strcmp(hold_error(number,sizeof number,1000000,true),"ms")&&!std::strcmp(number,"1.00"));
  assert(!std::strcmp(hold_error(number,sizeof number,1000000000,true),"s")&&!std::strcmp(number,"1.00"));
  assert(!std::strcmp(hold_error(number,sizeof number,1000000000000ull,true),"s")&&!std::strcmp(number,"HI"));
  hold_error(number,sizeof number,UINT64_MAX,true);assert(!std::strcmp(number,"----"));
 }
 // TZ affects only clock presentation, with receipt-based expiry and signed rollover.
 {auto z=live();z.utc_us=1793381400000000ull;z.tz={2,-300};z.tz_until_us=1000200;
  int64_t shown=0;assert(!std::strcmp(clock_zone(z,1000100,shown),"CDT"));assert(shown==int64_t(z.utc_us+100)-18000000000ll);
  z.tz.offset_min=-360;assert(!std::strcmp(clock_zone(z,1000100,shown),"CST"));
  z.tz={77,345};assert(!std::strcmp(clock_zone(z,1000100,shown),"LOC"));
  assert(!std::strcmp(clock_zone(z,1000200,shown),"UTC"));
  z.source=Source::Rc9;assert(!std::strcmp(clock_zone(z,1000100,shown),"UTC"));
  z=live();z.holdover.last20=1000000ull*20;z.holdover.witness_age_us=0;z.holdover.ready=true;z.holdover.duration_ms=24000;
  assert(shadow_current(z,1000100));char d[20];hold_duration(d,sizeof d,z.holdover,true);assert(!std::strcmp(d,"24.00"));
  z.source=Source::Retained;assert(!shadow_current(z,1000100));
  z=live();z.holdover.last20=1;assert(!shadow_current(z,1000100));
 }
 // Exercise every pixel-producing surface/state with guard words and sanitizers.
 std::array<uint16_t,Pixels+2> buffer{};buffer.front()=0x1234;buffer.back()=0xabcd;
 for(unsigned state=0;state<=unsigned(State::Fault);++state)
  for(unsigned source=0;source<=unsigned(Source::Retained);++source)
   for(unsigned screen=0;screen<=unsigned(Screen::Diagnostics);++screen)
    for(unsigned face=0;face<unsigned(Face::Count);++face)
     for(unsigned page=0;page<unsigned(mesh::Page::Count);++page){
     m=live();m.state=State(state);m.source=Source(source);m.offset_ns=INT64_MIN;m.rms_ns=UINT32_MAX;
     std::memset(m.parent,'W',24);m.parent[24]=0;
     View v;v.screen=Screen(screen);v.face=Face(face);v.diagnostic=mesh::Page(page);render(buffer.data()+1,m,v,1000100);
     assert(buffer.front()==0x1234&&buffer.back()==0xabcd);
    }
 {auto high=live();high.holdover.last20=1;high.holdover.witness_age_us=0;high.holdover.max_ns=1000000000000ull;
  View shadow;shadow.screen=Screen::Diagnostics;shadow.diagnostic=mesh::Page::Shadow;render(buffer.data()+1,high,shadow,1000100);
  constexpr uint16_t amber=((245>>3)<<11)|((196>>2)<<5)|(105>>3);
  assert(buffer[1+111*Width+236]==amber); // Explicit text HI, not empty seven-segment ghosts.
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
 apply(v,Action::NextFace);assert(v.face==Face::ReferenceWatch);
 apply(v,Action::NextFace);assert(v.face==Face::EpochCounter);
 apply(v,Action::NextFace);assert(v.face==Face::Instrument);
 apply(v,Action::NextFace);assert(v.face==Face::UtcRegister);
 apply(v,Action::OpenDiagnostics);assert(v.screen==Screen::Diagnostics);
 assert(hit(v,100,190)==Action::PreviousDiagnostic&&hit(v,200,190)==Action::NextDiagnostic);
 assert(hit(v,160,190)==Action::None&&hit(v,260,190)==Action::OpenClock);
 apply(v,Action::NextDiagnostic);assert(v.diagnostic==mesh::Page::Path);
 apply(v,Action::PreviousDiagnostic);assert(v.diagnostic==mesh::Page::Mesh);
 apply(v,Action::PreviousDiagnostic);assert(v.diagnostic==mesh::Page::Shadow);
 apply(v,Action::NextDiagnostic);assert(v.diagnostic==mesh::Page::Mesh);
 // Diagnostics navigation never modifies clock/face selection.
 const auto face=v.face;apply(v,Action::NextDiagnostic);assert(v.face==face&&v.screen==Screen::Diagnostics);
 mesh::Roles roles;roles.known=true;roles.received_us=100;roles.until_us=200;
 assert(mesh::live(roles,100)&&mesh::live(roles,199)&&!mesh::live(roles,200)&&!mesh::live(roles,99));
 {
  auto model=live();auto& r=model.mesh.roles;r.known=true;r.wire_version=3;r.root=model.root;r.root_boot=model.boot;r.epoch=model.source_epoch;
  r.birth_utc_us=1792986374188267;model.utc_us=r.birth_utc_us+7200000000;
  r.received_us=1000000;r.until_us=3750000;r.term=16;r.announcements=100;r.statuses=300;r.role_reports=99;
  View wire;wire.screen=Screen::Diagnostics;wire.diagnostic=mesh::Page::Wire;
  std::array<uint16_t,Pixels> shown{},hidden{};render(shown.data(),model,wire,1000100);
  constexpr uint16_t green=((182>>3)<<11)|((239>>2)<<5)|(176>>3);
  for(unsigned loss=0;loss<4;++loss){auto unavailable=model;
   if(loss==0)unavailable.utc_valid=false;else if(loss==1)++unavailable.source_epoch;
   else if(loss==2)unavailable.utc_us=r.birth_utc_us-1000000;else unavailable.source=Source::Retained;
   render(hidden.data(),unavailable,wire,1000100);
   bool changed=false;for(int y=74;y<85;++y)for(int x=23;x<304;++x){assert(hidden[y*Width+x]!=green);changed=changed||hidden[y*Width+x]!=shown[y*Width+x];}
   assert(changed); // Signed birth survives, elapsed needs current matched UTC.
   for(int y=59;y<68;++y)for(int x=23;x<304;++x)assert(hidden[y*Width+x]==shown[y*Width+x]);
  }
 }
 {
  auto model=live();auto& r=model.mesh.roles;r.known=true;r.wire_version=3;r.hot=4;r.hot_boot=5;r.hot_since_us=100000;
  r.received_us=500000;r.until_us=3000000;
  r.hot_report_known=r.hot_confirmed=true;r.hot_role=3;r.hot_received_us=500000;r.hot_until_us=5000000;
  View mesh;mesh.screen=Screen::Diagnostics;mesh.diagnostic=mesh::Page::Mesh;
  std::array<uint16_t,Pixels> current{},cached{};render(current.data(),model,mesh,1000100);
  r.hot_until_us=1000100;r.hot_confirmed=false;render(cached.data(),model,mesh,1000100);
  assert(mesh::hot_nomination_current(r,1000100));
  // Self-report expiry cannot recolor or clear the ACTIVE's current nominee.
  for(int y=120;y<150;++y)for(int x=12;x<181;++x)assert(current[y*Width+x]==cached[y*Width+x]);
  for(int y=146;y<158;++y)for(int x=191;x<296;++x)assert(current[y*Width+x]==cached[y*Width+x]);
  bool age_label_changed=false;for(int y=120;y<163;++y)for(int x=12;x<181;++x)age_label_changed=age_label_changed||current[y*Width+x]!=cached[y*Width+x];
  assert(age_label_changed); // The separate SELF report age labels its freshness.
  std::array<uint16_t,Pixels> old_nomination{};r.until_us=1000100;render(old_nomination.data(),model,mesh,1000100);
  assert(!mesh::hot_nomination_current(r,1000100));
  bool nominee_changed=false;for(int y=120;y<150;++y)for(int x=12;x<181;++x)nominee_changed=nominee_changed||cached[y*Width+x]!=old_nomination[y*Width+x];
  assert(nominee_changed);
  for(int y=146;y<158;++y)for(int x=191;x<296;++x)assert(cached[y*Width+x]==old_nomination[y*Width+x]);
  r.until_us=3000000;r.withdrawn=true;assert(!mesh::hot_nomination_current(r,1000100));
  r.withdrawn=false;r.hot=0;assert(!mesh::hot_nomination_current(r,1000100));
 }
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
 {
  for(unsigned page=0;page<=10;++page){View selected;assert(select_presentation(selected,page));assert(presentation_page(selected)==page);}
  View selected;assert(!select_presentation(selected,11)&&selected.screen==Screen::Launcher);
  mesh::ReferenceObserver observer;mesh::Reference q;q.reported=true;q.scope={1,2,3};q.fields=q.values=mesh::Fix|mesh::Pps;q.received_us=100;q.until_us=1000;
  observer.observe(q,{1,2,3},100);assert(observer.snapshot().since_us==100);
  observer.observe(q,{1,2,3},900);assert(observer.snapshot().since_us==100&&observer.snapshot().until_us==1000);
  q.received_us=950;q.until_us=2000;observer.observe(q,{1,2,3},950);assert(observer.snapshot().since_us==100&&observer.snapshot().until_us==2000);
  assert(mesh::current(observer.snapshot(),{1,2,3},1999)&&!mesh::current(observer.snapshot(),{1,2,3},2000));
  // A new good report after a gap must restart, even if no UI poll saw expiry.
  q.received_us=2100;q.until_us=3000;observer.observe(q,{1,2,3},2100);assert(observer.snapshot().since_us==2100);
  q.values=mesh::Fix;observer.observe(q,{1,2,3},2200);assert(!observer.snapshot().tracking);
  q.values=mesh::Fix|mesh::Pps;observer.observe(q,{1,2,3},2201);assert(observer.snapshot().since_us==2201);
  q.fields=mesh::Fix;observer.observe(q,{1,2,3},2202);assert(!observer.snapshot().tracking);
  q.fields=mesh::Fix|mesh::Pps;observer.observe(q,{1,2,3},2203);
  assert(!mesh::current(observer.snapshot(),{1,2,4},2203));
  observer.observe(q,{1,2,4},2204);assert(!observer.snapshot().tracking);
  observer.observe(q,{},2205);assert(!observer.snapshot().tracking);
  observer.observe(q,{1,2,3},2206);observer.clear();assert(!observer.snapshot().tracking);
  observer.observe(q,{1,2,3},3000);assert(!observer.snapshot().tracking);
 }
 return 0;
}
