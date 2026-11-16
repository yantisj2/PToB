// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
// Diagnostic stages: 1 gesture, 2 drain, 3 refused, 4 drain fault,
// 5 PMU command, 6 PMU returned without powering off.
void button_power_stage(uint8_t stage,int error=0);
enum class ButtonAction {None,Single,PowerOff};
// GPIO0: stable edges, two complete short clicks, no startup-held gesture.
class WatchButton {
 bool initialized=false,raw=false,down=false,armed=false,pending=false;
 uint64_t changed=0,pressed=0,released=0,last=0;
public:
 ButtonAction update(bool level,uint64_t now){
  if(!initialized||now<last){initialized=true;raw=down=level;armed=pending=false;changed=last=now;return ButtonAction::None;}
  last=now;
  if(level!=raw){raw=level;changed=now;}
  if(raw!=down&&now-changed>=40000){
   down=raw;
   if(down){armed=true;pressed=now;}
   else if(armed){
    armed=false;
    if(now-pressed>600000){pending=false;return ButtonAction::Single;}
    if(pending&&now-released<=450000){pending=false;return ButtonAction::PowerOff;}
    pending=true;released=now;
   }
  }
  if(pending&&!raw&&!down&&now-released>450000){pending=false;return ButtonAction::Single;}
  return ButtonAction::None;
 }
};
}
