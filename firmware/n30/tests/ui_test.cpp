// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include "display_budget.hpp"
#include <cassert>
#include <initializer_list>
#include <cstring>
int main(){
 for(uint64_t left=0;left<=50000;left++)for(size_t bytes:{size_t(4),size_t(129)}){
  const auto budget=n30::transfer_timeout(100000,100000-left,bytes);
  if(budget){assert(budget<=10&&uint64_t(budget)*1000+1000<=left);const uint64_t wire=(bytes*9*1000000+399999)/400000;assert(uint64_t(budget)*1000>=wire+1000);}
 }
 assert(!n30::transfer_timeout(1,2,4));assert(!n30::transfer_timeout(1,0,130));
 n30::Model m;m.sampled_us=m.utc_at_us=1000000;m.until_us=1250000;m.utc_us=123456789;m.valid=m.locked=true;m.state=n30::State::Follow;
 uint64_t utc;assert(n30::time(m,1000100,utc)&&utc==123456889);assert(!n30::time(m,999999,utc));assert(!n30::time(m,1250000,utc));
 m.state=n30::State::Fault;assert(!n30::time(m,1000100,utc));m.state=n30::State::Follow;
 struct Guard {uint8_t before[32],frame[n30::FrameBytes],after[32];} g;std::memset(&g,0xa5,sizeof g);
 for(unsigned page=0;page<3;page++)for(unsigned state=0;state<7;state++)for(bool help:{false,true}){
  n30::View v;v.page=page;v.help=help;m.state=n30::State(state);std::memset(m.unit,'X',24);m.unit[24]=0;std::memset(m.parent,'Y',24);m.parent[24]=0;
  n30::render(g.frame,m,v,1000001);for(auto b:g.before)assert(b==0xa5);for(auto b:g.after)assert(b==0xa5);
 }
 uint8_t native[1024],portrait[1024]{};
 for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++){
  std::memset(portrait,0,1024);portrait[y*8+x/8]|=0x80>>(x%8);
  for(bool cw:{false,true}){n30::rotate(native,portrait,cw);unsigned count=0;for(auto b:native)count+=__builtin_popcount(b);assert(count==1);unsigned nx=cw?127-y:y,ny=cw?x:63-x;assert(native[(ny/8)*128+nx]&(1<<(ny%8)));}
 }
 n30::View v;n30::Input input;
 input.update(true,100000,v);input.update(true,150000,v);input.update(false,200000,v);input.update(false,250000,v);assert(v.page==1);
 input.update(true,300000,v);input.update(true,350000,v);input.update(true,1850000,v);assert(v.help&&v.page==1);input.update(false,1900000,v);input.update(false,1950000,v);assert(v.help);
 input.update(true,2000000,v);input.update(true,2050000,v);input.update(false,2100000,v);input.update(false,2150000,v);assert(!v.help&&v.page==1);
 input.idle(302150000,true,v);assert(v.saver);input.update(true,302200000,v);input.update(true,302250000,v);assert(!v.saver);input.update(false,302300000,v);input.update(false,302350000,v);assert(v.page==1);
 input.update(true,303000000,v);input.update(true,303050000,v);input.update(true,307050000,v);assert(v.blank);input.update(false,307100000,v);input.update(false,307150000,v);
 input.update(true,307200000,v);input.update(true,307250000,v);assert(!v.blank);input.update(false,307300000,v);input.update(false,307350000,v);assert(v.page==1);
 n30::Input held_at_boot(true);n30::View b;held_at_boot.update(true,5000000,b);held_at_boot.update(false,5100000,b);held_at_boot.update(false,5150000,b);assert(!b.help&&!b.blank&&b.page==0);
 // W12 main gesture: one1.5s toggle, consumed even beyond4s and on release.
 n30::Input home;n30::View h;
 assert(home.update(true,100000,h)==n30::Action::None);home.update(true,140000,h);
 assert(home.update(true,1599999,h)==n30::Action::None);
 assert(home.update(true,1600000,h)==n30::Action::ToggleRelayOnly);
 assert(home.update(true,5000000,h)==n30::Action::None&&!h.blank&&!h.help&&h.page==0);
 home.update(false,5100000,h);assert(home.update(false,5140000,h)==n30::Action::None&&h.page==0);
 home.update(true,5200000,h);home.update(true,5240000,h);assert(home.update(true,6700000,h)==n30::Action::ToggleRelayOnly);
 home.update(false,6800000,h);home.update(false,6840000,h);assert(h.page==0);
 // Release debounce may be the first observation after the threshold.
 n30::Input release;n30::View rel;release.update(true,100000,rel);release.update(true,140000,rel);release.update(false,1590000,rel);
 assert(release.update(false,1630000,rel)==n30::Action::ToggleRelayOnly&&rel.page==0);
 // Page changes during a press cannot reclassify an RF hold into a toggle.
 n30::Input pinned;n30::View p;p.page=1;pinned.update(true,100000,p);pinned.update(true,140000,p);p.page=0;
 assert(pinned.update(true,1600000,p)==n30::Action::None);
 // A screensaver wake consumes a long press too; it cannot change the mode.
 n30::Input wake;n30::View w;w.saver=true;wake.update(true,100000,w);wake.update(true,140000,w);
 assert(wake.update(true,5000000,w)==n30::Action::None&&!w.blank&&!w.help&&!w.saver);
 // Third-page hold is one-shot, never RF help/blank, and cancels on navigation.
 n30::Input third;n30::View t;t.page=2;third.update(true,100000,t);third.update(true,140000,t);
 assert(third.update(true,1599999,t)==n30::Action::None);
 assert(third.update(true,1600000,t)==n30::Action::ToggleLoRa);
 assert(third.update(true,5000000,t)==n30::Action::None&&!t.blank&&!t.help);
 third.update(false,5100000,t);third.update(false,5140000,t);assert(t.page==2);
 n30::Input cancelled;n30::View cv;cv.page=2;cancelled.update(true,100000,cv);cancelled.update(true,140000,cv);cv.page=0;
 assert(cancelled.update(true,150000,cv)==n30::Action::None);cv.page=2;
 assert(cancelled.update(true,1600000,cv)==n30::Action::None);
 cancelled.update(false,5100000,cv);assert(cancelled.update(false,5140000,cv)==n30::Action::None&&cv.page==2);
 // Short presses complete the three-screen cycle.
 n30::Input cycle;n30::View cyc;
 for(unsigned n=1;n<=3;n++){auto at=uint64_t(n)*1000000;cycle.update(true,at,cyc);cycle.update(true,at+40000,cyc);cycle.update(false,at+100000,cyc);cycle.update(false,at+140000,cyc);assert(cyc.page==n%3);}
 n30::ActionMailbox mail;assert(mail.post(100));assert(!mail.post(101));assert(mail.take(102));assert(!mail.take(103));
 assert(mail.post(200));assert(!mail.take(199));assert(!mail.take(201));assert(mail.post(300));assert(!mail.take(300+n30::SnapshotMaxUs));
 m.sampled_us=m.utc_at_us=1000000;m.until_us=1250000;m.valid=m.locked=true;m.state=n30::State::Follow;
 for(auto source:{n30::Source::Ble,n30::Source::Rc1,n30::Source::Rc2,n30::Source::LoRa}){
  m.source=source;assert(std::strcmp(n30::source_label(m,1000001),source==n30::Source::Ble?"BLE":source==n30::Source::Rc1?"RC1":source==n30::Source::Rc2?"RC2":"LOR")==0);
  for(unsigned pos=0;pos<3;pos++){
   n30::View saver;saver.saver=true;saver.saver_position=pos;n30::render(g.frame,m,saver,1000001);
   // A rigid75px T096-style assembly: strip, dial, identity. No dashboard.
   const unsigned top=4+22*pos;
   for(unsigned y=0;y<128;y++)if(y<top||y>=top+75)for(unsigned x=0;x<8;x++)assert(!g.frame[y*8+x]);
   for(auto z:g.before)assert(z==0xa5);for(auto z:g.after)assert(z==0xa5);
  }
 }
 assert(!std::strcmp(n30::source_label(m,1250000),"WAIT"));m.mode_pending=true;assert(!std::strcmp(n30::source_label(m,1000001),"LOR"));m.source=n30::Source::Ble;assert(!std::strcmp(n30::source_label(m,1000001),"WAIT"));m.mode_pending=false;
 m.state=n30::State::Hold;assert(!std::strcmp(n30::source_label(m,1000001),"HOLD"));
 n30::Activity a;uint32_t c[8]{};a.update(m,c,1,2,1000000,true,false);c[2]++;a.update(m,c,1,2,1000001,true,false);assert(m.lamps[2]==1);a.update(m,c,1,3,1000002,true,false);assert(!m.lamps[2]);a.update(m,c,1,3,1000003,false,false);for(auto l:m.lamps)assert(!l);
}
