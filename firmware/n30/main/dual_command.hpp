// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "timing_command.hpp"
namespace n30 {
inline bool dual_cursors(const char* text,uint32_t& after,uint32_t& parent){
 if(std::strncmp(text,"dual read ",10))return false;
 const char* p=text+10;const char* start=p;
 if(!timing::number(p,after)||(p-start>1&&*start=='0')||*p!=' ')return false;
 start=++p;
 return timing::number(p,parent)&&!(p-start>1&&*start=='0')&&!*p;
}
}
