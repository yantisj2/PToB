// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
int main(int argc,char** argv){if(argc!=3)return 2;watch::Model m;m.ble_state=watch::RadioState::On;m.lora_state=watch::RadioState::On;m.time_valid=true;m.utc_us=1794679698000000ULL;m.sampled_us=1000000;m.until_us=2000000;m.rms_ns=87;m.locked=true;m.source=watch::Source::BLE;m.hold_valid=true;m.hold_ms=58300;m.hold_trials=100;m.hold_lower=false;m.lora_on=true;m.lora_receiving=true;m.battery=84;m.uptime_s=91800;watch::View v;
 if(!std::strcmp(argv[1],"instrument"))v.settings.face=1;
 if(!std::strcmp(argv[1],"instrument-lower")){v.settings.face=1;m.hold_lower=true;}
 if(!std::strcmp(argv[1],"instrument-lora")){v.settings.face=1;m.source=watch::Source::LOR;m.hold_valid=false;}
 if(!std::strcmp(argv[1],"instrument-acquire")){v.settings.face=1;m.locked=false;m.rms_ns=UINT32_MAX;m.hold_valid=false;m.lora_on=false;}
 if(!std::strcmp(argv[1],"instrument-noon")){v.settings.face=1;m.utc_us=1794686400000000ULL;}
 if(!std::strcmp(argv[1],"instrument-wait")){v.settings.face=1;m.time_valid=false;m.hold_valid=false;m.battery=-1;}
 if(!std::strcmp(argv[1],"instrument-hold")){v.settings.face=1;m.local_time=true;m.holding=true;m.locked=false;}
 if(!std::strcmp(argv[1],"instrument-over")){v.settings.face=1;m.rms_ns=10001;m.hold_ms=61000;}
 if(!std::strcmp(argv[1],"instrument-rc9")){v.settings.face=1;m.source=watch::Source::RC9;m.hold_valid=false;m.locked=false;m.rms_ns=UINT32_MAX;}
 if(v.settings.face==1)m.utc_us-=8ULL*3600*1000000;else {m.tz_id=2;m.tz_minutes=-360;}
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
 if(!std::strncmp(argv[1],"solar",5)){
  v.settings.face=2;m.source_rssi=-64;m.tz_id=2;m.tz_minutes=-360;
  auto day=watch::calendar::solar({2026,11,15});
  uint64_t utc=day.noon;
  if(!std::strncmp(argv[1],"solar-month-",12)){const unsigned month=unsigned(std::atoi(argv[1]+12));if(month<1||month>12)return 4;utc=watch::calendar::midnight({2026,month,15})+18*3600;}
  if(!std::strcmp(argv[1],"solar-summer"))utc=watch::calendar::solar({2026,6,21}).sunset-600;
  if(!std::strcmp(argv[1],"solar-winter"))utc=watch::calendar::solar({2026,12,21}).sunset-600;
  if(!std::strcmp(argv[1],"solar-spring"))utc=watch::calendar::solar({2026,3,20}).sunset-600;
  if(!std::strcmp(argv[1],"solar-autumn"))utc=watch::calendar::solar({2026,9,22}).sunset-600;
  if(!std::strcmp(argv[1],"solar-dst-start-before"))utc=watch::calendar::midnight({2026,3,8})+8*3600-1;
  if(!std::strcmp(argv[1],"solar-dst-start-after"))utc=watch::calendar::midnight({2026,3,8})+8*3600;
  if(!std::strcmp(argv[1],"solar-dst-end-before"))utc=watch::calendar::midnight({2026,11,1})+7*3600-1;
  if(!std::strcmp(argv[1],"solar-dst-end-after"))utc=watch::calendar::midnight({2026,11,1})+7*3600;
  if(!std::strcmp(argv[1],"solar-dawn"))utc=day.dawn+300;
  if(!std::strcmp(argv[1],"solar-sunset"))utc=day.sunset-600;
  if(!std::strcmp(argv[1],"solar-night"))utc=day.noon+11*3600;
  if(!std::strcmp(argv[1],"solar-midnight"))utc=day.noon+12*3600;
  if(!std::strcmp(argv[1],"solar-morning"))utc=day.noon-3*3600;
  if(!std::strcmp(argv[1],"solar-afternoon"))utc=day.noon+3*3600;
  if(!std::strcmp(argv[1],"solar-civil"))utc=day.dusk;
  if(!std::strcmp(argv[1],"solar-nautical"))utc=day.nautical_dusk;
  if(!std::strcmp(argv[1],"solar-astronomical"))utc=day.astronomical_dusk;
  if(!std::strcmp(argv[1],"solar-new"))utc=watch::calendar::midnight({2026,11,9})+18*3600;
  if(!std::strcmp(argv[1],"solar-full"))utc=watch::calendar::midnight({2026,11,24})+18*3600;
  if(!std::strcmp(argv[1],"solar-expired"))utc=watch::calendar::midnight({2028,3,1})+18*3600;
  if(!std::strcmp(argv[1],"solar-wait"))m.time_valid=false;
  if(!std::strcmp(argv[1],"solar-hold")){m.local_time=true;m.holding=true;m.locked=false;}
  if(!std::strcmp(argv[1],"solar-over")){m.rms_ns=10001;m.hold_lower=true;m.source_rssi=-108;}
  m.utc_us=utc*1000000;m.calendar=watch::calendar::snapshot(utc,m.time_valid,m.local_time);
 }
 uint64_t now=1000000;
 static watch::BatteryHistory history;
 if(!std::strncmp(argv[1],"battery",7)){
  v.screen=watch::Screen::Battery;now=25*watch::BatteryHistory::Hour+23*watch::BatteryHistory::Minute;m.uptime_s=now/1000000;m.battery_history=&history;
  if(!std::strcmp(argv[1],"battery-6h"))v.battery_hours=6;
  if(!std::strcmp(argv[1],"battery-12h"))v.battery_hours=12;
  if(!std::strcmp(argv[1],"battery-24h"))v.battery_hours=24;
  const bool empty=!std::strcmp(argv[1],"battery-empty"),missing=!std::strcmp(argv[1],"battery-unavailable"),low=!std::strcmp(argv[1],"battery-low"),usb=!std::strcmp(argv[1],"battery-usb"),gaps=!std::strcmp(argv[1],"battery-gaps");
  if(empty){now=18000000;m.uptime_s=18;m.battery=-1;history.observe(now,false);}
  else if(missing){m.battery_history=nullptr;m.battery=-1;}
  else{
   history.observe(0,false);
   for(uint64_t t=0;t<=now;t+=30000000){const uint64_t minute=t/watch::BatteryHistory::Minute;const bool on=minute%11<2||minute%23==0;
    history.observe(t,on);int level=low?4:usb?std::min(100,60+int(t*36/now)):98-int(t*14/now);
    if(gaps&&minute%19<5)level=-1;
    history.sample(t,level,usb);
   }
   history.observe(now,false);m.battery=low?4:usb?96:84;m.charging=usb;
  }
 }
 if(!std::strncmp(argv[1],"shadow",6)){
  v.settings.face=3;auto& s=m.shadow;s.sampled_us=now;s.powered=true;s.stage=watch::ShadowStage::Measuring;s.fix=true;s.pps=true;s.last_pps_us=now-50000;s.fix_dimension=3;s.in_view=18;s.used=11;s.satellite_count=12;s.valid=true;s.samples=128;s.mean_tenth_ns=874;s.p99_ns=246;s.rms_ns=112;s.settling_ms=10000;
  for(unsigned i=0;i<12;++i)s.satellites[i]={uint16_t(i+1),uint16_t((i*137)%360),uint8_t(10+(i*23)%75),uint8_t(22+(i*7)%30),'G'};
  if(!std::strcmp(argv[1],"shadow-search")){s.stage=watch::ShadowStage::Searching;s.fix=s.pps=s.valid=false;s.in_view=s.used=s.satellite_count=0;}
  if(!std::strcmp(argv[1],"shadow-fix")){s.stage=watch::ShadowStage::WaitPps;s.pps=s.valid=false;}
  if(!std::strcmp(argv[1],"shadow-settle")){s.stage=watch::ShadowStage::Settling;s.valid=false;s.samples=0;s.settling_ms=6000;}
  if(!std::strcmp(argv[1],"shadow-mesh")){s.stage=watch::ShadowStage::WaitMesh;s.valid=false;m.time_valid=false;}
  if(!std::strcmp(argv[1],"shadow-lost")){s.stage=watch::ShadowStage::Lost;s.pps=s.fix=s.valid=false;}
  if(!std::strcmp(argv[1],"shadow-fault")){s.stage=watch::ShadowStage::Fault;s.pps=s.fix=s.valid=false;s.powered=false;s.used=s.in_view=s.satellite_count=0;}
  if(!std::strcmp(argv[1],"shadow-lora"))m.source=watch::Source::LOR;
  if(!std::strcmp(argv[1],"shadow-hold")){m.local_time=true;m.holding=true;}
  if(!std::strcmp(argv[1],"shadow-invalid-sky"))for(unsigned i=0;i<12;++i)s.satellites[i].position_known=false;
  if(!std::strcmp(argv[1],"shadow-tail")){s.mean_tenth_ns=-12345678;s.p99_ns=500000000;s.approximate=true;s.rms_ns=250000000;s.samples=UINT32_MAX;}
 }
 // Common-clock UI regressions use synthetic receiver metrics, never measurements.
 if(!std::strncmp(argv[1],"metrics-",8)){
  v.settings.face=uint8_t(std::atoi(argv[1]+8));if(v.settings.face>=watch::FaceCount)return 4;
  m.common_clock=true;m.rms_source=watch::Source::BLE;m.rms_ns=51;m.rms_sampled_us=now;m.rms_until_us=now+250000;
  m.hold_sampled_us=now;m.hold_until_us=now+250000;
  if(std::strstr(argv[1],"-lora")){m.source=watch::Source::LOR;m.locked=false;m.rms_source=watch::Source::LOR;m.rms_ns=3599390;m.hold_valid=false;}
  if(std::strstr(argv[1],"-hold")){m.source=watch::Source::None;m.holding=m.local_time=true;m.locked=false;}
  if(std::strstr(argv[1],"-expired")){m.rms_until_us=now;m.hold_until_us=now;}
  if(std::strstr(argv[1],"-maximum"))m.rms_ns=UINT32_MAX-1;
  if(std::strstr(argv[1],"-zero"))m.rms_ns=0;
  m.calendar=watch::calendar::snapshot(m.utc_us/1000000,true,m.local_time);
 }
 static uint16_t frame[watch::Width*watch::Height];watch::render(frame,m,v,now);
 FILE* f=std::fopen(argv[2],"wb");if(!f)return 3;std::fprintf(f,"P6\n%d %d\n255\n",watch::Width,watch::Height);for(auto p:frame){unsigned char c[]={uint8_t((p>>11)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};std::fwrite(c,1,3,f);}std::fclose(f);
}
