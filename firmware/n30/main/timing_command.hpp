// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
namespace n30::timing {
enum class Command:uint8_t {Invalid,Start,Read,Stop};
struct Request {Command command=Command::Invalid;uint32_t duration=0,every=0,after=0;};
inline bool number(const char*& p,uint32_t& n){
 if(*p<'0'||*p>'9')return false;
 n=0;
 do{const auto digit=unsigned(*p-'0');if(n>(UINT32_MAX-digit)/10){return false;}n=n*10+digit;++p;}while(*p>='0'&&*p<='9');
 return true;
}
inline Request parse(const char* text){
 Request r;
 if(!std::strcmp(text,"timing stop")){r.command=Command::Stop;return r;}
 if(!std::strncmp(text,"timing read ",12)){
  const char* p=text+12;
  if(number(p,r.after)&&!*p)r.command=Command::Read;
  return r;
 }
 if(!std::strncmp(text,"timing start ",13)){
  const char* p=text+13;
  if(number(p,r.duration)&&*p==' '){++p;if(number(p,r.every)&&!*p&&r.duration>=1000&&r.duration<=600000&&r.every>=1&&r.every<=64)r.command=Command::Start;}
 }
 return r;
}
}
