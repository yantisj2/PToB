// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "watch.hpp"
namespace watch {
struct TouchStroke {
 int x0=0,y0=0,x1=0,y1=0;uint64_t began=0,ended=0;uint32_t context=0;
 bool apply(View& view,uint32_t current,uint64_t now)const{
  if(context!=current||now<ended||now-ended>1500000)return false;
  Gesture g;bool changed=g.update(view,true,x0,y0,began);
  g.update(view,true,x1,y1,ended);changed|=g.update(view,false,0,0,ended);
  if(changed)view.activity_us=now;
  return changed;
 }
};
// Compress motion into first/last positions so a slow render cannot overflow a
// sample queue. Forty milliseconds without contact tolerates inter-report gaps.
struct TouchCapture {
 TouchStroke stroke{};bool active=false,blocked=false;uint64_t last_contact=0,last_valid=0,last=0;
 bool update(bool valid,bool contact,int x,int y,uint32_t context,uint64_t now,TouchStroke& out){
  if(now<last){*this={};blocked=true;}last=now;
  if(!valid){if(active&&now-last_valid>100000){active=false;blocked=true;}return false;}
  last_valid=now;
  if(blocked){if(!contact)blocked=false;return false;}
  if(contact){
   if(!active){stroke={x,y,x,y,now,now,context};active=true;}
   if(stroke.context!=context){active=false;blocked=true;return false;}
   stroke.x1=x;stroke.y1=y;stroke.ended=now;last_contact=now;return false;
  }
  if(active&&now-last_contact>=40000){out=stroke;active=false;return true;}
  return false;
 }
};
}
