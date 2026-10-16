#include "g1_quality_display.hpp"
#include <cassert>
#include <cstdio>
using v12::G1QualityDisplay;
constexpr uint64_t Hz=20000000;
int main(){
  G1QualityDisplay d;
  auto q=d.update(Hz,1,300,10000,true);assert(q.rms_ns==300&&!q.hold_ms&&!q.locked);
  q=d.update(2*Hz,1,280,9000,true);assert(q.rms_ns==300&&!q.hold_ms&&!q.locked);
  q=d.update(3*Hz,1,290,8000,true);assert(q.rms_ns==300&&q.hold_ms==4000&&q.locked);
  // Quiet time counts down the previously displayed projection, even when
  // another raw estimate jumps upward. No frozen display extends that expiry.
  q=d.update(4*Hz,1,270,30000,true);assert(q.rms_ns==290&&q.hold_ms==3000&&q.locked);
  q=d.update(5*Hz,1,260,30000,true);assert(q.hold_ms==2000);
  q=d.update(6*Hz,1,250,30000,true);assert(q.hold_ms==5000&&q.rms_ns==290);
  q=d.update(7*Hz,1,500,0,false);assert(q.rms_ns==500&&!q.hold_ms&&!q.locked);
  // A brief radio recovery cannot erase a worse estimate or regain LOCK.
  q=d.update(7*Hz+Hz/2,1,240,10000,true);assert(q.rms_ns==500&&!q.hold_ms&&!q.locked);
  q=d.update(8*Hz,1,250,0,false);assert(q.rms_ns==500&&!q.hold_ms&&!q.locked);
  q=d.update(9*Hz,1,230,10000,true);assert(!q.locked&&!q.hold_ms);
  q=d.update(11*Hz,1,220,8000,true);assert(q.locked&&q.hold_ms==4000);
  // A replacement clock population never inherits old displayed confidence.
  q=d.update(12*Hz,2,100,5000,true);assert(q.rms_ns==100&&!q.hold_ms&&!q.locked);
  q=d.update(14*Hz,2,120,5000,true);assert(q.rms_ns==120&&q.hold_ms==4000&&q.locked);
  q=d.update(13*Hz,2,110,5000,true);assert(q.rms_ns==110&&!q.hold_ms&&!q.locked);
  q=d.update(15*Hz,0,100,5000,true);assert(q.rms_ns==UINT32_MAX&&!q.hold_ms&&!q.locked);
  q=d.update(16*Hz,3,UINT32_MAX,5000,true);assert(q.rms_ns==UINT32_MAX&&!q.hold_ms&&!q.locked);
  d.update(17*Hz,3,100,1000,true);q=d.update(19*Hz,3,100,999,true);assert(!q.locked&&q.hold_ms<=999);
  q=d.update(UINT64_MAX-10,4,100,UINT32_MAX,true);assert(!q.hold_ms&&!q.locked);
  d.clear();uint32_t prior=0;
  for(unsigned seconds=1;seconds<=30;++seconds){
    q=d.update(seconds*Hz,5,100,30000,true);
    assert(q.hold_ms<=30000);
    if(seconds>3)assert(q.hold_ms>0);
    if(seconds%3==0){assert(q.hold_ms>prior);prior=q.hold_ms;}
  }
  std::puts("PASS settled conservative RMS/hold/lock, countdown, rapid recovery, downgrade, scope/clock/exhaustion fences");
}
