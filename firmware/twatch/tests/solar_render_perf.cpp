// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <chrono>
#include <cstdio>
#include <vector>
#include <cstring>
extern uint64_t solar_math_counts[4];
int main(){using namespace watch;using Clock=std::chrono::steady_clock;
 std::vector<uint16_t> pixels(Width*Height);RenderCache damage;View view;view.settings.face=2;
 Model m;m.time_valid=true;m.source=Source::BLE;m.rms_ns=87;m.hold_valid=true;m.hold_trials=100;m.hold_ms=58000;m.source_rssi=-64;m.motion=true;
 const auto utc=calendar::solar({2026,11,15}).noon;m.calendar=calendar::snapshot(utc,true);m.utc_us=utc*1000000;m.sampled_us=1000000;m.until_us=1250000;
 const auto cold=Clock::now();render(pixels.data(),m,view,1000000,&damage);const double cold_us=std::chrono::duration<double,std::micro>(Clock::now()-cold).count();
 std::memset(solar_math_counts,0,sizeof(uint64_t)*4);double total=0;uint64_t hash=1469598103934665603ULL;
 constexpr int frames=120;
 for(int i=0;i<frames;++i){const uint64_t now=1100000+uint64_t(i)*100000;m.sampled_us=now;m.until_us=now+250000;m.utc_us=utc*1000000+now-1000000;
  m.solar_clock=solar_opposition(solar::civil_angle(m.utc_us/1000000,m.calendar.local_offset_minutes,(m.utc_us%1000000)/1000000.0));
  m.rms_position=i/150.0;m.hold_position=i/140.0;m.signal_position=i/130.0;m.tide_position=i/120.0;
  const auto start=Clock::now();render(pixels.data(),m,view,now,&damage);total+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
  for(auto pixel:pixels){hash^=pixel;hash*=1099511628211ULL;}
 }
 std::printf("{\"frames\":%d,\"cold_us\":%.3f,\"warm_mean_us\":%.3f,\"sin_calls\":%llu,\"cos_calls\":%llu,\"sqrt_calls\":%llu,\"pow_calls\":%llu,\"pixel_sequence_hash\":\"%016llx\"}\n",frames,cold_us,total/frames,(unsigned long long)solar_math_counts[0],(unsigned long long)solar_math_counts[1],(unsigned long long)solar_math_counts[2],(unsigned long long)solar_math_counts[3],(unsigned long long)hash);
}
