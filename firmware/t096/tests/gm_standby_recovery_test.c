/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Reuse the actual wire/JOIN/physical-tuple fixture; only delivery/PA is controlled.
 * This is not a native callback or RF qualification. */
#define main gm_fixture_main
#include "gm_standby_test.c"
#undef main
int main(int argc,char **argv){
 assert(argc==4);unsigned management=(unsigned)atoi(argv[3]);unsigned gap=(unsigned)atoi(argv[1]),negative=(unsigned)atoi(argv[2]);scenario=18;nodes=3;
 for(unsigned p=0;p<N;p++){paired[p]=-1;join_at[p]=UINT64_MAX;}join_at[0]=join_at[1]=0;join_at[2]=130000000;
 int root=-1,standby=-1;uint64_t recovered=0,first_clear=0,loss_at=0,first_repair=0,repair_end=0;uint32_t failures=0,publication=0,last_generation=0;uint64_t last_grant=0;
 for(now=0;now<270000000;now+=2500){
  if(now==125000000){root=active();standby=hot();assert(root>=0&&standby>=0);}
  if(now==200000000){assert(hot()==standby&&n[root].hot_ready);if(management)control_gap_until=now+(uint64_t)gap*1000000;else pa[standby]=false;failures=n[root].gm5.failures;publication=n[root].publication;}
  if(now==205000000&&negative){source[standby]=false;++epoch[standby];}
  if(now==200000000+(uint64_t)gap*1000000)if(!management)pa[standby]=true;
  step();
  if(now>=200000000){
   assert(active()==root&&n[root].publication==publication);
   if(!first_repair&&n[root].gm5.attempt_until){first_repair=now;repair_end=n[root].gm5.attempt_until;}
   if(!first_clear&&n[root].gm5.selected!=identity((unsigned)standby))first_clear=now;
   if(!n[standby].hot_ready&&!loss_at){loss_at=now;}
   if(!n[standby].timing_valid)assert(!n[standby].hot_ready);
   if(last_generation==n[standby].gm5.grant_generation&&last_grant&&n[standby].gm5.grant_until)assert(n[standby].gm5.grant_until<=last_grant);
   last_generation=n[standby].gm5.grant_generation;last_grant=n[standby].gm5.grant_until;
   assert(!n[standby].standby_authorized||local((unsigned)standby,now)<n[standby].gm5.grant_until);
   if(now>200000000+(uint64_t)gap*1000000&&hot()==standby&&n[root].hot_ready&&!recovered)recovered=now;
   if(negative&&now>205000000)assert(!n[standby].hot_ready);
  }
 }
 if(!negative&&gap<=30){assert(!first_clear&&recovered);if(management)assert(!loss_at);}
 if(management&&gap==50)assert(loss_at&&loss_at<250000000&&recovered);
 if(!management)assert(loss_at); /* Real precision expiry still withdraws immediately. */
 printf("{\"first_hot_loss_model_us\":%llu,\"gap_s\":%u,\"source_negative\":%u,\"repair_started_model_us\":%llu,\"repair_deadline_local_us\":%llu,\"first_selected_clear_model_us\":%llu,\"recovered_model_us\":%llu,\"failures_delta\":%u}\n",(unsigned long long)loss_at,gap,negative,(unsigned long long)first_repair,(unsigned long long)repair_end,(unsigned long long)first_clear,(unsigned long long)recovered,n[root].gm5.failures-failures);
 return 0;
}
