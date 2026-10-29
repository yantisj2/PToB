// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define W12_DISPLAY_PREVIEW 1
#include "../legacy/display.cpp"
#include <cassert>
#include <fstream>
#include <string>
static void save(const char* dir,const char* name){
 std::ofstream f(std::string(dir)+"/"+name+".pbm");f<<"P1\n128 64\n";
 for(unsigned y=0;y<64;y++){for(unsigned x=0;x<128;x++)f<<bool(fb[y/8*128+x]&(1<<(y%8)))<<' ';f<<'\n';}
}
int main(int argc,char** argv){
 assert(argc==2);DisplayState s;s.r2_dashboard=s.valid=s.fit=true;s.utc=7200;s.tz={2,-300};s.tz_sampled_us=1000000;s.tz_until_us=1750000;
 s.timing_source=DisplayState::Ble;s.unit_label="C-2";s.followed_clock="TC-03";
 assert(shown_clock(s)==-10800&&!std::strcmp(shown_zone(s),"CDT"));
 display_show(s);save(argv[1],"w12-clock-cdt");uint8_t actual[sizeof fb];std::memcpy(actual,fb,sizeof fb);
 auto ref=s;ref.tz_until_us=0;ref.utc=75600;display_show(ref);
 // Clock/date boundary and analog use the same signed offset. Compare dashboard below header.
 assert(!std::memcmp(actual+128*2,fb+128*2,sizeof fb-128*2));
 s.tz_sampled_us=1750000;assert(shown_clock(s)==7200&&!std::strcmp(shown_zone(s),"UTC"));display_show(s);save(argv[1],"w12-clock-expired-utc");
 s.tz_sampled_us=1000000;s.tz={42,345};assert(shown_clock(s)==27900&&!std::strcmp(shown_zone(s),"LOC"));
 s.tz={2,-360};assert(shown_clock(s)==-14400&&!std::strcmp(shown_zone(s),"CST"));
 for(auto source:{DisplayState::Lora,DisplayState::Rc9}){s.timing_source=source;assert(shown_clock(s)==7200&&!std::strcmp(shown_zone(s),"UTC"));}
 int64_t shown;assert(!std::strcmp(v12::clock_zone({42,345},1750000,1000000,INT64_MAX,shown),"UTC")&&shown==INT64_MAX);
 assert(s.utc==7200);puts("PASS W12 local digital/analog/calendar helper, lease expiry, signed midnight, unknown label, overflow and UTC fallback");
}
