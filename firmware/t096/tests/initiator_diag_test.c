/* Production passive counter/export boundary: no radio or hardware calls. */
#include <assert.h>
#include <stdio.h>
#define R2_TIMING_EVENTS_TEST 1
#include "initiator_diag.h"
#include "timing_events.h"
static unsigned emitted;
static struct {unsigned kind,detail,a,b;int result;uint64_t peer;} events[5];
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{assert(emitted<5);events[emitted++]=(typeof(events[0])){k,d,a,b,r,p};}
#include "../src/initiator_diag.c"
int main(void)
{
 struct r2_init_diag before,after;r2_init_diag_snapshot(&before);
 const uint8_t address[6]={1,2,3,4,5,0xc6};
 r2_init_diag_target(address,1,2);
 for(unsigned i=0;i<9;i++)r2_init_diag_count(R2_INIT_RX);
 for(unsigned i=0;i<3;i++)r2_init_diag_count(R2_INIT_MATCH);
 r2_init_diag_count(R2_INIT_BUFFER);r2_init_diag_count(R2_INIT_WINDOW);
 r2_init_diag_count(R2_INIT_REQUEST);r2_init_diag_count(R2_INIT_ABORT);
 r2_init_diag_count(R2_INIT_COUNTERS);r2_init_diag_emit(123,2,&before);
 assert(emitted==4&&events[0].detail==513&&events[0].a==0x04030201&&events[0].b==0xc605);
 assert(events[1].detail==1&&events[1].a==9&&events[1].b==3);
 assert(events[2].detail==1&&events[2].a==1&&events[2].b==1&&events[3].detail==1);
 for(unsigned i=0;i<4;i++)assert(events[i].peer==123&&events[i].result==2);
 r2_init_diag_snapshot(&after);assert(after.count[R2_INIT_RX]==9);
 state.count[R2_INIT_RX]=UINT32_MAX;emitted=0;r2_init_diag_count(R2_INIT_RX);
 r2_init_diag_emit(123,0,&after);assert(state.count[R2_INIT_RX]==UINT32_MAX);
 assert(events[0].detail&(1u<<31));
 r2_init_diag_snapshot(&before);emitted=0;
 r2_init_diag_starved(3,4);r2_init_diag_starved(2,4);r2_init_diag_emit(123,2,&before);
 assert(emitted==5&&events[4].kind==R2_TIMING_INIT_RX_RESERVE&&events[4].detail==4&&events[4].a==2&&events[4].b==2);
 /* Fresh MCU state establishes a real low-water value, not an initial zero. */
 state=(struct r2_init_diag){0};r2_init_diag_starved(3,4);r2_init_diag_snapshot(&after);
 assert(after.count[R2_INIT_BUFFER]==1&&after.rx_low==3&&after.rx_free==3&&after.rx_needed==4);
 puts("PASS initiator counter baselines, matching/blocked/request separation, target binding and explicit saturation");
}
