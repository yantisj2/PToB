// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "calendar.hpp"
#include "../vendor/suncalc/astronomy.hpp"
#include <cmath>
#include <algorithm>
#include <cstddef>
namespace watch::calendar {
namespace {
#include "../data/tides.inc"
constexpr double Pi=3.14159265358979323846;
bool leap(int y){return y%4==0&&(y%100!=0||y%400==0);}
unsigned month_days(int y,unsigned m){const unsigned n[]={31,28,31,30,31,30,31,31,30,31,30,31};return m>=1&&m<=12?n[m-1]+(m==2&&leap(y)):0;}
Date date_at(uint64_t utc){
 uint64_t days=utc/86400;int y=1970;
 for(;y<=2100;++y){unsigned n=leap(y)?366:365;if(days<n)break;days-=n;}
 if(y>2100)return {};
 unsigned m=1;while(m<=12&&days>=month_days(y,m)){days-=month_days(y,m);++m;}
 return {y,m,unsigned(days+1)};
}
void solar_terms(double day,int year,double& equation,double& declination){
 const double g=2*Pi/(leap(year)?366:365)*(day-1);
 equation=229.18*(.000075+.001868*std::cos(g)-.032077*std::sin(g)-.014615*std::cos(2*g)-.040849*std::sin(2*g));
 declination=.006918-.399912*std::cos(g)+.070257*std::sin(g)-.006758*std::cos(2*g)+.000907*std::sin(2*g)-.002697*std::cos(3*g)+.00148*std::sin(3*g);
}
uint64_t event(uint64_t base,int year,double day,double zenith,bool rising){
 double minute=720-4*Longitude;
 for(int i=0;i<3;++i){
  double eq,dec;solar_terms(day+(minute/60-12)/24,year,eq,dec);
  const double lat=Latitude*Pi/180;
  const double cosine=std::cos(zenith*Pi/180)/(std::cos(lat)*std::cos(dec))-std::tan(lat)*std::tan(dec);
  if(cosine<-1||cosine>1)return 0;
  const double angle=std::acos(cosine)*180/Pi;
  minute=720-4*(Longitude+(rising?angle:-angle))-eq;
 }
 return base+uint64_t(std::llround(minute*60));
}
}
uint64_t midnight(Date d){
 if(d.year<1970||d.year>2100||!d.month||d.month>12||!d.day||d.day>month_days(d.year,d.month))return 0;
 uint64_t days=0;for(int y=1970;y<d.year;++y)days+=leap(y)?366:365;
 for(unsigned m=1;m<d.month;++m)days+=month_days(d.year,m);
 return (days+d.day-1)*86400;
}
int16_t central_offset(uint64_t utc){
 const auto d=date_at(utc);if(d.year<2007||d.year>2099)return -360;
 const auto sunday=[&](unsigned month){const auto first=midnight({d.year,month,1});return 1+(7-unsigned((first/86400+4)%7))%7;};
 const auto start=midnight({d.year,3,sunday(3)+7})+8*3600;
 const auto end=midnight({d.year,11,sunday(11)})+7*3600;
 return utc>=start&&utc<end?-300:-360;
}
SolarDay solar(Date d){
 SolarDay s;s.date=d;const auto base=midnight(d);
 if(!base||d.year<2007||d.year>2099)return s;
 const double day=double((base-midnight({d.year,1,1}))/86400+1);
 s.astronomical_dawn=event(base,d.year,day,108,true);s.nautical_dawn=event(base,d.year,day,102,true);
 s.nautical_dusk=event(base,d.year,day,102,false);s.astronomical_dusk=event(base,d.year,day,108,false);
 s.dawn=event(base,d.year,day,96,true);s.sunrise=event(base,d.year,day,90.833,true);
 s.sunset=event(base,d.year,day,90.833,false);s.dusk=event(base,d.year,day,96,false);
 double eq,decl;solar_terms(day,d.year,eq,decl);s.noon=base+uint64_t(std::llround((720-4*Longitude-eq)*60));
 s.valid=s.dawn&&s.dawn<s.sunrise&&s.sunrise<s.noon&&s.noon<s.sunset&&s.sunset<s.dusk;return s;
}
Snapshot snapshot(uint64_t utc,bool valid,bool estimated){
 Snapshot s;if(!valid||utc<midnight({2007,1,1})||utc>=midnight({2100,1,1}))return s;
 s.sampled_utc=utc;s.clock_estimated=estimated;s.local_offset_minutes=central_offset(utc);
 const auto local=uint64_t(int64_t(utc)+int64_t(s.local_offset_minutes)*60);
 const auto date=date_at(local);s.today=solar(date);const auto tomorrow=solar(date_at(midnight(date)+86400));
 if(s.today.valid){
  s.next_sunrise=utc<s.today.sunrise?s.today.sunrise:tomorrow.sunrise;
  s.next_sunset=utc<s.today.sunset?s.today.sunset:tomorrow.sunset;
  if(utc<s.today.dawn||utc>=s.today.dusk)s.light=Light::Night;
  else if(utc<s.today.sunrise){s.light=Light::Dawn;s.daylight_permille=uint16_t((utc-s.today.dawn)*1000/(s.today.sunrise-s.today.dawn));}
  else if(utc<s.today.sunset){s.light=Light::Day;s.daylight_permille=1000;}
  else {s.light=Light::Dusk;s.daylight_permille=uint16_t((s.today.dusk-utc)*1000/(s.today.dusk-s.today.sunset));}
 }
 const auto sky=astronomy::position(utc,Latitude,Longitude);
 s.solar_altitude_deg=sky.sun_altitude;s.solar_azimuth_deg=sky.sun_azimuth;s.solar_hour_angle_deg=sky.sun_hour_angle;
 s.moon_phase=sky.phase;s.moon_illumination=sky.illumination;s.moon_altitude_deg=sky.moon_altitude;s.moon_azimuth_deg=sky.moon_azimuth;s.astronomy_valid=s.today.valid;
 s.tides_until=TideEnd;
 if(utc<TideStart){s.tides=Coverage::BeforeRange;return s;}
 if(utc>=TideEnd){s.tides=Coverage::Expired;return s;}
 s.tides=Coverage::Available;
 constexpr size_t count=sizeof TideTimes/sizeof *TideTimes;
 static_assert(count==sizeof TideHeights/sizeof *TideHeights);
 size_t lo=0,hi=count;while(lo<hi){const auto mid=(lo+hi)/2;if(uint64_t(TideTimes[mid]>>1)*60<utc)lo=mid+1;else hi=mid;}
 if(lo){const auto i=lo-1;s.previous_tide={uint64_t(TideTimes[i]>>1)*60,TideHeights[i],bool(TideTimes[i]&1)};s.previous_tide_valid=true;}
 if(lo<count&&s.previous_tide_valid){
  const auto& prior=s.previous_tide;const auto next=uint64_t(TideTimes[lo]>>1)*60;const bool high=TideTimes[lo]&1;
  if(high!=prior.high&&next>prior.utc&&next-prior.utc<=48*3600&&utc>=prior.utc&&utc<=next){
   const double progress=double(utc-prior.utc)/double(next-prior.utc);
   const double eased=(1-std::cos(Pi*progress))*.5;
   s.tide_position=high?eased:1-eased;s.tide_rising=high;s.tide_position_valid=true;
  }
 }
 for(;lo<count&&s.tide_count<8;++lo){auto& t=s.next_tides[s.tide_count++];t.utc=uint64_t(TideTimes[lo]>>1)*60;t.high=TideTimes[lo]&1;t.height_mm=TideHeights[lo];}
 return s;
}
uint8_t brightness(const Snapshot& s,uint8_t night,uint8_t day,uint8_t fallback){
 if(s.light==Light::Unknown)return std::min(fallback,uint8_t(100));
 night=std::min(night,uint8_t(100));day=std::max(night,std::min(day,uint8_t(100)));
 return uint8_t(night+(unsigned(day-night)*s.daylight_permille+500)/1000);
}
}
