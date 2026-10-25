/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main historical_service_cases
#include "gm7_service_test.c"
#undef main
static void stop_after_publication(void){fake_after_publish=NULL;r2_service_stop();}
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);
 setup(true);healthy_input();struct gm7_bridge b;assert(bridge(&b));
 struct gm7_source s;struct r2_source_clock map;struct r2_clock clock;
 assert(source_read(&s,&map,&clock,b)&&s.physical_ready);
 if(test==1)input.fatal=true;
 if(test==2)input.stopped=true;
 if(test==3)resource_fault=true;
 if(test==4)r2_service_stop();
 if(test==5)fake_after_publish=stop_after_publication;
 timing(b,&s,&map,&clock.view);
 if(test>=1&&test<=4)assert(!fake_scope.gm&&!fake_scope.follower&&!fake_resume_until);
 else if(test==5)assert(atomic_get(&stopping)&&!fake_scope.gm);
 else assert(fake_scope.gm&&fake_scope.local_source_epoch==3&&fake_resume_until>b.kernel_hi);
 char out[1536];int n=r2_gm7_diagnostic_format(out,sizeof out,0,"source",boot,true);
 assert(n>0&&(size_t)n<sizeof out&&strstr(out,"\"pt_stopped\":false")&&strstr(out,"\"pt_managed\":true")&&strstr(out,"\"pt_binding_matches\":")&&strstr(out,"\"schema\":7"));
 assert(r2_gm7_diagnostic_format(out,(size_t)n,0,"source",boot,true)==-ENOSPC);
 printf("PASS actual GM timing producer lifecycle gate%u and bounded named PT diagnostic formatting\n",test);return 0;
}
