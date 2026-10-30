// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace corese {
inline bool brightness_command(const char* text,int& request){
 if(!std::strcmp(text,"ui brightness auto")){request=0;return true;}
 if(std::strncmp(text,"ui brightness ",14))return false;
 unsigned value=0,n=0;for(const char* p=text+14;*p;++p){if(*p<'0'||*p>'9'||++n>3)return false;value=value*10+unsigned(*p-'0');}
 if(!n||value<32||value>255)return false;
 request=int(value);return true;
}
// Fixed1x gain/100ms integration. Values are raw ALS counts, not calibrated lux.
// Register definitions: Lite-On LTR-553ALS-WA datasheet, registers80..8C;
// M5Stack's LTR5XX example uses the mean of CH0 and CH1 for its ALS readout.
struct LightState {
 uint64_t sample_us=0;uint32_t samples=0,errors=0,invalid=0,old=0,writes=0,write_errors=0;
 uint16_t ch0=0,ch1=0,raw=0,filtered=0;
 uint8_t part=0,manufacturer=0,current=150,target=150,pmic_code=255;
 bool present=false,configured=false,automatic=true,fresh=false,verified=false,saturated=false;
};
class AmbientLight {
 // Keep AUTO at least as bright as the former fixed150 setting. Lower
 // settings remain available only through an explicit manual override.
 static constexpr uint8_t Levels[5]={150,160,192,224,255};
 static constexpr uint32_t Edges[4]={5,50,300,1000};
 LightState s_{};uint64_t last_=0,poll_=0,step_=0,pending_since_=0;
 uint32_t filtered256_=0;uint16_t ring_[3]{};unsigned n_=0,head_=0,band_=2,pending_=2;
 bool have_=false;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 void reset_filter(){have_=false;n_=head_=0;pending_=band_;pending_since_=0;s_.fresh=false;}
 void sample(uint16_t ch0,uint16_t ch1,uint64_t now){
  if(have_&&(now<s_.sample_us||now-s_.sample_us>750000))reset_filter();
  s_.ch0=ch0;s_.ch1=ch1;s_.raw=uint16_t((uint32_t(ch0)+ch1)/2);
  s_.saturated=ch0>=65500||ch1>=65500;
  ring_[head_]=s_.saturated?UINT16_MAX:s_.raw;head_=(head_+1)%3;n_=std::min(n_+1,3u);
  uint16_t ordered[3]{};std::copy(ring_,ring_+n_,ordered);std::sort(ordered,ordered+n_);
  const uint32_t value=uint32_t(ordered[n_/2])*256;
  // A large illumination change must not leave a linear EMA stuck near its
  // former full-scale reading. Median3 rejects spikes; the direction dwell
  // below still applies after reseeding a change larger than4x.
  filtered256_=!have_||value>filtered256_*4||filtered256_>value*4?value:(3*filtered256_+value+2)/4;
  s_.filtered=uint16_t((filtered256_+128)/256);s_.sample_us=now;s_.fresh=have_=true;inc(s_.samples);
  unsigned candidate=band_;
  while(candidate<4&&uint32_t(s_.filtered)*5>Edges[candidate]*6)++candidate;
  while(candidate>0&&uint32_t(s_.filtered)*5<Edges[candidate-1]*4)--candidate;
  if(candidate==band_){pending_=band_;pending_since_=0;return;}
  if(candidate!=pending_||!pending_since_){pending_=candidate;pending_since_=now;return;}
  if(now>=pending_since_&&now-pending_since_>=(candidate>band_?500000u:3000000u)){
   band_=candidate;pending_since_=0;
  }
 }
 public:
 static constexpr uint8_t Address=0x23;
 static uint8_t code(uint8_t value){return value?uint8_t((unsigned(value)+641)>>5):0;}
 template<class Bus>void begin(uint64_t now,Bus& bus){
  last_=poll_=step_=now;uint8_t id[2]{},ps=0,control=0,rate=0;
  if(bus.read(Address,0x86,id,2)){s_.part=id[0];s_.manufacturer=id[1];s_.present=(id[0]&0xf0)==0x90&&id[1]==5;}
  if(s_.present){
   s_.configured=bus.read(Address,0x81,&ps,1)&&bus.write(Address,0x81,uint8_t(ps&~3u))&&
    bus.write(Address,0x80,0)&&bus.write(Address,0x85,2)&&bus.write(Address,0x80,1)&&
    bus.read(Address,0x80,&control,1)&&bus.read(Address,0x85,&rate,1)&&
    bus.read(Address,0x81,&ps,1)&&control==1&&rate==2&&!(ps&3);
  }
  if(!s_.configured)inc(s_.errors);
  s_.verified=bus.brightness(150,s_.pmic_code);if(s_.verified)inc(s_.writes);else inc(s_.write_errors);
 }
 void automatic(){s_.automatic=true;pending_=band_;pending_since_=0;}
 bool manual(unsigned value){if(value<32||value>255)return false;s_.automatic=false;s_.target=uint8_t(value);return true;}
 template<class Bus>void poll(uint64_t now,bool awake,Bus& bus){
  if(!awake)return; // Sleep owns the final zero; never relight during drain.
  if(now<last_){reset_filter();poll_=step_=now;}last_=now;
  if(s_.configured&&now>=poll_&&now-poll_>=250000){
   poll_=now;uint8_t status=0,data[4]{};
   if(!bus.read(Address,0x8c,&status,1))inc(s_.errors);
   else if((status&0xf0)!=0)inc(s_.invalid); // Invalid data or unexpected gain.
   else if(!(status&4))inc(s_.old); // Old data never renews freshness.
   else if(!bus.read(Address,0x88,data,4))inc(s_.errors);
   else sample(uint16_t(data[2]|unsigned(data[3])<<8),uint16_t(data[0]|unsigned(data[1])<<8),now);
  }
  s_.fresh=have_&&now>=s_.sample_us&&now-s_.sample_us<2000000;
  if(!s_.fresh){pending_=band_;pending_since_=0;}
  if(s_.automatic)s_.target=s_.fresh?Levels[band_]:150;
  const auto actual=code(s_.current),target=code(s_.target);
  if(s_.verified&&actual==target){s_.current=s_.target;return;}
  const uint64_t interval=!s_.verified?1000000:target>actual?250000:500000;
  if(now<step_||now-step_<interval)return;
  step_=now;
  const auto next=uint8_t(!s_.verified?code(150):target>actual?actual+1:actual-1);
  const auto value=!s_.verified?uint8_t(150):next==target?s_.target:uint8_t(std::min(255u,unsigned(next-20)*32));
  uint8_t reported=255;
  if(bus.brightness(value,reported)){s_.current=value;s_.pmic_code=reported;s_.verified=true;inc(s_.writes);}
  else{s_.verified=false;s_.pmic_code=reported;inc(s_.write_errors);}
 }
 const LightState& state()const{return s_;}
};
}
