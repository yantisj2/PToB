/* Production map selection compared to its pre-0.14.6 exhaustive body.
 * Synthetic maps test semantics and bounded work, not physical accuracy. */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../src/relay_precision.c"
static bool reference_usable(const struct r2_relay_precision *s,const struct r2_relay_map_record *m,uint64_t tick,int64_t *ns){
 if(!m->used||!m->map.valid||tick<m->map.delivery||tick<m->accepted||
    !r2_servo_mapped(&m->map,tick,ns)||*ns<0)return false;
 uint64_t age=s->self==s->relay&&!s->passive_receiver?(s->experimental_fresh_us==500000?R2_CAPTURE_HZ/2:R2_RADIO_MAX_AGE_TICKS):R2_CAPTURE_HZ*3/2;
 if(tick-m->map.delivery<age&&tick-m->accepted<age&&(uint64_t)*ns<m->origin.expires_ns)return true;
 if(!m->origin.hold_horizon_us||(uint64_t)*ns<m->origin.created_ns||(uint64_t)*ns>=held_until(&m->origin))return false;
 uint64_t elapsed=r2_ceil_div((uint64_t)*ns-m->origin.created_ns,1000);
 uint64_t phase=m->map.phase<0?(uint64_t)(-(m->map.phase+1))+1:(uint64_t)m->map.phase;
 uint32_t base=phase>=UINT32_MAX?UINT32_MAX:r2_relay_bound_add(m->bound,(uint32_t)phase);
 uint32_t error=r2_hold_error(&s->hold_bound,elapsed,base);
 return error!=R2_V14_UNKNOWN&&error<=m->origin.hold_budget_ns;
}
static const struct r2_relay_map_record *reference(const struct r2_relay_precision *s,uint64_t tick){
 const struct r2_relay_map_record *best=NULL;int64_t ns;
 for(unsigned i=0;i<R2_RELAY_MAPS;i++)if(reference_usable(s,&s->map[i],tick,&ns)&&
    (!best||s->map[i].map.delivery>best->map.delivery))best=&s->map[i];
 return best;
}
static uint32_t random_state=91;
static uint32_t next(void){random_state=random_state*1664525u+1013904223u;return random_state;}
int main(void){
 struct r2_relay_precision s={.self=1,.relay=1,.epoch=1,.origin_valid=true};
 unsigned checks=0;
 for(unsigned treatment=0;treatment<20000;treatment++){
  s.self=treatment%3;s.passive_receiver=!!(treatment&8);s.experimental_fresh_us=treatment&16?500000:0;
  for(unsigned i=0;i<R2_RELAY_MAPS;i++){
   uint64_t delivery=16000000+(next()%20)*800000; /* ties are intentional */
   s.map[i]=(struct r2_relay_map_record){.used=!!(next()&4),.map={.delivery=delivery,.anchor=(int64_t)(delivery*125/2),.phase=(int32_t)(next()%2000)-1000,.rate=(int32_t)(next()%200000)-100000,.valid=!!(next()&4)},.accepted=delivery+(next()%400000),.origin={.created_ns=delivery*125/2,.expires_ns=delivery*125/2+1000000+(next()%1600000000),.utc_offset_ns=1000000000,.hold_horizon_us=treatment&32?1650000:0,.hold_budget_ns=500},.bound=treatment&64?10:UINT32_MAX};
  }
  uint64_t tick=16000000+next()%50000000;
  assert(usable_map(&s,tick)==reference(&s,tick));checks++;
 }
 /* All8 currently usable: one affine candidate suffices, same winner. */
 for(unsigned i=0;i<R2_RELAY_MAPS;i++)s.map[i]=(struct r2_relay_map_record){.used=true,.map={.delivery=16000000+i*16000,.anchor=1000000000+(int64_t)i*1000000,.valid=true},.accepted=16000000+i*16000,.origin={.expires_ns=3000000000,.utc_offset_ns=1000000000}};
 s.self=0;s.passive_receiver=false;
 clock_t begin=clock();volatile unsigned sum=0;
 for(unsigned i=0;i<10000;i++)sum+=(unsigned)(reference(&s,16200000)-s.map);
 double old=(double)(clock()-begin)/CLOCKS_PER_SEC;
 begin=clock();for(unsigned i=0;i<10000;i++)sum+=(unsigned)(usable_map(&s,16200000)-s.map);
 double fast=(double)(clock()-begin)/CLOCKS_PER_SEC;
 printf("PASS %u exact-selector comparisons; old=%f s optimized=%f s host-only ratio=%f sum=%u\n",checks,old,fast,old/fast,sum);
 return 0;
}
