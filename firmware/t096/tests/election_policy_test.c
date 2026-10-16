#include "election_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_clock_quality a={128,248,254,128,65535,100},b=a;
#define ORDER(f,better) do{b=a;b.f=better;assert(r2_quality_compare(&b,&a)<0&&r2_quality_compare(&a,&b)>0);}while(0)
 ORDER(priority1,127);ORDER(clock_class,247);ORDER(accuracy,253);ORDER(variance,65534);ORDER(priority2,127);ORDER(identity,99);
#undef ORDER
 assert(!r2_quality_compare(&a,&a));b=a;b.identity=1;b.variance=65534;b.accuracy=255;assert(r2_quality_compare(&b,&a)>0);
 struct r2_source_admission s;r2_admission_init(&s,0);
 for(uint64_t t=0;t<=60000000;t+=1000000)assert(r2_admission_update(&s,true,true,true,1,t+1500000,t)==(t==60000000));
 assert(!r2_admission_update(&s,true,true,true,1,70000000,62000000)); /* unobserved source interval */
 for(uint64_t t=62000000;t<=122000000;t+=1000000)assert(r2_admission_update(&s,true,true,true,2,t+1500000,t)==(t==122000000));
 assert(!r2_admission_update(&s,true,false,true,2,130000000,122000001));
 assert(!r2_admission_update(&s,true,true,false,2,130000000,122000002));
 assert(!r2_admission_update(&s,false,true,true,2,130000000,122000003));
 assert(!r2_admission_update(&s,true,true,true,2,130000000,122000002)&&s.failed);
 struct r2_ranked_clock c[3];for(unsigned i=0;i<3;i++)c[i]=(struct r2_ranked_clock){.quality=a,.boot=100+i,.wire=i,.gm_capable=true,.gnss_capable=true,.independent_ready=true,.authenticated=true};
 for(unsigned best=0;best<3;best++)for(unsigned second=0;second<3;second++)if(best!=second){
  for(unsigned i=0;i<3;i++)c[i].quality.identity=10+i;
  c[best].quality.identity=1;c[second].quality.identity=2;
  struct r2_elected_roles r=r2_election_rank(c,3);assert(r.gm==best&&r.coordinator==second&&r.standby==second);
  c[best].gnss_capable=false;r=r2_election_rank(c,3);assert(r.gm==second&&r.standby!=best);c[best].gnss_capable=true;
 }
 c[0].independent_ready=c[1].independent_ready=false;struct r2_elected_roles r=r2_election_rank(c,3);assert(r.gm==2&&r.standby==255&&r.coordinator==255);
 c[2].independent_ready=false;r=r2_election_rank(c,3);assert(r.gm==255&&r.coordinator==255);
 puts("PASS offline election policy: quality order,60s admission,source withdrawal,all role permutations,GNSS capability exclusion,standalone/no invented standby");
}
