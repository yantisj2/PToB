// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){if(argc!=3)return 2;watch::Model m;m.time_valid=true;m.utc_us=1794679698000000ULL;m.sampled_us=1000000;m.until_us=2000000;m.rms_ns=87;m.locked=true;m.source=watch::Source::BLE;m.hold_valid=true;m.hold_ms=58300;m.hold_trials=100;m.hold_lower=false;m.lora_on=true;m.battery=84;m.uptime_s=91800;watch::View v;
 if(!std::strcmp(argv[1],"instrument"))v.settings.face=1;
 if(!std::strcmp(argv[1],"instrument-lora")){v.settings.face=1;m.source=watch::Source::LOR;m.hold_valid=false;}
 if(!std::strcmp(argv[1],"instrument-acquire")){v.settings.face=1;m.locked=false;m.rms_ns=UINT32_MAX;m.hold_valid=false;m.lora_on=false;}
 if(!std::strcmp(argv[1],"instrument-noon")){v.settings.face=1;m.utc_us=1794686400000000ULL;}
 if(!std::strcmp(argv[1],"instrument-wait")){v.settings.face=1;m.time_valid=false;m.hold_valid=false;m.battery=-1;}
 if(!std::strcmp(argv[1],"instrument-hold")){v.settings.face=1;m.local_time=true;m.holding=true;m.locked=false;}
 if(!std::strcmp(argv[1],"instrument-over")){v.settings.face=1;m.rms_ns=10001;m.hold_ms=61000;}
 if(!std::strcmp(argv[1],"instrument-rc9")){v.settings.face=1;m.source=watch::Source::RC9;m.hold_valid=false;m.locked=false;m.rms_ns=UINT32_MAX;}
 if(v.settings.face==1)m.utc_us-=8ULL*3600*1000000;
 if(!std::strcmp(argv[1],"settings"))v.screen=watch::Screen::Settings;
 if(!std::strcmp(argv[1],"launcher"))v.screen=watch::Screen::Launcher;
 if(!std::strcmp(argv[1],"faces"))v.screen=watch::Screen::Faces;
 if(!std::strcmp(argv[1],"waiting"))m.time_valid=false;
 if(!std::strcmp(argv[1],"lora")){m.source=watch::Source::LOR;m.rms_ns=731;m.hold_valid=false;}
 if(!std::strcmp(argv[1],"hold")){m.holding=true;m.locked=false;}
 if(!std::strcmp(argv[1],"maximum")){m.rms_ns=UINT32_MAX-1;m.hold_ms=UINT32_MAX;}
 if(!std::strcmp(argv[1],"acquire")){m.locked=false;m.rms_ns=UINT32_MAX;m.hold_valid=false;}
 if(!std::strcmp(argv[1],"save-error")){v.screen=watch::Screen::Settings;v.save_failed=true;}
 if(!std::strcmp(argv[1],"sleep"))v.asleep=true;
 static uint16_t frame[watch::Width*watch::Height];watch::render(frame,m,v,1000000);
 FILE* f=std::fopen(argv[2],"wb");if(!f)return 3;std::fprintf(f,"P6\n%d %d\n255\n",watch::Width,watch::Height);for(auto p:frame){unsigned char c[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(c,1,3,f);}std::fclose(f);
}
