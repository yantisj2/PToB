/* Actual protocol/codec, synthetic source and transport boundaries. */
#define main existing_pair_campaign_main
#include "pair_durability_test.c"
#undef main
int main(void){
 rng=23;drop=0;delay=6000;mask=3;init(1,0,0);
 for(uint64_t t=0;t<260000000;t+=10000){
  if(t==120000000)init(0,t,0);
  step(t);
  if(t==110000000)assert(n[1].local_gm&&n[1].coordinator==255);
  if(t>150000000)assert(n[1].local_gm&&n[1].choice==1);
  if(t>210000000){assert(n[0].choice==1&&!n[0].local_gm&&n[0].hot_ready);assert(n[1].coordinator==0);}
 }
 /* Explicit source withdrawal while control HELLOs continue: prior HOT
  * must take the fenced recovery path, without waiting10s reservation. */
 source[1]=false;uint64_t resumed=0;
 for(uint64_t t=260000000;t<270000000;t+=10000){step(t);assert(!n[1].local_gm);
  if(n[0].local_gm&&!resumed)resumed=t;}
 assert(resumed>=263000000&&resumed<264000000);
 /* Independent solo partitions converge from actual SERVICE evidence. */
 memset(n,0,sizeof n);memset(online,0,sizeof online);memset(source,0,sizeof source);
 head=tail=0;mask=5;partition=true;init(0,0,0);init(2,0,0);
 for(uint64_t t=0;t<160000000;t+=10000){
  if(t==120000000){assert(n[0].local_gm&&n[2].local_gm);partition=false;}
  step(t);if(t>140000000)assert(n[0].local_gm&&!n[2].local_gm&&n[2].choice==0&&n[2].hot_ready);
 }
 puts("PASS explicit withdrawal honors3s fence; independent solo partitions converge");
 puts("PASS solo incumbent retained; better-ranked staggered newcomer follows and becomes HOT");
 return 0;
}
