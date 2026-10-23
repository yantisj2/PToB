// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstdint>
namespace n30 {
constexpr uint64_t SignalMaxUs=2000000;
struct Signal {
 uint64_t peer=0,at=0;uint32_t epoch=0;int16_t dbm=127;
 bool fresh(uint64_t now)const{return peer&&epoch&&at&&now>=at&&now-at<SignalMaxUs&&dbm>=-127&&dbm<=20;}
};
// Host-owner only. Radio observation grants no clock or source permission.
class SignalStrength {
 Signal value_{};
 public:
 void observe(uint64_t peer,uint32_t epoch,int16_t dbm,uint64_t at,uint64_t now){
  if(!peer||!epoch||!at||now<at||now-at>=50000||dbm<-127||dbm>20)return;
  if(value_.peer==peer&&value_.epoch==epoch&&at<=value_.at)return;
  if(value_.peer==peer&&value_.epoch==epoch&&value_.fresh(at))dbm=int16_t((3*value_.dbm+dbm)/4);
  value_={peer,at,epoch,dbm};
 }
 Signal snapshot(uint64_t peer,uint32_t epoch,uint64_t now)const{
  return value_.peer==peer&&value_.epoch==epoch&&value_.fresh(now)?value_:Signal{};
 }
};
inline unsigned signal_bars(const Signal& s,uint64_t now){
 if(!s.fresh(now))return 0;
 return s.dbm>=-55?4:s.dbm>=-70?3:s.dbm>=-85?2:1;
}
}
