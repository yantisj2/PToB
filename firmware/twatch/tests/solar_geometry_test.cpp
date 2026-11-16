// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "solar_geometry.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
int main(){using namespace watch;
 const auto six=solar::point(solar::hour_angle(6),143),eighteen=solar::point(solar::hour_angle(18),143);
 assert(std::abs(six.x-62)<1e-10&&std::abs(eighteen.x-348)<1e-10&&six.y==251&&eighteen.y==251);
 double minimum_gap=1000;
 for(double x:{solar::CornerLeft,solar::CornerRight})for(double y:{solar::CornerTop,solar::CornerBottom}){
  const double gap=std::hypot(x-solar::CenterX,y-solar::CenterY)-solar::SkyRadius-solar::CornerRadius-1;
  minimum_gap=std::min(minimum_gap,gap);assert(gap>9.5);
  const double frame_x=x<205?x:409-x,frame_y=y<251?y:501-y;
  assert(std::abs((frame_x-45.5)-(frame_y-49.5))<1e-10);
  assert(std::abs((frame_x+frame_y-95)/std::sqrt(2.0)-34.5-10.754833995939)<1e-8); // Includes both AA fringes.
  for(int a=0;a<360;++a){const double px=x+34.5*std::sin(a*solar::Pi/180),py=y-34.5*std::cos(a*solar::Pi/180);
   const double rx=std::min(px,409-px),ry=std::min(py,501-py);
   assert(rx>=18&&ry>=22&&rx+ry>95); // Clipped case shoulders.
   if(rx<100&&ry<100)assert(std::hypot(rx-100,ry-100)<100); // Conservative panel aperture.
  }
 }
 const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 double min_height=1e9,max_height=-1e9,min_slope=1e9,max_slope=-1e9;unsigned checked=0;
 for(unsigned month=1;month<=12;++month)for(unsigned day=1;day<=days[month-1];++day){
  const auto s=calendar::solar({2026,month,day});assert(s.valid);
  const uint64_t events[][2]={{s.sunrise,s.sunset},{s.dawn,s.dusk},{s.nautical_dawn,s.nautical_dusk},{s.astronomical_dawn,s.astronomical_dusk}};
  solar::Chord chords[4];
  for(int k=0;k<4;++k){chords[k]=solar::chord(events[k][0],events[k][1]);assert(chords[k].valid);
   for(auto utc:events[k]){const auto p=solar::event(utc);assert(std::abs(chords[k].y(p.x)-p.y)<1e-8);
    // Independent civil-day coordinate, including each event's actual DST offset.
    const int64_t seconds=(int64_t(utc)+calendar::central_offset(utc)*60)%86400;
    assert(std::abs(solar::civil_angle(utc,calendar::central_offset(utc))-(seconds/240.0-180))<1e-8);
   }
  }
  for(int x=39;x<=371;++x)for(int k=1;k<4;++k)assert(chords[k].y(x)>chords[k-1].y(x));
  min_height=std::min(min_height,chords[0].y(205));max_height=std::max(max_height,chords[0].y(205));
  min_slope=std::min(min_slope,chords[0].slope);max_slope=std::max(max_slope,chords[0].slope);++checked;
 }
 const auto summer=calendar::solar({2026,6,21}),winter=calendar::solar({2026,12,21});
 assert(solar::chord(summer.sunrise,summer.sunset).y(205)>251);
 assert(solar::chord(winter.sunrise,winter.sunset).y(205)<251);
 assert(max_height-min_height>50&&max_slope-min_slope>.2);
 for(auto date:{calendar::Date{2026,3,20},calendar::Date{2026,9,22}}){const auto s=calendar::solar(date);assert(s.valid&&solar::chord(s.sunrise,s.sunset).valid);}
 const uint64_t spring=calendar::midnight({2026,3,8})+8*3600,autumn=calendar::midnight({2026,11,1})+7*3600;
 assert(calendar::central_offset(spring-1)==-360&&calendar::central_offset(spring)==-300);
 assert(calendar::central_offset(autumn-1)==-300&&calendar::central_offset(autumn)==-360);
 for(auto date:{calendar::Date{2026,3,8},calendar::Date{2026,11,1}}){const auto s=calendar::solar(date);const auto p=solar::event(s.sunrise);
  const auto wrong=solar::point(solar::civil_angle(s.sunrise,calendar::central_offset(calendar::midnight(date))));
  assert(std::hypot(p.x-wrong.x,p.y-wrong.y)>30); // A midnight-offset reuse is detected.
 }
 std::printf("PASS Solar geometry: %u days, both solstices/equinoxes/DST transitions; fixed 06/18; 8 exact event-chord crossings/day; %.2fpx minimum AA-aware subdial gap\n",checked,minimum_gap);
}
