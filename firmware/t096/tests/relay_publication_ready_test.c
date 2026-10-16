/* Exact production LIVE predicate at its public capture/map boundaries. */
#include "relay_service.h"
#include <assert.h>
#include <stdio.h>
static struct r2_relay_precision timing;
static bool selected,active,closing,pa_started,ext_started,coarse_valid;
static unsigned own=1,relay=1;
static int error;
static void *pa;
static uint64_t activation_until,data_until_tick;
static bool captured=true,map_valid=true;
static struct r2_capture physical;
bool r2_capture_now(struct r2_capture *out){*out=physical;return captured;}
bool r2_relay_precision_read(const struct r2_relay_precision *s,struct r2_capture now,uint64_t us,int64_t *ns,int64_t *utc){return map_valid;}
#include "relay_ready_under_test.h"
static void ready(void){
 selected=active=pa_started=ext_started=coarse_valid=true;closing=false;error=0;
 pa=&timing;own=relay=1;captured=map_valid=true;
 activation_until=2000000;data_until_tick=32000000;
 physical=(struct r2_capture){16000000,1,R2_CAPTURE_NOW};
 timing=(struct r2_relay_precision){.epoch=1,.phase_admitted=true,.sent_count=1,.last_tx_tick=15000000};
}
int main(void){
 ready();assert(r2_relay_service_serving(1000000));
 for(unsigned i=0;i<15;i++){
  ready();switch(i){
   case 0:pa=NULL;break;case 1:pa_started=false;break;case 2:ext_started=false;break;
   case 3:closing=true;break;case 4:error=-1;break;case 5:own=2;break;
   case 6:activation_until=1000000;break;case 7:data_until_tick=physical.tick;break;
   case 8:timing.sent_count=0;break;case 9:timing.phase_admitted=false;break;
   case 10:timing.last_tx_tick=physical.tick-R2_RADIO_MAX_AGE_TICKS;break;
   case 11:physical.epoch++;break;case 12:map_valid=false;break;
   case 13:timing.passive_receiver=true;break;case 14:captured=false;break;
  }
  assert(!r2_relay_service_serving(1000000));
 }
 puts("PASS actual LIVE gate: advertiser allocation/start, captured TX age, map/permit/epoch expiry, leaf/passive role and faults");
 return 0;
}
