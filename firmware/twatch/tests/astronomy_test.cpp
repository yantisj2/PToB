// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "calendar.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace watch::calendar;
int main(int argc,char** argv){
 if(argc==2){const auto s=snapshot(std::strtoull(argv[1],nullptr,10),true);assert(s.astronomy_valid);
  std::printf("%.12f %.12f %.12f %.12f\n",s.solar_altitude_deg,s.solar_azimuth_deg,s.moon_phase,s.moon_illumination);return 0;}
 for(int year=2026;year<=2027;++year)for(unsigned month=1;month<=12;++month){
  auto day=solar({year,month,15});
  assert(day.astronomical_dawn<day.nautical_dawn&&day.nautical_dawn<day.dawn&&day.dawn<day.sunrise);
  assert(day.sunset<day.dusk&&day.dusk<day.nautical_dusk&&day.nautical_dusk<day.astronomical_dusk);
  for(auto utc=midnight({year,month,15});utc<midnight({year,month,16});utc+=900){
   const auto s=snapshot(utc,true);assert(s.astronomy_valid);
   assert(std::isfinite(s.moon_phase)&&s.moon_phase>=0&&s.moon_phase<=1);
   assert(s.moon_illumination>=0&&s.moon_illumination<=1);
   assert(s.solar_hour_angle_deg>=-180&&s.solar_hour_angle_deg<180);
   assert(s.solar_altitude_deg>=-90&&s.solar_altitude_deg<=90&&s.moon_altitude_deg>=-90&&s.moon_altitude_deg<=90);
   if(s.tide_position_valid){assert(s.previous_tide_valid&&s.tide_count&&s.tide_position>=0&&s.tide_position<=1);assert(s.previous_tide.high!=s.next_tides[0].high);}
  }
 }
 auto unknown=snapshot(0,false);assert(!unknown.astronomy_valid&&!unknown.tide_position_valid);
 auto expired=snapshot(midnight({2028,1,1}),true);assert(expired.astronomy_valid&&!expired.tide_position_valid);
 auto first=snapshot(midnight({2026,1,1}),true);assert(!first.previous_tide_valid&&!first.tide_position_valid);
 auto s=snapshot(midnight({2026,11,15}),true);assert(s.tide_count==8);
 for(unsigned i=0;i<7;++i){auto at=snapshot(s.next_tides[i].utc,true);if(at.tide_position_valid)assert(std::abs(at.tide_position-(s.next_tides[i].high?1:0))<1e-8);
  auto after=snapshot(s.next_tides[i].utc+1,true);if(at.tide_position_valid&&after.tide_position_valid)assert(std::abs(at.tide_position-after.tide_position)<.00001);
 }
}
