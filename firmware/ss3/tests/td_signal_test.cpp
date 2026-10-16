#include "td_signal.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using v12::TdSignal;
 TdSignal s;s.observe(-67,0);assert(s.value==-67&&s.bars==3);
 for(uint64_t t=10000;t<=10000000;t+=10000){s.observe((t/10000)%2?-66:-68,t);assert(s.bars==3&&s.value==-67);}
 auto last=s.display_us;unsigned changes=0;
 for(uint64_t t=10010000;t<=15000000;t+=10000){auto value=s.value;auto bars=s.bars;s.observe(-40,t);if(value!=s.value||bars!=s.bars){assert(t-last>=TdSignal::DisplayUs);++changes;last=t;}}
 assert(changes&&s.bars==4&&s.value>=-44);
 TdSignal low,high;low.observe(-68,1);high.observe(-66,1);
 for(uint64_t t=10001;t<5000001;t+=10000){low.observe(-66,t);high.observe(-68,t);assert(low.bars==2&&high.bars==3);}
 for(uint64_t t=5000001;t<10000001;t+=10000){low.observe(-58,t);high.observe(-78,t);}
 assert(low.bars==3&&high.bars==2);
 const auto seen=s.seen_us;const auto shown=s.value;s.observe(127,seen+9000000);assert(s.seen_us==seen);assert(s.fresh(seen+9999999)&&!s.fresh(seen+10000000));s.observe(-90,seen-1);assert(s.value==shown&&s.seen_us==seen);s.observe(-90,seen+10000000);assert(s.value==-90&&s.bars==1&&s.weak);
 for(uint64_t t=seen+10010000;t<seen+16000000;t+=10000)s.observe(-80,t);assert(!s.weak);
 TdSignal invalid;invalid.observe(127,1);assert(!invalid.fresh(1)&&invalid.value==127);invalid.observe(126,1);invalid.observe(-128,1);assert(!invalid.initialized);
 TdSignal saturated;saturated.observe(-127,1);for(unsigned i=0;i<100000;i++)saturated.observe(-127,2);saturated.observe(-127,1000001);assert(saturated.value==-127&&saturated.bars==1);
 puts("PASS 1Hz publication, 2dB deadband, 3dB Schmitt bars, weak-state recovery, invalid/stale/out-of-order/saturated samples");
}
