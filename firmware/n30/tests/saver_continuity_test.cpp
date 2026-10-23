// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "dual_time.hpp"
#include "display_phase.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
namespace {
using namespace n30;
Model live(uint64_t now,bool ble_lock=true,bool lora=false,bool pending=false){
 Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+SnapshotMaxUs;
 m.utc_us=10000000+now;m.valid=true;m.locked=ble_lock;m.mode_pending=pending;
 m.state=State::Follow;m.source=Source::Ble;m.hz=16;
 m.ble={now,m.utc_us,m.until_us,1,2,3,true,true};
 n30_lora_view l{};
 if(lora){l.requested=l.active=true;l.state=2;l.sampled_us=now;l.utc_us=m.utc_us+1;l.until_us=now+12000000;}
 select_lora(m,l,now);return m;
}
Model absent(uint64_t now,bool hold=false){
 Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+SnapshotMaxUs;
 m.utc_us=10000000+now;m.valid=hold;m.state=hold?State::Hold:State::Acquire;
 m.source=hold?Source::Ble:Source::None;m.ble={now,m.utc_us,m.until_us,1,2,3,hold,false};return m;
}
struct Worker {
 Input input;ClockHold cache;SaverTimer session;View view;uint8_t pixels[FrameBytes]{};
 void step(const Model& m,uint64_t now,bool transfer=true){
  update_saver(input,cache,session,m,view,now);cache.draw(pixels,m,view,now);
  if(transfer)cache.presented(m,view,now,pixels);else cache.reset();
 }
};
bool pixel(const uint8_t* f,unsigned x,unsigned y){return f[y*8+x/8]&(0x80u>>(x%8));}
void minimal_only(const Worker& w){
 assert(w.view.saver&&w.view.minimal);const unsigned top=4+22*w.view.saver_position;
 for(unsigned y=0;y<128;y++)if(y<top||y>=top+11)for(unsigned x=0;x<64;x++)assert(!pixel(w.pixels,x,y));
}
}
int main(){
 using namespace n30;
 for(unsigned page=0;page<n30::PageCount;page++)for(unsigned pos=0;pos<3;pos++)for(bool minimal:{false,true}){
  Worker w;w.view.page=page;const uint64_t entry=IdleUs+pos*MoveUs+1000000;
  w.step(live(entry),entry);assert(w.view.saver&&!w.view.minimal&&w.view.saver_position==pos);
  const uint64_t at=entry+(minimal?MinimalAfterUs:1000000);
  w.step(live(at),at);assert(w.view.saver&&w.view.minimal==minimal);
  // Repeated real BLE/LOR selection switches, pending BLE, and a live but
  // unlocked BLE clock never interrupt the one resident saver session.
  for(unsigned i=1;i<=400;i++){
   const auto now=at+i*10000;const auto m=live(now,i%4==0,i%4==1||i%4==2,i%4==1);
   w.step(m,now);assert(w.view.saver&&w.view.minimal==minimal&&!w.view.clock_frozen);
   if(minimal)minimal_only(w);
   if(!m.locked)assert(!pps_on(m,now)); // No new precision or PPS permission.
  }
  // A long precision dip keeps time and the30s movement clock alive.
  const uint64_t moving=at+MoveUs+10000;w.step(live(moving,false),moving);
  assert(w.view.saver&&w.view.minimal==minimal&&w.view.saver_position==(pos+1)%3);
  // Resetting the pixel cache after an I2C failure cannot reset saver age.
  w.step(live(moving+10000,false),moving+10000,false);
  w.step(live(moving+20000,false),moving+20000);assert(w.view.saver&&w.view.minimal==minimal);
  // Both inputs absent: clock pixels expire, but the selected page must never
  // replace the saver, including after a failed I2C transfer cleared its cache.
  w.cache.reset();const auto lost=moving+30000;w.step(absent(lost),lost);
  w.step(absent(lost+499999,true),lost+499999);assert(w.view.saver&&w.view.minimal==minimal);
  if(minimal)minimal_only(w);
  for(uint64_t dt:{500000ULL,510000ULL,900000ULL,10000000ULL}){
   const auto now=lost+dt;const auto m=absent(now,true);w.step(m,now);
   assert(w.view.saver&&w.view.minimal==minimal&&!w.view.clock_frozen);
   assert(!pps_on(m,now));uint8_t expected[FrameBytes];render(expected,m,w.view,now);
   assert(!std::memcmp(w.pixels,expected,FrameBytes));
   View page_view;page_view.page=page;render(expected,m,page_view,now);
   assert(std::memcmp(w.pixels,expected,FrameBytes));
   if(minimal)minimal_only(w);
  }
  // Recovery retains the same residence, including the minimal layout.
  const auto recovered=lost+11000000;w.step(live(recovered),recovered);
  assert(w.view.saver&&w.view.minimal==minimal);
  w.step(live(recovered+MinimalAfterUs),recovered+MinimalAfterUs);minimal_only(w);
 }
 // The fifteen-minute boundary can occur inside a brief total outage. Recovery
 // by the other input keeps minimal mode; long outages retain that surface.
 for(unsigned page=0;page<n30::PageCount;page++){
  Worker w;w.view.page=page;const auto start=IdleUs+1000000,edge=start+MinimalAfterUs;
  w.step(live(start),start);w.step(live(edge-200000),edge-200000);
  w.step(absent(edge-100000),edge-100000);assert(w.view.saver&&!w.view.minimal&&w.view.clock_frozen);
  w.step(absent(edge),edge);minimal_only(w);
  w.step(live(edge+399999,false,true,true),edge+399999);minimal_only(w);
  for(unsigned i=0;i<5;i++){
   const auto lost=edge+500000+i*1000000;w.step(absent(lost),lost);minimal_only(w);
   w.step(absent(lost+499999),lost+499999);minimal_only(w);
   w.step(live(lost+499999,false,i%2),lost+499999);minimal_only(w);
  }
  const auto lost=edge+6000000;w.step(absent(lost),lost);w.step(absent(lost+500000),lost+500000);minimal_only(w);assert(!w.view.clock_frozen);
 }
 // Any available following input prevents total loss, even if an intervening
 // selected-clock snapshot is unusable. Local HOLD and stale snapshots cannot.
 for(bool lora:{false,true}){
  Worker w;const auto start=IdleUs+1000000,at=start+MinimalAfterUs;
  w.step(live(start),start);w.step(live(at),at);
  for(unsigned i=1;i<=100;i++){
   const auto now=at+i*10000;auto m=live(now,false,lora);m.valid=false;m.source=Source::None;
   w.step(m,now);minimal_only(w);
  }
  auto stale=live(at+1100000,false,lora);w.step(stale,at+1350000);minimal_only(w);
  w.step(stale,at+1849999);minimal_only(w);w.step(stale,at+1850000);minimal_only(w);assert(!w.view.clock_frozen);
 }
 // Wake/navigation still override both saver layouts immediately.
 Worker w;const auto start=IdleUs+1000000,at=start+MinimalAfterUs;
 w.step(live(start),start);w.step(live(at),at);minimal_only(w);
 w.input.update(true,at+10000,w.view);w.input.update(true,at+50000,w.view);w.step(live(at+50000),at+50000);assert(!w.view.saver&&!w.view.minimal);
 assert(w.input.update(true,at+2000000,w.view)==Action::None);
}
