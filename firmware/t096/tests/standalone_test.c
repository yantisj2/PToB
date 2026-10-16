/* Every roster position can serve alone from fresh coarse GNSS. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
static struct r2_network net;
int main(void){
 unsigned fractions[]={0,100000,999999};
 for(unsigned self=0;self<3;self++)for(unsigned f=0;f<3;f++){
  assert(!r2_network_init(&net,self,3,100+self,200+self,300+self,40000,1000000));
  r2_network_capabilities(&net,7,7);
  net.relative_source=f==1; /* Explicit synthetic hardware capability for this case. */
  for(uint64_t t=1000000;t<601000000;t+=20000){
   uint64_t arrival=t/1000000*1000000;
   struct r2_clock c={0};c.view.valid=c.view.fix_active=c.view.pps_present=true;
   c.view.source_epoch=29;c.last_rmc=arrival;c.last_pps=arrival;
   c.last_rmc_utc=1791540000000000LL+arrival+fractions[f];
   r2_network_tick(&net,&c,t);
   if(t>=65000000){struct v12_dc_snapshot s;r2_network_snapshot(&net,&s);
    if(!s.local_gm_ready){fprintf(stderr,"FAIL standalone self=%u fraction=%u at=%llu local_available=%d phase=%u root=%u\n",self,fractions[f],(unsigned long long)t,net.candidate.available,s.phase,s.elected_root);return 1;}
    assert(!s.coarse.qualified&&s.elected_root==self&&s.coordinator_wire==255&&s.hot_standby==255);
    assert(s.relative_precision_permitted==net.relative_source&&!s.precision_permitted);
    assert(!s.serving&&!s.service_acks&&!s.service_receipts);
    uint64_t delivered=s.coarse.utc_second*1000000+t-s.coarse.evidence_us;
    assert(delivered==(uint64_t)c.last_rmc_utc+t-arrival);
   }
  }
  struct r2_clock lost={0};
  r2_network_tick(&net,&lost,601000000);
  struct v12_dc_snapshot withdrawn;r2_network_snapshot(&net,&withdrawn);
  assert(!withdrawn.local_gm_ready&&!withdrawn.serving&&!withdrawn.precision_permitted&&!withdrawn.relative_precision_permitted);
 }
 puts("PASS standalone: all3 roster positions,10min each,integer/fractional RMC preserved,continuous unqualified service");
}
