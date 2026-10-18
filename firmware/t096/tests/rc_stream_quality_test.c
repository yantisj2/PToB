#include <assert.h>
#include <stdio.h>
#include "rc_stream_quality.h"
static unsigned checks;
#define CHECK(x) do{assert(x);++checks;}while(0)
int main(void){
 struct rc_stream_history h={0};struct rc_stream_sample s={0};struct r2_link_quality q={0};
 CHECK(sizeof h==272&&sizeof s==32);
 rc_stream_failed(&h,11,false,1000000);CHECK(rc_stream_blocked(&h,11,false,300999999));CHECK(!rc_stream_blocked(&h,11,false,301000000));
 rc_stream_failed(&h,11,false,301000000);CHECK(rc_stream_blocked(&h,11,false,900999999));CHECK(!rc_stream_blocked(&h,11,false,901000000));
 rc_stream_failed(&h,11,true,901000000);
 CHECK(rc_stream_find(&h,11,true,901000000)&&rc_stream_find(&h,11,false,901000000));
 CHECK(rc_stream_find(&h,11,true,901000000)!=rc_stream_find(&h,11,false,901000000));
 memset(&h,0,sizeof h);r2_link_quality_bind(&q,11,1,1);
 rc_stream_observe(&h,&s,&q,11,false,true,true,1000000);
 for(unsigned i=1;i<=16;i++){uint64_t now=1000000+(uint64_t)i*62500;r2_link_quality_rx(&q,1,(uint16_t)i,now);rc_stream_observe(&h,&s,&q,11,false,true,true,now);}
 struct rc_stream_row *r=rc_stream_find(&h,11,false,2000000);CHECK(r&&r->delivery==1000&&r->usable==1000&&r->usable_samples==1);
 rc_stream_failed(&h,11,false,2000000);
 for(unsigned i=17;i<=16*10;i++){uint64_t now=1000000+(uint64_t)i*62500;r2_link_quality_rx(&q,1,(uint16_t)i,now);rc_stream_observe(&h,&s,&q,11,false,false,true,now);}
 r=rc_stream_find(&h,11,false,11000000);CHECK(r&&r->delivery==1000&&r->usable<100&&r->failures==1);CHECK(rc_stream_blocked(&h,11,false,11000000)); // Audible packets cannot clear poor time.
 int failed_score=rc_stream_score(&h,11,false,-20,11000000);CHECK(failed_score<rc_stream_score(&h,22,true,-70,11000000));
 unsigned n=r->usable_samples;rc_stream_observe(&h,&s,&q,11,false,false,false,11100000);rc_stream_observe(&h,&s,&q,11,false,false,false,20000000);CHECK(r->usable_samples==n); // No manufactured quiet-window denominator.
 for(unsigned i=0;i<RC_STREAM_CAPACITY+3;i++)rc_stream_failed(&h,100+i,true,21000000+(uint64_t)i*1000000);
 unsigned entries=0;for(unsigned i=0;i<RC_STREAM_CAPACITY;i++)entries+=h.row[i].id!=0;CHECK(entries==8);CHECK(!rc_stream_find(&h,11,false,33000000));
 rc_stream_advance(&h,2000000000);for(unsigned i=0;i<RC_STREAM_CAPACITY;i++)CHECK(!h.row[i].id);
 rc_stream_failed(&h,11,false,2000000001);rc_stream_advance(&h,1);CHECK(!rc_stream_find(&h,11,false,1)); // Clock reversal cannot retain bad or good authority.

 memset(&s,0,sizeof s);for(unsigned i=0;i<8;i++)rc_stream_window(&s,500);CHECK(s.unstable);
 for(unsigned i=0;i<2;i++)rc_stream_window(&s,1000);CHECK(s.unstable);
 for(unsigned i=0;i<6;i++)rc_stream_window(&s,1000);CHECK(!s.unstable);
 memset(&h,0,sizeof h);
 for(unsigned failure=0;failure<3;failure++) {
  uint64_t now=(UINT64_C(1)+failure*300)*1000000;
  rc_stream_failed(&h,22,true,now);
  uint64_t until=now+(UINT64_C(30000000)<<failure);
  CHECK(rc_stream_blocked(&h,22,true,until-1));CHECK(!rc_stream_blocked(&h,22,true,until));
 }
 memset(&h,0,sizeof h);
 for(unsigned failure=0;failure<3;failure++) {
  uint64_t now=(UINT64_C(1)+failure*300)*1000000;
  rc_stream_admission_failed(&h,33,true,now);
  uint64_t until=now+(UINT64_C(30000000)<<failure);
  CHECK(rc_stream_blocked(&h,33,true,until-1));CHECK(!rc_stream_blocked(&h,33,true,until));
 }
 // The earlier retry for a previously settled GM is bounded and keeps failure history.
 memset(&h,0,sizeof h);
 for(unsigned failure=0;failure<3;failure++) {
  uint64_t now=(UINT64_C(1)+failure*300)*1000000;
  rc_stream_gm_recovery_failed(&h,55,now);
  uint64_t until=now+(UINT64_C(60000000)<<failure);
  CHECK(rc_stream_blocked(&h,55,false,until-1));CHECK(!rc_stream_blocked(&h,55,false,until));
  CHECK(rc_stream_find(&h,55,false,until)->failures==failure+1);
 }
 // One good sample cannot erase a recent failure, even for a new history row.
 memset(&h,0,sizeof h);memset(&s,0,sizeof s);memset(&q,0,sizeof q);
 r2_link_quality_bind(&q,44,1,1);rc_stream_failed(&h,44,true,1000000);
 rc_stream_observe(&h,&s,&q,44,true,true,true,1000000);
 for(unsigned i=1;i<=16*9;i++) {
  uint64_t now=1000000+(uint64_t)i*62500;
  r2_link_quality_rx(&q,1,(uint16_t)i,now);
  rc_stream_observe(&h,&s,&q,44,true,true,true,now);
  if(i==16)CHECK(rc_stream_blocked(&h,44,true,now));
 }
 CHECK(!rc_stream_blocked(&h,44,true,10000000));
 // Quality confidence ages without fabricating samples or extending it on failure.
 memset(&h,0,sizeof h);r=rc_stream_obtain(&h,77,true,1000000);
 r->usable=r->delivery=1000;r->usable_samples=r->delivery_samples=32;
 CHECK(rc_stream_proven(&h,77,true,1000000));
 int fresh=rc_stream_score(&h,77,true,-50,1000000);
 int aged=rc_stream_score(&h,77,true,-50,301000000);
 CHECK(aged<fresh&&!rc_stream_proven(&h,77,true,301000000));
 unsigned count=r->usable_samples;rc_stream_failed(&h,77,true,302000000);
 CHECK(r->observed==1&&r->usable_samples==count);
 rc_stream_scope_changed(&h,77,true,303000000);
 CHECK(!r->usable_samples&&r->delivery_samples==8&&r->failures==1);
 CHECK(!rc_stream_proven(&h,77,true,303000000));
 printf("PASS %u stream history assertions; history=%zu sampler=%zu bytes\n",checks,sizeof h,sizeof s);
}
