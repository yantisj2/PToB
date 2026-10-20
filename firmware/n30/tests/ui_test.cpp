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
 for(unsigned page=0;page<2;page++)for(unsigned state=0;state<7;state++)for(bool help:{false,true}){
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
 n30::Activity a;uint32_t c[8]{};a.update(m,c,1,2,1000000,true,false);c[2]++;a.update(m,c,1,2,1000001,true,false);assert(m.lamps[2]==1);a.update(m,c,1,3,1000002,true,false);assert(!m.lamps[2]);a.update(m,c,1,3,1000003,false,false);for(auto l:m.lamps)assert(!l);
}
