/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
/* Extend existing production network/JOIN/precision simulation: one endpoint
 * resets while remote caches, queued wire and half-connection remain retained. */
#define main original_simulator_main
#include "gm_standby_test.c"
#undef main
int main(int argc,char **argv) {
 unsigned victim_role=argc>1?(unsigned)atoi(argv[1]):0;
 uint64_t delay=argc>2?(uint64_t)atoi(argv[2])*1000:500000;
 assert(victim_role<2&&(delay==500000||delay==5000000));
 scenario=18;nodes=4;
 for(unsigned p=0;p<N;p++){paired[p]=-1;join_at[p]=UINT64_MAX;}
 join_at[0]=join_at[1]=0;join_at[2]=130000000;join_at[3]=150000000;
 int root=-1,standby=-1,victim=-1,survivor=-1,replacement=-1;uint64_t obligation=0,restored=0;
 for(now=0;now<650000000;now+=2500) {
  if(now==190000000){root=active();standby=hot();assert(root>=0&&standby>=0);victim=victim_role?root:standby;survivor=victim_role?standby:root;}
  if(now==200000000){
   struct r2_network remote=n[survivor];obligation=n[root].gm5.issued_until;
   init((unsigned)victim);source[victim]=false; /* reset only this MCU, then no GNSS */
   assert(!memcmp(&remote,&n[survivor],sizeof remote));
   assert(paired[survivor]==victim&&paired[victim]==-1); /* half-connection */
   assert(!n[victim].standby_authorized&&!n[victim].hot_ready);
  }
  if(now==200000000+delay){
   r2_network_link(&n[survivor],1,false,local((unsigned)survivor,now));paired[survivor]=-1;
   r2_network_link(&n[survivor],1,false,local((unsigned)survivor,now)); /* duplicate callback */
   assert(!n[survivor].peer[1].authenticated&&!n[survivor].peer[1].map_valid);
  }
  if(now==450000000)source[victim]=true; /* recovered physical source, normal dwell */
  step();
  if(now>200000000&&!victim_role)assert(active()==root);
  if(now>200000000&&now<200000000+delay&&!victim_role)assert(n[root].gm5.issued_until==obligation);
  if(now>200000000&&now<450000000)assert(!n[victim].local_gm&&!n[victim].standby_authorized);
  if(now>200000000&&hot()>=0&&hot()!=root&&hot()!=standby&&!restored){replacement=hot();restored=now;}
  if(now>=440000000){assert(active()>=0&&hot()>=0&&n[active()].hot_ready);assert(active()!=victim);}
  if(now>=510000000){assert(n[victim].admission.eligible);assert(n[victim].gm5.role==R2_GM5_COLD);}
 }
 assert(restored&&replacement!=victim);
 printf("PASS role%u reset;remote cached boot/epoch/grant+queued wire retained;disconnect delay%llu/duplicate;new COLD fills HOT at%lluus;recovered prior identity remains COLD under healthy pair;finalACTIVE%d/HOT%d\n",victim_role,(unsigned long long)delay,(unsigned long long)restored,active(),hot());
 return 0;
}
