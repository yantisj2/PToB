/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main historical_service_cases
#include "gm7_service_test.c"
#undef main
#include "core.h"
static void stop_after_publication(void){fake_after_publish=NULL;r2_service_stop();}
int main(int argc,char **argv){
 assert(argc==2);unsigned test=(unsigned)strtoul(argv[1],NULL,10);
 setup(true);healthy_input();struct gm7_bridge b;assert(bridge(&b));
 if(test==6){
  struct r2_gm7_follow_diagnostic *d=&fake_follow_diagnostic;
  memset(d,0xff,sizeof *d);
  d->scope.gm=false;d->scope.follower=d->scope.broadcast=true;
  d->grant.gm=false;d->grant.follower=d->grant.broadcast=true;
  d->current=d->paired=d->managed=d->stopped=d->binding_current=d->scope_equal=true;
  d->utc_at_sample=d->utc_readable_now=d->map_at_sample=true;
  d->refusals=(1u<<19)-1;d->owner_error=INT32_MIN;d->stats.rate=INT32_MIN;
  owner.scope=(struct gm7_scope){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
  configured=started=true;owner.failed=owner.stopped=owner.serving=true;input.stopped=input.fatal=resuming=true;owner.role=UINT8_MAX;owner.reason=UINT32_MAX;
  char source[65];memset(source,'f',64);source[64]=0;
  union{char json[3072-96];char frame[3072];}buffer;
  int n=r2_gm7_diagnostic_format(buffer.json,sizeof buffer.json,9,source,UINT64_MAX,true);
  assert(n>0&&(size_t)n<sizeof buffer.json);
  assert(strstr(buffer.json,"SIGNED_DATA_PERMISSION_EXPIRED")&&strstr(buffer.json,"QUERY_CAPTURE_UNAVAILABLE")&&strstr(buffer.json,"\"signed_origin_expiry_ns\":null"));
  printf("GM9_MAX_JSON %d %s\n",n,buffer.json);
  int framed=r2_frame(buffer.frame,sizeof buffer.frame,UINT32_MAX,UINT32_MAX,buffer.json);
  assert(framed>0&&(size_t)framed<sizeof buffer.frame);
  printf("GM9_MAX_FRAME %d %s\n",framed,buffer.frame);
  /* False booleans and false agreement labels are one character wider. */
  d->current=d->paired=d->managed=d->stopped=d->binding_current=d->scope_equal=false;
  d->utc_at_sample=d->utc_readable_now=d->map_at_sample=false;
  --owner.scope.active;--d->header_root;
  n=r2_gm7_diagnostic_format(buffer.json,sizeof buffer.json,9,source,UINT64_MAX,true);
  assert(n>0&&(size_t)n<sizeof buffer.json);
  printf("GM9_FALSE_MAX_JSON %d %s\n",n,buffer.json);
  framed=r2_frame(buffer.frame,sizeof buffer.frame,UINT32_MAX,UINT32_MAX,buffer.json);
  assert(framed>0&&(size_t)framed<sizeof buffer.frame);
  printf("GM9_FALSE_MAX_FRAME %d %s\n",framed,buffer.frame);
  assert(r2_gm7_diagnostic_format(buffer.json,8,9,source,UINT64_MAX,true)==-ENOSPC);
  puts("PASS actual fullwidth GM09/cause labels/source/CRC shared-buffer frame and original bounds");return 0;
 }
 struct gm7_source s;struct r2_source_clock map;struct r2_clock clock;
 assert(!source_read(&s,&map,&clock,&b)&&s.physical_ready);
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
