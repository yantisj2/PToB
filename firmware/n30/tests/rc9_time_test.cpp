// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9_time.hpp"
#include "sync_watchdog.hpp"
#include "dual_time.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
int main(){
 using namespace n30;constexpr uint64_t now=1000000;
 v12::Rc9Status s;s.sampled_us=now;s.until_us=now+10000000;s.utc_us=1700000000000000;
 s.server=1;s.server_boot=2;s.root=3;s.source_epoch=4;s.sequence=5;s.valid=s.following=true;
 uint64_t utc=0;assert(rc9_time(s,now,utc)&&utc==s.utc_us);assert(!s.root_boot);
 v12::R2Status normal;normal.requested=true;n30_lora_view l{};
 assert(synchronized_until(normal,l,now,s)==now+SnapshotMaxUs);assert(!ble_locked(normal,now));
 for(unsigned failure=0;failure<10;failure++){
  auto bad=s;if(failure==0)bad.valid=false;if(failure==1)bad.following=false;if(failure==2)bad.fault=true;
  if(failure==3)bad.server=0;if(failure==4)bad.server_boot=0;if(failure==5)bad.root=0;if(failure==6)bad.sequence=0;
  if(failure==7)bad.sampled_us=now+1;if(failure==8)bad.until_us=now;if(failure==9)bad.source_epoch=0;
  assert(!rc9_time(bad,now,utc));assert(!synchronized_until(normal,l,now,bad));
 }
 assert(!rc9_time(s,now+SnapshotMaxUs,utc));
 Model m;m.sampled_us=now;m.state=State::Search;select_rc9(m,s,now,"C-4","TC-03");
 assert(m.valid&&!m.locked&&!m.serving&&m.source==Source::Rc9&&m.state==State::Follow);
 assert(!std::strcmp(source_label(m,now),"RC9")&&!std::strcmp(m.parent,"C-4")&&!std::strcmp(m.root,"TC-03"));
 assert(!precision_current(m,now)&&m.rms_ns==UINT32_MAX&&m.delivery==UINT16_MAX&&rf_bars(m,now)==0);
 // The adapter already checks the exact current parent's authenticated RSSI.
 m.signal={s.server,now,1,-45};select_rc9(m,s,now);assert(rf_bars(m,now)==4);
 assert(rf_bars(m,now+SnapshotMaxUs)==0);
 m.signal.peer=99;select_rc9(m,s,now);assert(rf_bars(m,now)==0);
 assert(time(m,now,utc)&&analog_time(m,now,utc)&&saver_time(m,now,utc));
 assert(!time(m,now+SnapshotMaxUs,utc));
 // A valid RC9 stream never inherits an earlier BLE precision/lock or relay.
 m.precision.valid=m.precision.locked=m.serving=true;m.locked=false;select_rc9(m,s,now);
 assert(!m.precision.valid&&!m.precision.locked&&!m.serving);
 auto ble=m;ble.source=Source::Ble;ble.locked=true;select_rc9(ble,s,now);assert(ble.source==Source::Ble);
 auto lor=m;lor.source=Source::LoRa;select_rc9(lor,s,now);assert(lor.source==Source::LoRa);
 for(auto state:{State::Off,State::Fault}){auto bad=m;bad.state=state;bad.source=Source::None;select_rc9(bad,s,now);assert(bad.state==state&&bad.source==Source::None);}
 // All pages retain the one saver session and unchanged500ms total-loss exit.
 for(unsigned page=0;page<PageCount;page++){
  Input input;ClockHold hold;SaverTimer timer;View view;view.page=page;
  const auto at=IdleUs+1000;auto rc=m;rc.sampled_us=rc.utc_at_us=at;rc.until_us=at+SnapshotMaxUs;
  update_saver(input,hold,timer,rc,view,at);assert(view.saver);
  rc.valid=false;rc.sampled_us=at+10000;update_saver(input,hold,timer,rc,view,at+10000);assert(view.saver);
  rc.sampled_us=at+509999;update_saver(input,hold,timer,rc,view,at+509999);assert(view.saver);
  rc.sampled_us=at+510000;update_saver(input,hold,timer,rc,view,at+510000);assert(!view.saver);
 }
 uint8_t a[FrameBytes],b[FrameBytes];View view;view.page=PrecisionPage;render(a,m,view,now);m.source=Source::None;render(b,m,view,now);assert(std::memcmp(a,b,sizeof a));
}
