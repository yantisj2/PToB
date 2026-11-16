// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
namespace watch {
// Presentation dismissal only. Bosch decides the raise; this gate recognizes
// a sustained return toward the preceding pose. No handedness assumption.
struct WristReturn {
 struct Vector {double x=0,y=0,z=0;};
 struct Sample {Vector v;uint64_t at=0;};
 Sample history[24]{};unsigned head=0;Vector filtered{},before{},view{};
 double gravity=0;uint64_t last=0,raised=0,since=0;bool active=false,lowered=false;
 static double dot(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
 static Vector unit(Vector v){const double n=std::sqrt(dot(v,v));return n>0?Vector{v.x/n,v.y/n,v.z/n}:Vector{};}
 void cancel(){active=lowered=false;since=0;}
 void sample(double x,double y,double z,uint64_t at){
  if(last&&at<=last){cancel();return;}
  Vector raw{x,y,z};const double n=std::sqrt(dot(raw,raw));
  if(!std::isfinite(n)||n<1){since=0;return;}
  if(last&&at-last>400000){cancel();gravity=0;}
  if(gravity&&(n<gravity*.75||n>gravity*1.25)){since=0;return;}
  const auto u=unit(raw);
  filtered=gravity?unit({filtered.x*.65+u.x*.35,filtered.y*.65+u.y*.35,filtered.z*.65+u.z*.35}):u;
  gravity=gravity?gravity*.99+n*.01:n;last=at;history[head]={filtered,at};head=(head+1)%24;
  if(!active)return;
  if(at-raised<240000){view=filtered;return;}
  // Require a real raise excursion, then return within ~26 degrees of its
  // earlier pose and at least ~41 degrees away from the viewing pose.
  const bool reverse=dot(before,view)<.85&&dot(filtered,before)>.9&&dot(filtered,view)<.75;
  if(!reverse){since=0;return;}
  if(!since)since=at;
  if(at-since>=240000){lowered=true;active=false;}
 }
 void begin(uint64_t at){
  cancel();if(!last)return;
  if(at<last){if(last-at>240000)return;at=last;}
  if(at-last>240000)return;
  const Sample* prior=nullptr;
  for(const auto& s:history)if(s.at&&at>=s.at&&at-s.at>=700000&&at-s.at<=1800000&&(!prior||s.at>prior->at))prior=&s;
  if(!prior)return;
  before=prior->v;view=filtered;raised=at;active=true;
 }
 bool take_lowered(){const bool value=lowered;lowered=false;return value;}
};
// Direction-aware trial for the measured P2 / left wrist / buttons-elbow
// wearing frame. Original implementation: settled history and angular change
// are established watch techniques (see docs/twatch/wrist-wake.md), not a
// claim of cross-wrist acceptance. Bosch tilt remains an independent witness.
struct WristPose {
 using Vector=WristReturn::Vector;
 Vector window[3]{},before{},view{};double magnitudes[3]{};
 unsigned count=0,head=0;uint64_t last=0,down_since=0,view_since=0,down_at=0,lower_since=0;
 bool prepared=false,returning=false;uint32_t raises=0,rejections=0;
 void reset(){count=head=0;last=down_since=view_since=down_at=lower_since=0;prepared=returning=false;}
 void cancel_return(){returning=false;lower_since=0;}
 // Return 1 = settled raise, 2 = settled reversal; events cannot repeat
 // without first acquiring a fresh down pose. Gaps and shocks break dwell.
 int sample(double x,double y,double z,uint64_t at,bool allow_raise,bool glancing){
  if(last&&(at<=last||at-last>400000)){reset();}
  last=at;
  if(!allow_raise){prepared=false;down_since=view_since=0;}
  if(!glancing)cancel_return();
  Vector raw{x,y,z};double n=std::sqrt(WristReturn::dot(raw,raw));
  if(!std::isfinite(n)||n<1){count=0;down_since=view_since=lower_since=0;++rejections;return 0;}
  window[head]=WristReturn::unit(raw);magnitudes[head]=n;head=(head+1)%3;if(count<3)++count;
  if(count<3)return 0;
  Vector mean{};double lo=magnitudes[0],hi=lo;
  for(unsigned i=0;i<3;++i){mean.x+=window[i].x/3;mean.y+=window[i].y/3;mean.z+=window[i].z/3;lo=std::fmin(lo,magnitudes[i]);hi=std::fmax(hi,magnitudes[i]);}
  double variance=1-WristReturn::dot(mean,mean);
  if(variance>.012||hi>lo*1.25){down_since=view_since=lower_since=0;++rejections;return 0;}
  const auto pose=WristReturn::unit(mean);
  if(glancing&&returning){
   bool reverse=WristReturn::dot(pose,before)>.9&&WristReturn::dot(pose,view)<.75;
   if(!reverse)lower_since=0;
   else {if(!lower_since)lower_since=at;if(at-lower_since>=240000){cancel_return();return 2;}}
  }
  if(!allow_raise)return 0;
  // +Y is the observed lowered pose; viewing is -Y with glass toward user.
  if(pose.y>.10){
   view_since=0;if(!down_since)down_since=at;
   if(at-down_since>=240000){before=pose;down_at=at;prepared=true;}
   return 0;
  }
  down_since=0;
  if(!prepared)return 0;
  if(at-down_at>4000000){prepared=false;view_since=0;return 0;}
  const bool viewing=pose.y<-.55&&pose.z>.05&&std::fabs(pose.x)<.65&&WristReturn::dot(before,pose)<.6;
  if(!viewing){view_since=0;return 0;}
  if(!view_since)view_since=at;
  if(at-view_since<160000)return 0;
  view=pose;prepared=false;view_since=0;returning=true;lower_since=0;++raises;return 1;
 }
};

}
