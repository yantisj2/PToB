// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
namespace v12 {
// Immutable when copied to a reader. The owner advances the causal sensor
// history; held phase, rate anchor and temperature coefficient never change.
struct W12ThermalProjection {
  static constexpr uint64_t Hz=20000000;
  struct Point {uint64_t tick=0;double measured=0,lagged=0,area=0;};
  uint64_t start20=0;double initial=0,beta=0;Point current{},previous{};
  bool enabled=false,valid=false;
  void arm(uint64_t at,double measured,double lagged,double slope){
    start20=at;initial=lagged;beta=slope;current={at,measured,lagged,0};previous={};
    enabled=true;valid=at&&std::isfinite(measured)&&std::isfinite(lagged)&&std::isfinite(slope)&&std::abs(slope)<=5000;
  }
  double area_at(const Point& p,uint64_t at)const {
    const double dt=double(at-p.tick)/Hz,decay=std::exp(-dt/8);
    return p.area+(p.measured-initial)*dt+(p.lagged-p.measured)*8*(1-decay);
  }
  void temperature(uint64_t at,double measured){
    if(!enabled||!valid||at<=current.tick)return;
    if(!std::isfinite(measured)||measured<-40||measured>125||at-current.tick>3*Hz){valid=false;return;}
    const auto old=current;const double dt=double(at-old.tick)/Hz;
    previous=old;current={at,measured,old.measured+(old.lagged-old.measured)*std::exp(-dt/8),area_at(old,at)};
  }
  bool correction(uint64_t at,int64_t& ns)const {
    ns=0;if(!enabled)return true;
    if(!valid||at<start20)return false;
    const auto& p=at>=current.tick?current:previous;
    if(!p.tick||at<p.tick||at-p.tick>3*Hz)return false;
    const double value=beta*area_at(p,at);
    if(!std::isfinite(value)||std::abs(value)>=1000000)return false;
    ns=int64_t(std::llround(value));return true;
  }
};
static_assert(sizeof(W12ThermalProjection)<=96,"Frozen thermal projection RAM budget");
}
