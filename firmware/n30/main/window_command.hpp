// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "timing_command.hpp"
namespace n30::window {
enum class Command {Invalid,Start,Packet,Edges,Late,Ack,Stop};
struct Request {Command command=Command::Invalid;uint32_t duration=0,count=0,cursor=0;};
inline Request parse(const char* s){
 Request r;if(!s||std::strlen(s)>=80||std::strncmp(s,"window ",7))return r;s+=7;
 if(!std::strcmp(s,"packet")){r.command=Command::Packet;return r;}
 if(!std::strcmp(s,"stop")){r.command=Command::Stop;return r;}
 if(!std::strncmp(s,"start ",6)){s+=6;
  if(timing::number(s,r.duration)&&*s==' '){++s;if(timing::number(s,r.count)&&!*s&&r.duration>=1000&&r.duration<=600000&&r.count>=1&&r.count<=128)r.command=Command::Start;}return r;
 }
 if(!std::strncmp(s,"edges ",6)){s+=6;if(timing::number(s,r.cursor)&&!*s)r.command=Command::Edges;return r;}
 if(!std::strncmp(s,"late ",5)){s+=5;if(timing::number(s,r.cursor)&&!*s&&r.cursor<=128)r.command=Command::Late;return r;}
 if(!std::strncmp(s,"ack ",4)){s+=4;if(timing::number(s,r.cursor)&&!*s&&r.cursor>=1&&r.cursor<=128)r.command=Command::Ack;}
 return r;
}
}
