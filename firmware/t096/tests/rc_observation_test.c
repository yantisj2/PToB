/* Independent observation math checks; never a precision acceptance score. */
#include "../sim/rc_observation.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
int main(void){
 unsigned count=0;
 for(int ppm=-50;ppm<=50;ppm+=25){
  for(unsigned boot=0;boot<4;boot++){
   uint64_t start=boot*1234567u;
   long double offset=boot*0.123456789L;
   for(uint64_t i=1;i<20000;i++){
    /* Independent physical event time, including >32bit counter wrap. */
    long double phy=(long double)i*.031250037L+offset;
    long double local=(1.0L+(long double)ppm/1000000)*phy-offset;
    if(local*1e6L<start)continue;
    uint64_t tick;assert(rc_observation_tick(local*1e6L,start,&tick));
    long double recovered=((long double)start+(long double)tick/16)*1e-6L;
    long double error=(local-recovered)*1e9L;
    assert(error>-0.0001L&&error<62.5001L);
    uint32_t raw=(uint32_t)tick;
    uint64_t now=tick+32000;
    assert(now-(uint32_t)((uint32_t)now-raw)==tick);
    uint64_t rx;assert(rc_observation_tick(local*1e6L+9,start,&rx));
    assert(rx-tick==144);
    count++;
   }
  }
 }
 uint64_t tick;assert(!rc_observation_tick(-1,0,&tick));
 assert(!rc_observation_tick(1,2,&tick));
 assert(!rc_observation_tick((long double)UINT64_MAX,0,&tick));
 printf("observation_math PASS cases=%u oscillator_offsets_boot_wrap=true max_quantization_ns=62.5\n",count);
}
