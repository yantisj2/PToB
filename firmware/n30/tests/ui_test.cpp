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
 for(unsigned page=0;page<n30::PageCount;page++)for(unsigned state=0;state<7;state++)for(bool help:{false,true})for(bool menu:{false,true}){
  n30::View v;v.page=page;v.help=help;v.menu.open=menu;v.menu.item=state%5;v.menu.hold_percent=100;m.lora_supported=true;m.state=n30::State(state);std::memset(m.unit,'X',24);m.unit[24]=0;std::memset(m.parent,'Y',24);m.parent[24]=0;
  n30::render(g.frame,m,v,1000001);for(auto b:g.before)assert(b==0xa5);for(auto b:g.after)assert(b==0xa5);
 }
 uint8_t native[1024],portrait[1024]{};
 for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++){
  std::memset(portrait,0,1024);portrait[y*8+x/8]|=0x80>>(x%8);
  for(bool cw:{false,true}){n30::rotate(native,portrait,cw);unsigned count=0;for(auto b:native)count+=__builtin_popcount(b);assert(count==1);unsigned nx=cw?127-y:y,ny=cw?x:63-x;assert(native[(ny/8)*128+nx]&(1<<(ny%8)));}
 }
 // Physical menu boundaries and power selection are covered by power_input_test.
 n30::Input held_at_boot(true);n30::View b;held_at_boot.update(true,5000000,b);held_at_boot.update(false,5100000,b);held_at_boot.update(false,5150000,b);assert(!b.menu.open&&!b.blank&&b.page==0);
 // Short presses complete the four-screen cycle.
 n30::Input cycle;n30::View cyc;
 for(unsigned n=1;n<=4;n++){auto at=uint64_t(n)*1000000;cycle.update(true,at,cyc);cycle.update(true,at+40000,cyc);cycle.update(false,at+100000,cyc);cycle.update(false,at+140000,cyc);assert(cyc.page==n%n30::PageCount);}
 n30::ActionMailbox mail;assert(mail.post(100));assert(!mail.post(101));assert(mail.take(102));assert(!mail.take(103));
 assert(mail.post(200));assert(!mail.take(199));assert(!mail.take(201));assert(mail.post(300));assert(!mail.take(300+n30::SnapshotMaxUs));
 m.sampled_us=m.utc_at_us=1000000;m.until_us=1250000;m.valid=m.locked=true;m.state=n30::State::Follow;
 for(auto source:{n30::Source::Ble,n30::Source::Rc1,n30::Source::Rc2,n30::Source::LoRa,n30::Source::Rc9}){
  m.source=source;assert(std::strcmp(n30::source_label(m,1000001),source==n30::Source::Ble?"BLE":source==n30::Source::Rc1?"RC1":source==n30::Source::Rc2?"RC2":source==n30::Source::Rc9?"RC9":"LOR")==0);
  for(unsigned pos=0;pos<3;pos++){
   n30::View saver;saver.saver=true;saver.saver_position=pos;n30::render(g.frame,m,saver,1000001);
   // A rigid75px T096-style assembly: strip, dial, identity. No dashboard.
   const unsigned top=4+22*pos;
   for(unsigned y=0;y<128;y++)if(y<top||y>=top+75)for(unsigned x=0;x<8;x++)assert(!g.frame[y*8+x]);
   for(auto z:g.before)assert(z==0xa5);for(auto z:g.after)assert(z==0xa5);
  }
 }
 m.source=n30::Source::LoRa;assert(!std::strcmp(n30::source_label(m,1250000),"WAIT"));m.mode_pending=true;assert(!std::strcmp(n30::source_label(m,1000001),"LOR"));m.source=n30::Source::Ble;assert(!std::strcmp(n30::source_label(m,1000001),"WAIT"));m.mode_pending=false;
 m.state=n30::State::Hold;assert(!std::strcmp(n30::source_label(m,1000001),"HOLD"));
 n30::Activity a;uint32_t c[8]{};a.update(m,c,1,2,1000000,true,false);c[2]++;a.update(m,c,1,2,1000001,true,false);assert(m.lamps[2]==1);a.update(m,c,1,3,1000002,true,false);assert(!m.lamps[2]);a.update(m,c,1,3,1000003,false,false);for(auto l:m.lamps)assert(!l);
}
