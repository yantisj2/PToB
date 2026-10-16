#include "link_quality.h"
#include <assert.h>
#include <stdio.h>
static struct r2_link_quality q;
static uint64_t now;
static uint16_t event;
static void sample(unsigned delivered){
 for(unsigned n=0;n<16;n++){
  if(n<delivered)r2_link_quality_rx(&q,1,event,now);
  ++event;now+=62500;r2_link_quality_tick(&q,true,now);
 }
}
int main(void){
 now=1000000;r2_link_quality_bind(&q,11,22,33);r2_link_quality_tick(&q,true,now);
 for(unsigned i=0;i<6;i++)sample(16);
 assert(q.view.healthy&&!q.view.poor&&q.view.delivery_per_mille==1000);
 sample(0);sample(0);assert(!q.view.poor); /* two-second outage retains parent */
 for(unsigned i=0;i<16;i++)sample(16);
 assert(q.view.healthy&&!q.view.poor);
 for(unsigned i=0;i<8;i++)sample(8);
 assert(q.view.poor&&!q.view.healthy);
 sample(16);assert(q.view.poor); /* one recovered window is insufficient */
 for(unsigned i=0;i<16;i++)sample(16);
 assert(q.view.healthy&&!q.view.poor);
 uint32_t received=q.view.received;
 r2_link_quality_rx(&q,1,(uint16_t)(event-1),now);assert(q.view.received==received);
 r2_link_quality_rx(&q,1,(uint16_t)(event-2),now);assert(q.view.received==received);
 event=65530;q.event=65529;
 sample(16);assert(q.view.received==received+16); /* wrap */
 r2_link_quality_tick(&q,false,now);now+=10000000;r2_link_quality_tick(&q,false,now);
 assert(!q.view.poor); /* local pause is not RF loss */
 r2_link_quality_tick(&q,true,now);sample(16);assert(!q.view.poor);
 received=q.view.received;now+=3000000;r2_link_quality_tick(&q,true,now);
 r2_link_quality_rx(&q,1,(uint16_t)(q.event+40000u),now);assert(q.view.received==received+1);
 r2_link_quality_bind(&q,11,44,33);assert(!q.view.samples&&!q.view.received&&!q.view.poor&&!q.view.healthy);
 r2_link_quality_tick(&q,true,now);
 for(unsigned i=0;i<4;i++)sample(0);assert(!q.view.poor);sample(0);assert(q.view.poor);
 puts("PASS delivery thresholds, sustained loss/recovery, short outage, wrap/duplicate/reorder, pause and scope reset");
}
