// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
static void save(const char* dir,const char* name,const uint8_t* f){
 std::string path=std::string(dir)+"/"+name+".pgm";FILE* p=std::fopen(path.c_str(),"wb");assert(p);
 std::fprintf(p,"P5\n64 128\n255\n");for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(f[y*8+x/8]&(0x80>>(x%8))?255:0,p);std::fclose(p);
}
int main(int argc,char** argv){
 assert(argc==2);constexpr uint64_t at=1000000;
 n30::Model m;m.sampled_us=m.utc_at_us=at;m.until_us=at+250000;m.utc_us=7200125000;
 m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;m.tz={2,-300};m.tz_until_us=at+200000;
 uint64_t local=0;assert(!std::strcmp(n30::clock_zone(m,at,m.utc_us,local),"CDT")&&local==75600125000);
 uint8_t a[1024],b[1024];n30::View v;
 n30::render(a,m,v,at);save(argv[1],"n30-clock-cdt",a);
 auto reference=m;reference.tz_until_us=0;reference.utc_us=local;
 n30::render(b,reference,v,at);
 // Exact digital and analog pixels equal an independently shifted UTC clock.
 assert(!std::memcmp(a+23*8,b+23*8,69*8));
 n30::render(b,reference,v,at);assert(std::memcmp(a+12*8,b+12*8,11*8)); // Zone strip changed.
 assert(m.utc_us==7200125000); // Immutable UTC model.
 for(unsigned pos=0;pos<3;pos++){
  v.saver=true;v.saver_position=pos;n30::render(a,m,v,at);n30::render(b,reference,v,at);
  assert(!std::memcmp(a+(17+22*pos)*8,b+(17+22*pos)*8,49*8));
  if(!pos)save(argv[1],"n30-saver-cdt",a);
 }
 v={};n30::render(a,m,v,at+200000);auto utc=m;utc.tz_until_us=0;n30::render(b,utc,v,at+200000);assert(!std::memcmp(a,b,1024));save(argv[1],"n30-clock-expired-utc",a);
 for(auto source:{n30::Source::LoRa,n30::Source::Rc9}){m.source=source;assert(!std::strcmp(n30::clock_zone(m,at,m.utc_us,local),"UTC")&&local==m.utc_us);}
 m.source=n30::Source::Ble;m.tz={42,345};assert(!std::strcmp(n30::clock_zone(m,at,0,local),"LOC")&&local==20700000000);
 m.tz={2,-360};assert(!std::strcmp(n30::clock_zone(m,at,0,local),"CST")&&local==64800000000);
 // Local pixels cannot remain in the original 500 ms timing-loss cache after TZ expiry.
 n30::ClockHold hold;m.tz={2,-300};m.tz_until_us=at+200000;
 hold.update(m,v,at);hold.draw(a,m,v,at);hold.presented(m,v,at,a);
 m.valid=m.locked=false;m.state=n30::State::Acquire;m.sampled_us=at+100000;
 hold.update(m,v,at+100000);assert(v.clock_frozen);
 m.sampled_us=at+200000;hold.update(m,v,at+200000);assert(!v.clock_frozen);
 // RF diagnostics retain UTC and its difference calculations.
 m.valid=m.locked=true;m.state=n30::State::Follow;m.sampled_us=at;m.tz_until_us=at+200000;v.page=1;
 n30::render(a,m,v,at);m.tz_until_us=0;n30::render(b,m,v,at);assert(!std::memcmp(a,b,1024));
 puts("PASS N30 local digital/analog/saver, midnight, fixed lease, cached-pixel expiry, numeric unknown IDs and UTC diagnostics/fallback");
}
