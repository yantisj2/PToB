/* Production passive counter/export boundary: no radio or hardware calls. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define R2_TIMING_EVENTS_TEST 1
#include "initiator_diag.h"
#include "timing_events.h"
static unsigned emitted;
static struct {unsigned kind,detail,a,b;int result;uint64_t peer;} events[8];
void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{assert(emitted<8);events[emitted++]=(typeof(events[0])){k,d,a,b,r,p};}
#include "../src/initiator_diag.c"
int main(void)
{
 char empty[512]="{}";assert(r2_init_diag_append(empty,sizeof empty,2)>0&&strstr(empty,"\"valid\":false"));
 struct r2_init_diag before,after;r2_init_diag_snapshot(&before);
 const uint8_t address[6]={1,2,3,4,5,0xc6};
 r2_init_diag_target(address,1,2);
 for(unsigned i=0;i<9;i++)r2_init_diag_count(R2_INIT_RX);
 for(unsigned i=0;i<3;i++)r2_init_diag_count(R2_INIT_MATCH);
 r2_init_diag_count(R2_INIT_BUFFER);r2_init_diag_count(R2_INIT_WINDOW);
 r2_init_diag_count(R2_INIT_REQUEST);r2_init_diag_count(R2_INIT_ABORT);
 r2_init_diag_count(R2_INIT_COUNTERS);r2_init_diag_emit(123,2,&before);
 assert(emitted==7&&events[0].detail==513&&events[0].a==0x04030201&&events[0].b==0xc605);
 assert(events[1].detail==1&&events[1].a==9&&events[1].b==3);
 assert(events[2].detail==1&&events[2].a==1&&events[2].b==1&&events[3].detail==1);
 for(unsigned i=0;i<7;i++)assert(events[i].peer==123&&events[i].result==2);
 r2_init_diag_snapshot(&after);assert(after.count[R2_INIT_RX]==9);
 state.count[R2_INIT_RX]=UINT32_MAX;emitted=0;r2_init_diag_count(R2_INIT_RX);
 r2_init_diag_emit(123,0,&after);assert(state.count[R2_INIT_RX]==UINT32_MAX);
 assert(events[0].detail&(1u<<31));
 r2_init_diag_snapshot(&before);emitted=0;
 r2_init_diag_starved(3,4);r2_init_diag_starved(2,4);r2_init_diag_emit(123,2,&before);
 assert(emitted==8&&events[4].kind==R2_TIMING_INIT_RX_RESERVE&&events[4].detail==4&&events[4].a==2&&events[4].b==2);
 /* Fresh MCU state establishes a real low-water value, not an initial zero. */
 state=(struct r2_init_diag){0};r2_init_diag_starved(3,4);r2_init_diag_snapshot(&after);
 assert(after.count[R2_INIT_BUFFER]==1&&after.rx_low==3&&after.rx_free==3&&after.rx_needed==4);
 r2_init_diag_aux_rsp(false,false,false);r2_init_diag_aux_rsp(true,false,false);
 r2_init_diag_aux_rsp(true,true,false);r2_init_diag_aux_rsp(true,true,true);
 r2_init_diag_snapshot(&after);
 for(unsigned i=R2_INIT_AUX_RSP_OK;i<R2_INIT_COUNTERS;i++)assert(after.count[i]==1);
 // Frozen last-attempt evidence survives subsequent ISR counter changes and
 // event-ring overwrite. The formatter runs only in the main thread.
 emitted=0;r2_init_diag_emit(UINT64_MAX,INT32_MIN,&before);
 char json[2977]="{}";int n=r2_init_diag_append(json,sizeof json,2);assert(n>0);
 assert(strstr(json,"\"schema\":2")&&strstr(json,"ffffffffffffffff")&&strstr(json,"-2147483648"));
 char frozen[2977];strcpy(frozen,json);r2_init_diag_count(R2_INIT_MATCH);
 strcpy(json,"{}");assert(r2_init_diag_append(json,sizeof json,2)==n&&!strcmp(json,frozen));
 assert(r2_init_diag_append(json,8,2)<0&&r2_init_diag_append(json,sizeof json,0)<0);
 for(unsigned i=0;i<R2_INIT_COUNTERS;i++)state.count[i]=UINT32_MAX;
 memset(&before,0,sizeof before);emitted=0;r2_init_diag_emit(UINT64_MAX,INT32_MIN,&before);
 // Existing worst-width RC-parent/capsule fields have over2KB available;
 // append directly to the shared reply buffer, with no512B stack scratch.
 memset(json,'x',2070);memcpy(json,"{\"padding\":\"",12);json[2068]='\"';json[2069]='}';json[2070]=0;
 n=r2_init_diag_append(json,sizeof json,2070);assert(n>2070&&n<2976);
 assert(strstr(json,"\"saturated\":true"));
 last.serial=UINT32_MAX;emitted=0;r2_init_diag_emit(7,0,&before);
 assert(last.serial==UINT32_MAX&&last.peer==7&&!last.result);
 puts("PASS initiator counter baselines, matching/blocked/request separation, target binding and explicit saturation");
}
