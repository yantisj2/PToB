// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
namespace watch {
// Panel-only sleep: ALDO2, CST9217 and its reset stay untouched. Commands and
// the display boost enable follow LilyGoUltra::sleepDisplay/wakeupDisplay.
struct PanelSleep {
 template<class Command,class Enable,class Delay> static bool sleep(bool asleep,Command command,Enable enable,Delay delay){
  if(asleep){
   if(!command(0x28)||!command(0x10))return false;
   delay(120);
   return enable(false);
  }
  if(!enable(true))return false;
  delay(10);
  if(!command(0x11))return false;
  delay(120);
  return true; // Fresh pixels are committed before the separate Display On.
 }
};
}
