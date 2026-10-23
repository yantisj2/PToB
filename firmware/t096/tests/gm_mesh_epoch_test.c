/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
/* Actual authenticated JOIN/STATUS/control and captured-tuple precision.
 * Reuse only the existing fake RF/clock/controller boundaries. */
#define main original_simulator_main
#include "gm_standby_test.c"
#undef main
static void advance(uint64_t until)
{for(;now<until;now+=2500)step();}
static void pair_until(uint64_t until,int expected)
{
 for(;now<until;now+=2500){
  step();int a=active(),h=hot();
  if(a>=0&&h>=0&&n[a].hot_ready&&(expected<0||a==expected)){now+=2500;return;}
 }
 assert(!"Original bounded modeled recovery exhausted");
}
int main(void)
{
 scenario=18;nodes=4;
 for(unsigned p=0;p<N;p++){paired[p]=-1;join_at[p]=UINT64_MAX;}
 join_at[0]=join_at[1]=0;join_at[2]=130000000;join_at[3]=150000000;
 advance(195000000);pair_until(now+600000000,-1);
 uint32_t mesh=n[active()].mesh_epoch;assert(mesh);
 for(unsigned p=0;p<N;p++)assert(n[p].mesh_epoch==mesh);
 for(unsigned round=0;round<8;round++){
  int lost=active(),successor=hot();assert(lost>=0&&successor>=0);
  uint64_t failed=now;online[lost]=false;
  pair_until(now+700000000,successor);
  assert(n[successor].mesh_epoch==mesh);
  assert(now>=failed+3000000); /* Existing output fence still applies. */
  /* Replace the removed GM with a different full factory identity, boot and
   * independent source generation. No survivor or queued wire is reset. */
  identity_offset[lost]+=1000;epoch[lost]=11+round;init((unsigned)lost);
  n[lost].quality.priority1=128;
  uint64_t joined=now,limit=now+55000000;
  for(;now<limit&&!n[lost].mesh_epoch;now+=2500)step();
  assert(n[lost].mesh_epoch==mesh&&!n[lost].admission.eligible);
  assert(!n[lost].local_gm&&!n[lost].standby_authorized);
  advance(joined+65000000);
  assert(n[lost].admission.eligible&&n[lost].mesh_epoch==mesh);
  for(unsigned p=0;p<N;p++)if(online[p])assert(n[p].mesh_epoch==mesh);
  printf("PASS succession%u ACTIVE%llu HOT%llu replacement%llu mesh%u local%u\n",round,
   (unsigned long long)identity((unsigned)active()),(unsigned long long)identity((unsigned)hot()),
   (unsigned long long)identity((unsigned)lost),mesh,n[lost].candidate.generation);
 }
 for(unsigned p=0;p<N;p++)assert(identity_offset[p]); /* All originals gone. */
 struct r2_network c=n[active()];uint32_t physical=c.candidate.generation;
 struct r2_clock source_clock={0};source_clock.view.source_available=true;
 source_clock.view.source_epoch=physical+1;source_clock.last_pair_edge=source_clock.last_rmc=c.now+1;
 source_clock.last_pair_utc=INT64_C(1791417600000000)+c.now;
 r2_network_tick(&c,&source_clock,c.now+1);
 assert(c.mesh_epoch==mesh&&!c.admission.eligible&&!c.local_gm&&!c.standby_authorized);
 assert(c.admission.source_epoch==physical+1); /* Same original qualification reset. */
 puts("PASS all original GMs replaced; mesh retained; physical generation still resets qualification/grants");
 return 0;
}
