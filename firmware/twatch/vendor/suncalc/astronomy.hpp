// Selected equations ported from SunCalc v1.9.0.
// Copyright (c) 2011-2015 Vladimir Agafonkin. BSD-2-Clause; see LICENSE.
// C++ adaptation Copyright (c) 2026 Jonathan Yantis, same BSD-2-Clause terms.
// Approximate geocentric astronomy for a watch illustration, not navigation.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace watch::astronomy {
constexpr double Pi=3.14159265358979323846,Rad=Pi/180,Obliquity=23.4397*Rad;
struct Equatorial {double ra,dec,distance;};
struct Position {double sun_altitude,sun_azimuth,sun_hour_angle,moon_altitude,moon_azimuth,phase,illumination;};
inline double clamp_unit(double value){return std::max(-1.0,std::min(1.0,value));}
inline double normalize(double value){value=std::fmod(value,360);return value<0?value+360:value;}
inline Equatorial equatorial(double longitude,double latitude,double distance){
 return {std::atan2(std::sin(longitude)*std::cos(Obliquity)-std::tan(latitude)*std::sin(Obliquity),std::cos(longitude)),
  std::asin(clamp_unit(std::sin(latitude)*std::cos(Obliquity)+std::cos(latitude)*std::sin(Obliquity)*std::sin(longitude))),distance};
}
inline double altitude(double hour,double latitude,double declination){
 return std::asin(clamp_unit(std::sin(latitude)*std::sin(declination)+std::cos(latitude)*std::cos(declination)*std::cos(hour)))/Rad;
}
// Clockwise from geographic north, unlike upstream's south-relative azimuth.
inline double azimuth(double hour,double latitude,double declination){
 return normalize(std::atan2(std::sin(hour),std::cos(hour)*std::sin(latitude)-std::tan(declination)*std::cos(latitude))/Rad+180);
}
inline Position position(uint64_t utc,double latitude,double longitude){
 const double days=double(utc)/86400+2440587.5-2451545;
 const double mean=Rad*(357.5291+.98560028*days);
 const double sun_longitude=mean+Rad*(1.9148*std::sin(mean)+.02*std::sin(2*mean)+.0003*std::sin(3*mean)+102.9372)+Pi;
 const auto sun=equatorial(sun_longitude,0,149598000);
 const double moon_mean=Rad*(134.963+13.064993*days),moon_f=Rad*(93.272+13.229350*days);
 const auto moon=equatorial(Rad*(218.316+13.176396*days)+Rad*6.289*std::sin(moon_mean),Rad*5.128*std::sin(moon_f),385001-20905*std::cos(moon_mean));
 const double sidereal=Rad*(280.16+360.9856235*days+longitude),lat=latitude*Rad;
 const double sun_hour=sidereal-sun.ra,moon_hour=sidereal-moon.ra;
 const double separation=std::acos(clamp_unit(std::sin(sun.dec)*std::sin(moon.dec)+std::cos(sun.dec)*std::cos(moon.dec)*std::cos(sun.ra-moon.ra)));
 const double incidence=std::atan2(sun.distance*std::sin(separation),moon.distance-sun.distance*std::cos(separation));
 const double angle=std::atan2(std::cos(sun.dec)*std::sin(sun.ra-moon.ra),std::sin(sun.dec)*std::cos(moon.dec)-std::cos(sun.dec)*std::sin(moon.dec)*std::cos(sun.ra-moon.ra));
 return {altitude(sun_hour,lat,sun.dec),azimuth(sun_hour,lat,sun.dec),normalize(sun_hour/Rad+180)-180,
  altitude(moon_hour,lat,moon.dec),azimuth(moon_hour,lat,moon.dec),.5+.5*incidence*(angle<0?-1:1)/Pi,(1+std::cos(incidence))/2};
}
}
