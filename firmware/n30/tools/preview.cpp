// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Production renderer with illustrative values, never a hardware capture.
#include "ui.hpp"
#include <cstdio>
#include <cstring>
int main(int argc,char** argv){
 if(argc!=3)return 2;n30::Model m;std::snprintf(m.unit,sizeof m.unit,"%s",argv[2]);
 std::snprintf(m.parent,sizeof m.parent,"TC-02");m.sampled_us=m.utc_at_us=1000000;m.until_us=1250000;
 m.utc_us=10ULL*3600000000+10ULL*60000000+42000000;m.valid=m.locked=true;
 m.state=n30::State::Follow;m.source=n30::Source::Ble;m.rms_ns=65;m.hz=16;m.delivery=950;
 m.lamps[0]=m.lamps[2]=m.lamps[4]=1;m.lamps[3]=1;
 const auto fixture_utc=m.utc_us;
 uint8_t frame[n30::FrameBytes];n30::View view;
 const char* names[]={"clock","rf","saver-ble","saver-rc1","saver-rc2","clock-relay","clock-pending","clock-grace","clock-syncing"};
 for(unsigned i=0;i<9;i++){
  view={};view.page=i==1;view.saver=i>=2&&i<=4;view.saver_position=view.saver?i-2:0;
  m.source=i==3?n30::Source::Rc1:i==4?n30::Source::Rc2:n30::Source::Ble;
  m.relay_only=i==5;m.mode_pending=i==6;if(m.mode_pending)m.valid=m.locked=false;
  uint64_t now=1000000;
  if(i>=7){
   n30::DialHold hold;m.valid=m.locked=true;m.mode_pending=false;
   m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us=fixture_utc;
   hold.update(m,view,now);hold.presented(m,view,now);m.locked=false;
   hold.update(m,view,1100000);now=i==7?1200000:1600000;
   m.sampled_us=m.utc_at_us=now;m.until_us=now+250000;m.utc_us+=now-1000000;
   hold.update(m,view,now);
  }
  n30::render(frame,m,view,now);char path[1024];std::snprintf(path,sizeof path,"%s/%s.pgm",argv[1],names[i]);
  auto f=std::fopen(path,"wb");if(!f)return 3;std::fprintf(f,"P5\n64 128\n255\n");
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);
  std::fclose(f);
 }
}
