/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
#include "gm7_join_fixture.h"
static struct r2_gm7_admission_source snapshot(void){
 struct gm7_bridge b;assert(bridge(&b));return admission(b);
}
static void first_announce(void){
 struct gm7_bridge b;assert(bridge(&b));
 assert(!gm7_owner_reserve(&owner,fake_hf+10000,fake_hf+14000,fake_hf));
 struct gm7_packet p;assert(!gm7_owner_planned_control(&owner,&p,fake_hf));
 assert(!queue(GM7_PRIMARY,&p,window(fake_hf+10000,100),0,b));
 finish(GM7_PRIMARY,true,true);assert(gm7_owner_output(&owner,fake_hf));
}
int main(int argc,char **argv){
 assert(argc==2);setup(false);struct gm7_join_fixture f={.version=1,.group=group};
 f.source[0]=snapshot();assert(!f.source[0].serving);
 first_announce();f.source[1]=snapshot();assert(r2_gm7_admission_live(&f.source[1],fake_kernel));
 gm7_owner_withdraw(&owner,GM7_R_MERGE,fake_hf);
 f.source[2]=snapshot();assert(!f.source[2].serving);
 setup(true);tick_to(owner.source.until_us);f.source[3]=snapshot();assert(!f.source[3].serving);
 setup(false);first_announce();f.source[4]=snapshot();assert(r2_gm7_admission_live(&f.source[4],fake_kernel));
 for(unsigned i=0;i<5;i++){f.source[i].group=NULL;f.source[i].name=NULL;}
 FILE *out=fopen(argv[1],"wb");assert(out&&fwrite(&f,sizeof f,1,out)==1&&!fclose(out));
 /* The production cycle actually publishes ordinary-service availability. */
 healthy_input();cycle(&service_work);assert(fake_join_available&&fake_join.serving);
 r2_service_stop();assert(!fake_join_available);
 puts("PASS real service admission: pre-ANN false, measured completed ANN live, merge withdrawal false, original source expiry false, completed reactivation live and explicit stop clears ordinary availability");
}
