#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ptob_gnss {
inline void increment(uint32_t& value){if(value!=UINT32_MAX)++value;}
// Independent capture-counter conversion only. No GNSS observation enters the
// BLE estimator. S3 APB 80 MHz / XTAL GPTimer 20 MHz requires locked clocks.
class CounterMap {
public:
 struct Anchor {uint64_t before=0,after=0,available=0;uint32_t capture=0;};
 struct Edge {uint64_t tick=0,anchor_available=0;uint32_t bracket_ns=0;bool valid=false;};
 bool observe(const Anchor& a) {
  if(!a.before || a.after<a.before || a.available<a.after || a.after-a.before>400){increment(rejected);return false;}
  if(last_.before){
   if(a.before<=last_.after || a.before-last_.after>20000000){reset();increment(rejected);return false;}
   const uint32_t dc=a.capture-last_.capture;
   const uint64_t low=(a.before-last_.after)*4,high=(a.after-last_.before)*4;
   if(uint64_t(dc)+8<low || uint64_t(dc)>high+8){reset();increment(rejected);return false;}
  }
  anchors_[head_]=a;head_=(head_+1)%anchors_.size();last_=a;increment(accepted);return true;
 }
 bool release(uint64_t available) {
  if(!last_.before || available<last_.available || available<last_.after){reset();increment(rejected);return false;}
  last_.available=available;anchors_[(head_+anchors_.size()-1)%anchors_.size()].available=available;return true;
 }
 Edge convert(uint32_t capture,uint64_t consumed)const {
  const Anchor* best=nullptr;uint32_t elapsed=0;
  for(const auto& a:anchors_){
   if(!a.before || consumed<a.available || consumed-a.available>40000000)continue;
   const uint32_t d=capture-a.capture;
   // Only preceding anchors within two seconds are eligible. Thus modulo
   // wrap is unambiguous; a later calibration cannot change a prior edge.
   if(d>160000000u || (best && a.available<=best->available))continue;
   best=&a;elapsed=d;
  }
  if(!best)return {};
  const uint64_t midpoint=best->before+(best->after-best->before)/2;
  if(midpoint>UINT64_MAX-(uint64_t(elapsed)+2)/4)return {};
  const uint64_t tick=midpoint+(uint64_t(elapsed)+2)/4;
  if(tick>consumed)return {};
  // Enclosure covers read/rounding quantization and one APB capture quantum.
  // GPIO input synchronizer bias, GPS/cable bias and RF map terms stay UNKNOWN.
  const uint32_t bound=uint32_t((best->after-best->before)*25+100+13);
  return {tick,best->available,bound,true};
 }
 void reset(){anchors_={};last_={};head_=0;}
 uint32_t accepted=0,rejected=0;
private:
 std::array<Anchor,16> anchors_{};Anchor last_{};unsigned head_=0;
};

struct Rmc {uint32_t second=0;bool fix=false,valid=false;};
inline bool digit(char c){return c>='0' && c<='9';}
inline int two(const char* s){return digit(s[0]) && digit(s[1])?(s[0]-'0')*10+s[1]-'0':-1;}
inline int hex(char c){return c>='0' && c<='9'?c-'0':c>='A' && c<='F'?c-'A'+10:c>='a' && c<='f'?c-'a'+10:-1;}
inline bool checksum(const char* s,size_t n) {
 if(n<7 || s[0]!='$')return false;
 size_t star=1;uint8_t value=0;
 for(;star<n && s[star]!='*';++star)value^=uint8_t(s[star]);
 return star+3==n && hex(s[star+1])>=0 && hex(s[star+2])>=0 && value==((hex(s[star+1])<<4)|hex(s[star+2]));
}
inline Rmc rmc(const char* s,size_t n) {
 if(!checksum(s,n) || n<7 || s[3]!='R' || s[4]!='M' || s[5]!='C' || s[6]!=',')return {};
 const char* fields[13]{};size_t lengths[13]{};unsigned count=0;size_t first=7;
 for(size_t i=7;i<n && count<13;++i)if(s[i]==',' || s[i]=='*'){
  fields[count]=s+first;lengths[count++]=i-first;first=i+1;if(s[i]=='*')break;
 }
 if(count<9 || lengths[0]<6 || lengths[1]!=1 || lengths[8]!=6)return {};
 const auto* time=fields[0];const auto* date=fields[8];
 const int h=two(time),m=two(time+2),sec=two(time+4),d=two(date),month=two(date+2),yy=two(date+4);
 if(h<0 || h>23 || m<0 || m>59 || sec<0 || sec>59 || d<1 || month<1 || month>12 || yy<0 || yy>79)return {};
 // Fractional RMC epochs cannot be silently associated to a whole PPS.
 for(size_t i=6;i<lengths[0];++i)if((i==6 && time[i]!='.') || (i>6 && time[i]!='0'))return {};
 const int year=2000+yy;const bool leap=(year%4==0 && (year%100!=0 || year%400==0));
 static constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 if(d>days[month-1]+int(month==2 && leap) || (fields[1][0]!='A' && fields[1][0]!='V'))return {};
 uint64_t nd=0;for(int y=1970;y<year;++y)nd+=365+(y%4==0 && (y%100!=0 || y%400==0));
 for(int mo=1;mo<month;++mo)nd+=days[mo-1]+int(mo==2 && leap);
 nd+=d-1;const uint64_t second=((nd*24+h)*60+m)*60+sec;
 if(second>UINT32_MAX)return {};
 return {uint32_t(second),fields[1][0]=='A',true};
}
} // namespace ptob_gnss
