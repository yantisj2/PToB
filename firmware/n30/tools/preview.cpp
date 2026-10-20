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
 m.state=n30::State::Follow;m.rms_ns=65;m.hz=16;m.delivery=950;
 m.lamps[0]=m.lamps[2]=m.lamps[4]=1;m.lamps[3]=1;
 uint8_t frame[n30::FrameBytes];n30::View view;
 for(unsigned page=0;page<2;page++){
  view.page=page;n30::render(frame,m,view,1000000);char path[1024];std::snprintf(path,sizeof path,"%s/%s.pgm",argv[1],page?"rf":"clock");
  auto f=std::fopen(path,"wb");if(!f)return 3;std::fprintf(f,"P5\n64 128\n255\n");
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(frame[y*8+x/8]&(0x80>>(x%8))?255:0,f);
  std::fclose(f);
 }
}
