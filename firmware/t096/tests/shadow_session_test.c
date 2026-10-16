#include "shadow_session.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_shadow_session s={0};struct r2_shadow_session_view v;
 assert(!r2_shadow_session_step(&s,true,false,true,100));
 assert(!r2_shadow_session_step(&s,true,false,true,1099));
 assert(!r2_shadow_session_step(&s,true,false,false,1100)&&s.state==R2_SHADOW_WAIT_PPS);
 assert(r2_shadow_session_step(&s,true,false,true,1200));
 assert(!r2_shadow_session_begin(&s,1200,0,1,50));
 assert(r2_shadow_session_begin(&s,1200,100,1,50));
 r2_shadow_session_sample(&s,51,99,1,true,-100);assert(!s.samples&&s.skipped==1);
 r2_shadow_session_sample(&s,52,101,1,false,100);assert(s.skipped==2);
 r2_shadow_session_sample(&s,53,102,2,true,100);assert(s.skipped==3);
 const int phases[5]={-10,-3,0,2,20};
 for(unsigned i=0;i<5;i++)r2_shadow_session_sample(&s,55+i,104+i,1,true,phases[i]);
 r2_shadow_session_sample(&s,59,108,1,true,999);assert(s.samples==5&&s.missing==1);
 assert(!r2_shadow_session_step(&s,true,false,true,2200));r2_shadow_session_view(&s,&v);
 assert(v.valid&&v.samples==5&&v.mean_tenth_ns==18&&v.p95_ns==18&&v.p99_ns==20&&v.max_ns==20&&!v.approximate&&v.elapsed_ms==1000);
 assert(!r2_shadow_session_step(&s,false,false,true,2300)&&!s.active);uint32_t n=s.samples;
 r2_shadow_session_sample(&s,60,109,1,true,100);assert(s.samples==n);
 assert(!r2_shadow_session_step(&s,true,true,true,2400)&&s.state==R2_SHADOW_GM&&!s.samples);
 assert(!r2_shadow_session_step(&s,true,false,true,2500)&&s.samples==0);
 assert(r2_shadow_session_step(&s,true,false,true,3500));assert(r2_shadow_session_begin(&s,3500,200,2,100));
 for(unsigned i=0;i<2000;i++)r2_shadow_session_sample(&s,101+i,201+i,2,true,2000);
 r2_shadow_session_view(&s,&v);assert(v.samples==2000&&v.mean_tenth_ns==20000&&v.max_ns==2000&&v.approximate);
 assert(sizeof(s)<4500);
 puts("PASS full-second dwell, actual start boundary, invalid/reference/epoch exclusions, all-session mean and tails, navigation/GM stop, fresh reentry, bounded2000sample quantiles");
}
