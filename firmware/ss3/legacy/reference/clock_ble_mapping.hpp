#pragma once
#include <algorithm>
#include <cmath>
#include "clock_s3_rx_descriptor.hpp"

namespace adv_rx {
// BLE and GPTimer share the crystal. Average the noisy phase of their
// correlation, rather than imposing every 500 ns ROM clock quantizer on D1.
// This does not invent sub-tick RF timestamps or remove descriptor quantization.
class BleMapping {
public:
 void reset(){*this=BleMapping{};}
 bool observe(const BleAnchor& a,int32_t& correction){
  correction=0;
  if(!a.before || a.after<a.before || a.after-a.before>400 ||
     a.time.half_slots>BleClockMask || a.time.half_us>624){reset();return false;}
  const auto mid=a.before+(a.after-a.before)/2;
  const auto width=a.after-a.before;
  const uint32_t wrap=(a.time.half_slots-last_time_.half_slots)&BleClockMask;
  const int64_t slots=wrap&(1u<<27)?int64_t(wrap)-(1LL<<28):wrap;
  const int64_t delta=slots*625+int64_t(a.time.half_us)-last_time_.half_us;
  if(!last_ || mid<=last_ || mid-last_>5000000 || delta<=0 || delta>500000 ||
     width+20<width_){
   reset();origin_=mid;width_=width;
  }else{
   elapsed_+=delta*10;
  }
  last_=mid;last_time_=a.time;
  // Wide calls have different midpoint bias; do not contaminate narrow calls.
  if(width>width_+20)return false;
  const int64_t offset=int64_t(mid-origin_)-elapsed_;
  if(count_ && std::abs(double(offset)-double(sum_)/count_)>40){
   reset();origin_=last_=mid;last_time_=a.time;width_=width;
   offsets_[0]=0;count_=1;next_=1;return false;
  }
  if(count_==16)sum_-=offsets_[next_];else ++count_;
  offsets_[next_]=offset;sum_+=offset;next_=(next_+1)%16;
  const double adjust=double(sum_)/count_-offset;
  if(count_<4 || mid-origin_<1500000 || std::abs(adjust)>10)return false;
  correction=int32_t(std::llround(adjust));return true;
 }
 unsigned samples() const{return count_;}
private:
 BleTime last_time_{};uint64_t origin_=0,last_=0,width_=0;
 int64_t elapsed_=0,offsets_[16]{},sum_=0;
 unsigned count_=0,next_=0;
};
}
