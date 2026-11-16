// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "calendar.hpp"
#include <cmath>
namespace watch::solar {
constexpr double Pi=3.14159265358979323846;
constexpr int CenterX=205,CenterY=251,SkyRadius=166,OrbitRadius=124;
// Chamfer midpoints of the actual (18,22)..(391,479) case, inset32px
// along each inward45-degree normal. All four nearest case margins match.
constexpr double CornerLeft=77.5,CornerRight=331.5,CornerTop=81.5,CornerBottom=419.5;
constexpr int CornerRadius=34;
struct Point {double x,y;};
// A fixed CIVIL 24-hour scale: noon top, 18 right, midnight bottom, 06 left.
inline double hour_angle(double hour){return hour*15-180;}
inline double civil_angle(uint64_t utc,int16_t offset,double fraction=0){
 double seconds=std::fmod(double(utc%86400)+offset*60+fraction+86400,86400);
 return seconds/240-180;
}
inline Point point(double angle,double radius=OrbitRadius){const double a=angle*Pi/180;return {CenterX+radius*std::sin(a),CenterY-radius*std::cos(a)};}
// Each event uses its own Central offset, including dates on which DST changes.
inline Point event(uint64_t utc){return point(civil_angle(utc,calendar::central_offset(utc)));}
struct Chord {
 Point morning{},evening{};double slope=0,intercept=CenterY;bool valid=false;
 double y(double x)const{return slope*x+intercept;}
};
inline Chord chord(uint64_t morning,uint64_t evening){
 Chord c;if(!morning||!evening)return c;c.morning=event(morning);c.evening=event(evening);
 const double dx=c.evening.x-c.morning.x;if(std::abs(dx)<1e-6)return c;
 c.slope=(c.evening.y-c.morning.y)/dx;c.intercept=c.morning.y-c.slope*c.morning.x;c.valid=true;return c;
}
}
