// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_native_clock.hpp"
#include <cassert>
#include <cstdio>
int main(){
 n30::LoRaNativeClock c;uint64_t tick=0,event=0,us=1000000;const uint32_t origin=0xfffe0000;
 assert(!c.current(0,origin,us,tick));assert(c.current(1,origin,us,tick)&&tick==16000000);
 assert(c.current(1,origin+80,us+1,tick)&&tick==16000016);
 assert(c.event(1,origin+79,event)&&event==16000015); // floor80/5, <62.5ns quantization
 for(uint64_t i=1;i<=180000;i++){
  us=1000000+i*2000;assert(c.current(1,uint32_t(origin+i*160000),us,tick));assert(tick==16000000+i*32000);
 }
 assert(c.event(1,uint32_t(origin+UINT64_C(360000000)*80-UINT64_C(8000000)*80),event));
 assert(event==16000000+UINT64_C(352000000)*16);
 assert(!c.event(1,uint32_t(origin+UINT64_C(360000000)*80-UINT64_C(8000000)*80-1),event));
 assert(!c.event(2,uint32_t(origin),event));assert(!c.current(2,0,us+1,tick));
 c.reset();assert(c.current(2,0,1,tick));assert(!c.event(2,UINT32_MAX,event));
 assert(!c.current(2,0,102,tick));assert(!c.event(2,0,event)); // stopped is latched
 c.reset();assert(c.current(3,0,1,tick));assert(!c.current(3,100,1+n30::fine::Counter::HalfWrapUs,tick));
 c.reset();assert(c.current(4,0,1,tick));assert(!c.current(4,40000,1001,tick)); // half-rate
 assert(c.fault()==uint8_t(n30::fine::Counter::Fault::RateLow));
 // Reproduce the old caller-time race without changing the rate limits:
 // healthy raw advances2ms, but the caller is preempted for30ms after capture.
 n30::LoRaNativeClock old_pair,paired;
 assert(old_pair.current(1,0,1000000,tick)&&paired.current(1,0,1000000,tick));
 assert(!old_pair.current(1,160000,1032000,tick));
 assert(old_pair.fault()==uint8_t(n30::fine::Counter::Fault::RateLow));
 assert(paired.current(1,160000,1002000,tick)&&tick==16032000);
 assert(paired.current(1,2720000,1034000,tick)&&tick==16544000);
 c.reset();assert(!c.current(UINT64_C(1)<<32,0,1,tick));
 std::puts("PASS independent group0 fixed-origin80MHz/16MHz coordinate,6min wraps, exact8s event bound, no future/pre-origin, epoch/stopped/rate/halfwrap refusal");
}
