#pragma once
#include "../../main/ss3_td_model.hpp"
#include <algorithm>
#include <cmath>
namespace sticks3 {
inline bool chart_point(const v12::TdTrace& p,uint64_t id,uint64_t now,unsigned metric){
 return id&&p.id==id&&p.at_us&&p.at_us<=now&&now-p.at_us<=120000000&&
  (metric==0?p.jitter_ns!=UINT32_MAX:metric==1?p.delivery<=1000:p.rssi!=127);
}
// Shared logarithmic decades for the two currently displayed identities.
// Missing samples remain gaps; measured zero is drawn at the <=1ns floor.
struct TdLogScale {
 unsigned lower=2,upper=3;
 static bool visible(const v12::TdTrace& p,uint64_t id,uint64_t now) {
  return chart_point(p,id,now,0);
 }
 static TdLogScale range(const v12::TdSnapshot& d,uint64_t gm,uint64_t relay) {
  uint64_t low=UINT64_MAX,high=0;
  for(unsigned kind=0;kind<2;kind++)for(unsigned i=0;i<24;i++) {
   const auto& p=(kind?d.relay:d.gm)[i];
   if(!visible(p,kind?relay:gm,d.now_us))continue;
   const uint64_t value=std::max<uint32_t>(1,p.jitter_ns);
   low=std::min(low,value);high=std::max(high,value);
  }
  if(low==UINT64_MAX)return {};
  TdLogScale s{0,0};uint64_t power=1;
  while(power<=low/10&&s.lower<9){power*=10;++s.lower;}
  power=1;while(power<high&&s.upper<10){power*=10;++s.upper;}
  if(s.upper<=s.lower)s.upper=s.lower+1;
  return s;
 }
 int y(uint32_t ns) const {
  const double decade=std::log10(double(std::max<uint32_t>(1,ns)));
  const double fraction=std::clamp((decade-lower)/double(upper-lower),0.0,1.0);
  return 168-int(std::lround(fraction*64));
 }
 int tick(unsigned decade)const{return 168-int((decade-lower)*64/(upper-lower));}
};
struct TdLinearScale {
 int lower=0,upper=10;unsigned metric=1;
 static int value(const v12::TdTrace& p,unsigned metric){return metric==1?1000-int(p.delivery):int(p.rssi);}
 static TdLinearScale range(const v12::TdSnapshot& d,uint64_t gm,uint64_t relay,unsigned metric){
  int low=1000,high=-1000;bool any=false;
  for(unsigned kind=0;kind<2;kind++)for(const auto& p:(kind?d.relay:d.gm))
   if(chart_point(p,kind?relay:gm,d.now_us,metric)){const int v=value(p,metric);low=std::min(low,v);high=std::max(high,v);any=true;}
  if(metric==1){int top=10;if(any&&high>top)for(int power=10;power<=1000;power*=10){
    for(int multiplier:{1,2,5,10})if(power*multiplier>=high){top=std::min(1000,power*multiplier);break;}
    if(top>=high)break;
   }return {0,top,metric};}
  if(!any)return {-100,-20,metric};
  int bottom=int(std::floor(double(low)/10))*10,top=int(std::ceil(double(high)/10))*10;
  if(top-bottom<20){bottom-=10;top=bottom+20;}return {bottom,top,metric};
 }
 int y(const v12::TdTrace& p)const{return 168-int(std::lround(std::clamp(double(value(p,metric)-lower)/double(upper-lower),0.0,1.0)*64));}
};
}
