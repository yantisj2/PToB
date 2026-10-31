// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define V12_RC7_CLOCK_TEST_INSPECT 1
#include "g1_corrected_clock.hpp"
#include "g1_time_zone.hpp"
#include "coarse_time_zone.hpp"
#include "coarse_tz_scan.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace v12 {
struct Rc7PermissionInspect {
 static void pair(G1CorrectedClock& c,uint64_t local,uint64_t remote,uint8_t channel,uint64_t available){
  G1CorrectedClock::Original o{};o.local=local;o.channel=channel;
  const auto before=c.prediction();c.observe_clock(o,remote,available);
  if(before.tick)assert(c.prediction().relative_at(available)==before.relative_at(available));
 }
};
}
int main(){
 using namespace v12;
 G1ClockPrediction p{20000000,1000000000,2000000,0,0};
 assert(p.phase_slew_ns_s()==50000&&p.relative_at(40000000)==2000050000);
 p.phase=-2000000;assert(p.relative_at(40000000)==1999950000);
 p.phase=1999;assert(p.phase_slew_ns_s()==1000);p.phase=2000;assert(p.phase_slew_ns_s()==8000);
 G1CorrectedClock clock;
 for(unsigned i=0;i<128;i++){
  const uint64_t tick=20000000+uint64_t(i)*1250000;
  const auto channel=uint8_t(i%7);
  const uint64_t remote=tick*50+10000000000+uint64_t(std::llround(double(tick-20000000)/20000000*27.3))+channel*15;
  Rc7PermissionInspect::pair(clock,tick,remote,channel,tick+2000);
  if(i==32)assert(clock.frequency_trained(tick+2000));
 }
 assert(clock.frequency_trained()&&clock.fit_points()==64&&std::abs(clock.prediction().rate-27.3)<1);
 assert(!clock.frequency_trained(clock.fit_tick20()+40000001));
 auto c=clock.prediction();assert(!c.relative_at(c.tick-1));
 std::printf("servo %.17g %lld %lld %u %llu %llu\n",c.rate,(long long)c.epoch,(long long)c.phase,clock.rate_pairs(),(unsigned long long)clock.causal_squares(),(unsigned long long)clock.causal_max_ns());
 GmControlScan scan;assert(!scan.poll(1000000,true,123,0,1000000));
 assert(scan.poll(1032000,true,123,32000,1032000)&&scan.observed(1032000));
 assert(!scan.poll(1050000,true,123,50000,1050000));
 assert(!scan.poll(1250000,false,124,0,1250000)&&!scan.allows(1282000));
 r2_tz_lease z{};G1TimeZoneOrder order;
 assert(order.bootstrap(z,{2,-300},1000000,1000000));
 assert(order.observe(z,{2,-360},3,1100000,1100000,R2_TZ_GM_PERIOD_US,false));
 assert(!order.observe(z,{2,-300},2,1200000,1200000,R2_TZ_GM_PERIOD_US,false));
 assert(!order.bootstrap(z,{2,-300},1200000,1200000));
 assert(order.observe(z,r2_tz_unknown(),4,1200000,1200000,R2_TZ_GM_PERIOD_US,false));
 r2_tz_renew(&z,1300000,R2_TZ_GM_PERIOD_US);assert(!r2_tz_current(&z,1300000).id);
 int64_t shown=0;assert(!std::strcmp(clock_zone({2,-300},1750000,1749999,100000000000,shown),"CDT")&&shown==82000000000);
 assert(!std::strcmp(clock_zone({2,-300},1750000,1750000,100000000000,shown),"UTC")&&shown==100000000000);
}
