#include "precision_servo.h"
static unsigned rms_calls;
#define r2_servo_rms_floor(...) (rms_calls++,r2_servo_rms_floor(__VA_ARGS__))
#include "shadow_session.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_shadow_session s={0};struct r2_shadow_session_view v;
 assert(!r2_shadow_session_step(&s,true,false,true,100));
 assert(!r2_shadow_session_step(&s,true,false,true,1099));
 assert(!r2_shadow_session_step(&s,true,false,false,1100)&&s.state==R2_SHADOW_WAIT_PPS);
 assert(r2_shadow_session_step(&s,true,false,true,1200));
 /* Nonzero startup work shares this storage. No phantom bins may survive. */
 memset(s.histogram,0xa5,sizeof s.histogram);
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
 assert(v.rms_ns==10); /* sqrt((100+9+0+4+400)/5), no bias removal. */
 assert(!r2_shadow_session_step(&s,false,false,true,2300)&&!s.active);uint32_t n=s.samples;
 r2_shadow_session_sample(&s,60,109,1,true,100);assert(s.samples==n);
 assert(!r2_shadow_session_step(&s,true,true,true,2400)&&s.state==R2_SHADOW_GM&&!s.samples);
 assert(!r2_shadow_session_step(&s,true,false,true,2500)&&s.samples==0);
 assert(r2_shadow_session_step(&s,true,false,true,3500));assert(r2_shadow_session_begin(&s,3500,200,2,100));
 for(unsigned i=0;i<2000;i++)r2_shadow_session_sample(&s,101+i,201+i,2,true,2000);
 r2_shadow_session_view(&s,&v);assert(v.samples==2000&&v.mean_tenth_ns==20000&&v.max_ns==2000&&v.approximate);
 assert(v.rms_ns==2000);
 /* Cross the 64-bit sum limit with retained half-second errors. */
 memset(&s,0,sizeof s);s.entered=true;assert(r2_shadow_session_begin(&s,1,1,1,0));
 for(unsigned i=1;i<=200;i++)r2_shadow_session_sample(&s,i,i+1,1,true,i%2?500000000:-500000000);
 r2_shadow_session_view(&s,&v);assert(v.mean_tenth_ns==0&&v.rms_ns==500000000&&s.squares_hi);
 /* Maximum representable population, exact 96-bit accumulated square. */
 s.samples=UINT32_MAX;s.sum=0;s.squares_hi=0x03782dac;s.squares_lo=UINT64_C(0xe660d25316270000);
 r2_shadow_session_view(&s,&v);assert(v.rms_ns==500000000);
 assert(sizeof(s)<4500);
 struct r2_shadow_settle settle={0};struct rc_shadow_status st={.epoch=1};r2_shadow_settle_begin(&settle,&st);
 struct rc_shadow_record r={.edge={.epoch=1,.kind=R2_CAPTURE_PPS},.map={.root=1,.root_boot=2,.root_epoch=3},.phase_ns=-1640000};
 for(unsigned i=1;i<=10;i++){r.sequence=i;r.edge.tick=(uint64_t)i*16000000;assert(!r2_shadow_settle_observe(&settle,&r,true));}
 r.sequence=11;r.edge.tick=176000000;r.map.lora=true;
 assert(!r2_shadow_settle_observe(&settle,&r,true)&&settle.first==r.edge.tick&&settle.resets==1);
 r.sequence=12;r.edge.tick+=16000000;r.phase_ns=1640000;
 assert(!r2_shadow_settle_observe(&settle,&r,true)&&settle.resets==1); /* Phase magnitude never gates startup. */
  /* Populations never mix parent epochs, trains, BLE/local-HOLD or LoRa
   * calibration. Normal map revisions/accepted counts are the same source. */
  struct rc_shadow_population population;rc_shadow_population_set(&population,&r.map);
  struct rc_shadow_map other=r.map;other.accepted++;other.lo++;other.active++;
  assert(rc_shadow_population_same(&population,&other));
  for(unsigned field=0;field<9;field++){
   other=r.map;
   switch(field){case 0:other.root++;break;case 1:other.root_boot++;break;
    case 2:other.parent++;break;case 3:other.parent_boot++;break;
    case 4:other.root_epoch++;break;case 5:other.parent_epoch++;break;
    case 6:other.train++;break;case 7:other.lora_delay_ns++;break;default:other.lora=false;other.local_hold=true;}
   assert(!rc_shadow_population_same(&population,&other));
   struct r2_shadow_settle changed=settle;r.map=other;r.sequence++;r.edge.tick+=16000000;
   assert(!r2_shadow_settle_observe(&changed,&r,true)&&changed.resets==settle.resets+1);
   rc_shadow_population_set(&population,&r.map);settle=changed;
  }
  assert(sizeof(struct r2_shadow_settle)==104&&sizeof(struct rc_shadow_population)==56);
 /* One-pass ranks must match the retained four-rank implementation at every
  * small-N/interpolation and histogram-tail boundary, without losing signs. */
 memset(&s,0,sizeof s);s.entered=true;assert(r2_shadow_session_begin(&s,1,1,1,0));
 struct r2_shadow_session_view cached={0},fresh;uint32_t random=0x5912a0b7;
 const int64_t edges[]={INT64_MIN,-500000001,-500000000,-1024,-1023,-1,0,1,1023,1024,2047,2048,500000000,500000001,INT64_MAX};
 for(unsigned i=1;i<=6000;i++){
  random^=random<<13;random^=random>>17;random^=random<<5;
  int64_t phase=i-1<sizeof edges/sizeof edges[0]?edges[i-1]:
   (i%17?((int64_t)(random%2049)-1024):(int64_t)(random%1000000001)-500000000);
  r2_shadow_session_sample(&s,i,i+1,1,i%23!=0,phase);s.elapsed_ms=i*1000;
  r2_shadow_session_view(&s,&fresh);
  bool approximate=false;
  assert(fresh.p95_ns==(s.samples?r2_shadow_percentile(&s,95,&approximate):0));
  assert(fresh.p99_ns==(s.samples?r2_shadow_percentile(&s,99,&approximate):0));
  assert(fresh.approximate==approximate);
  r2_shadow_session_refresh(&s,&cached);assert(!memcmp(&cached,&fresh,sizeof fresh));
  unsigned calculated=rms_calls;s.elapsed_ms++;s.missing++;
  r2_shadow_session_refresh(&s,&cached);assert(rms_calls==calculated&&cached.elapsed_ms==s.elapsed_ms&&cached.missing==s.missing);
  r2_shadow_session_view(&s,&fresh);assert(!memcmp(&cached,&fresh,sizeof fresh));
 }
 /* Reentry with the same numerical count cannot retain an old session's
  * cached moments; startup metadata remains independent of that cache. */
 s.session++;s.samples=0;s.max_ns=0;s.sum=0;s.squares_hi=0;s.squares_lo=0;
 cached.warmup=7;r2_shadow_session_refresh(&s,&cached);
 assert(!cached.valid&&!cached.mean_tenth_ns&&!cached.rms_ns&&!cached.p95_ns&&!cached.p99_ns&&!cached.approximate&&cached.warmup==7);
 puts("PASS full-second dwell, actual start boundary, invalid/reference/epoch exclusions, all-session mean and tails, navigation/GM stop, fresh reentry, bounded2000sample quantiles");
}
