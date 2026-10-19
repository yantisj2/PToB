// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "m5stick_s3/ui.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
int main(int argc,char** argv){
 uint8_t frame[sticks3::FrameBytes]{},strip[sticks3::StripBytes]{};sticks3::RenderAudit audit;
 sticks3::render_splash(frame,&audit);assert(!audit.clipped_pixels&&!audit.text_overflow);
 for(unsigned row=0;row<sticks3::Height;row+=sticks3::StripRows){
  sticks3::render_splash_strip(strip,row);assert(!std::memcmp(strip,frame+row*sticks3::Width*2,sizeof strip));
 }
 if(argc==2){std::ofstream out(argv[1],std::ios::binary);out<<"P6\n135 240\n255\n";
  for(unsigned i=0;i<sizeof frame;i+=2){unsigned c=frame[i]*256+frame[i+1];char rgb[]{char((c>>11)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};out.write(rgb,3);}}
}
