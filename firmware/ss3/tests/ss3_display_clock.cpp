#include "ss3_display_clock.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>
int main(){using C=sticks3::DisplayClock;C c;
 c.tick=20000000;c.until=c.tick+20000000;c.model_until=c.tick+1000000;
 c.utc_ns=1791981234000000000LL;c.model.tick=c.tick;c.model.epoch=c.utc_ns;
 int64_t utc=0;
 assert(c.project(true,true,c.tick,utc)==C::Source::Fine&&utc==c.utc_ns);
 assert(c.project(true,true,c.model_until-1,utc)==C::Source::Fine);
 // Crossing fine permission keeps canonical held time without fresh quality.
 assert(c.project(true,true,c.model_until,utc)==C::Source::Coarse&&utc==c.utc_ns+50000000);
 for(uint64_t t=c.model_until;t<c.until;t+=1000)assert(c.project(true,true,t,utc)==C::Source::Coarse&&utc==c.utc_ns+int64_t(t-c.tick)*50);
 assert(c.project(true,true,c.until,utc)==C::Source::Invalid&&utc==0);
 assert(c.project(false,true,c.tick,utc)==C::Source::Invalid);
 assert(c.project(true,false,c.tick,utc)==C::Source::Invalid);
 assert(c.project(true,true,c.tick-1,utc)==C::Source::Invalid);
 c.model.rate=NAN;assert(c.project(true,true,c.tick,utc)==C::Source::Coarse);
 c.model={};assert(c.project(true,true,c.tick,utc)==C::Source::Coarse);
 c.utc_ns=INT64_MAX-49;assert(c.project(true,true,c.tick+1,utc)==C::Source::Invalid);
 assert(C::deadline(100,500,550)==1100);
 assert(C::deadline(100,500,500)==0);
 assert(C::deadline(UINT64_MAX-10,500,550)==0);
 assert(C::deadline(100,0,UINT64_MAX)==0);
 puts("PASS fine expiry retains canonical held clock; original expiry, invalid context/timer/model and overflow fences");
}
