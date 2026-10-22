// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fine_counter.hpp"
#include <cassert>
#include <cstdio>
using namespace n30::fine;
int main(){
 Counter c;Time t{},e{};
 assert(!c.begin(0,1,1,80000000)&&!c.begin(1,1,1,20000000));
 const uint64_t origin=0xfffff000U;uint64_t extended=origin,us=1000;
 assert(c.begin(7,uint32_t(extended),us,80000000));
 for(unsigned i=0;i<200000;++i){
  extended+=10000*80;us+=10000;assert(c.current(7,uint32_t(extended),us,t));
  assert(t.raw80==extended&&t.tick20==1000000+(extended-origin)/4);
  const uint64_t age=uint64_t(i%10000)*80;
  if(extended-age>=origin){assert(c.event(7,uint32_t(extended-age),e));assert(e.raw80==extended-age);}
  assert(!c.event(7,uint32_t(extended+1),e));assert(!c.event(6,uint32_t(extended),e));
 }
 assert(!c.event(7,uint32_t(extended-250001*80),e));
 assert(!c.current(7,uint32_t(extended),us+Counter::HalfWrapUs,t)&&!c.live());
 assert(c.begin(1,100,1000,80000000));assert(c.current(1,100,1050,t));assert(c.current(1,100,1100,t));
 assert(!c.current(1,100,1101,t)); // repeated short reads cannot hide stopped timer.
 assert(c.begin(1,100,1000,80000000));assert(!c.current(1,40100,2000,t)); // half rate
 assert(c.begin(1,100,1000,80000000));assert(!c.current(1,160100,2000,t)); // double rate
 assert(c.begin(1,100,1000,80000000));assert(!c.current(1,180,999,t));
 assert(c.begin(1,100,1000,80000000));assert(!c.current(2,180,1001,t));
 History h;Edge rows[4]{};assert(!h.start(0,1000,1000,1)&&!h.start(1,1000,999,1)&&!h.start(1,1000,1000,0));
 assert(h.start(1,1000,1000,2)&&!h.start(2,1001,1000,1));
 for(unsigned i=0;i<80;++i){assert(h.active(1000+i));h.append({0,0,1000+i,0,i,1,uint8_t(i%2),1,true});}
 assert(h.seen()==80&&h.rx()==40&&h.tx()==40&&h.sequence()==40&&h.overwritten()==8&&h.oldest()==9);
 assert(h.read(0,rows,4)==4&&rows[0].number==9&&rows[3].number==12);
 assert(h.read(0,rows,4)==4&&rows[0].number==9);assert(h.read(40,rows,4)==0);
 assert(h.active(1000999)&&!h.active(1001000));h.append({});assert(h.seen()==80);
 assert(h.start(2,2000000,1000,1));assert(!h.active(1999999)); // guard reversal once serviced
 std::puts("PASS common80 arithmetic:200000 delayed events/37wraps; stale/stopped/rate/reversal/incarnation; ringdecimation/overwrite/expiry/nonconsumingcursor");
}
