// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "calendar.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace watch::calendar;
int main(){
 const auto day=midnight({2026,11,15});
 assert(day==1794700800ULL);
 assert(!midnight({2026,2,29})&&midnight({2028,2,29})&&!midnight({2028,2,30}));
 assert(snapshot(day,false).light==Light::Unknown&&snapshot(day,false).tide_count==0);
 assert(!solar({2006,1,1}).valid&&solar({2028,2,29}).valid);
 const auto march=midnight({2026,3,8})+8*3600,november=midnight({2026,11,1})+7*3600;
 assert(central_offset(march-1)==-360&&central_offset(march)==-300);
 assert(central_offset(november-1)==-300&&central_offset(november)==-360);
 const auto sun=solar({2026,11,15});assert(sun.valid);
 auto n=snapshot(sun.dawn-1,true);assert(n.light==Light::Night&&brightness(n,10,60,40)==10);
 n=snapshot(sun.sunrise,true);assert(n.light==Light::Day&&brightness(n,10,60,40)==60);
 assert(snapshot(sun.dusk,true).light==Light::Night);
 assert(brightness(snapshot(0,false),10,60,40)==40);
 uint8_t prior=10;
 for(auto t=sun.dawn;t<=sun.sunrise;t+=10){const auto value=brightness(snapshot(t,true),10,60,40);assert(value>=prior&&value<=60);prior=value;}
 n=snapshot(day+3600,true);assert(n.today.date.day==14); // UTC midnight is previous local evening.
 n=snapshot(sun.sunset+1,true,true);assert(n.clock_estimated&&n.next_sunset>sun.sunset&&n.next_sunrise>sun.sunrise);
 n=snapshot(midnight({2025,12,31}),true);assert(n.tides==Coverage::BeforeRange&&!n.tide_count&&n.today.valid);
 n=snapshot(midnight({2028,1,1}),true);assert(n.tides==Coverage::Expired&&!n.tide_count&&n.today.valid);
 n=snapshot(day,true);assert(n.tides==Coverage::Available&&n.tide_count==8);
 for(unsigned i=1;i<n.tide_count;++i)assert(n.next_tides[i].utc>n.next_tides[i-1].utc);
 auto event=snapshot(n.next_tides[0].utc,true);assert(event.next_tides[0].utc==n.next_tides[0].utc);
 assert(snapshot(n.next_tides[0].utc+1,true).next_tides[0].utc>n.next_tides[0].utc);
 for(const Date d: {Date{2026,3,20},Date{2026,6,21},Date{2026,11,15},Date{2026,12,21}}){
  const auto s=solar(d);assert(s.valid);
  std::printf("%04d-%02u-%02u %llu %llu %llu %llu\n",d.year,d.month,d.day,(unsigned long long)s.dawn,(unsigned long long)s.sunrise,(unsigned long long)s.sunset,(unsigned long long)s.dusk);
 }
}
