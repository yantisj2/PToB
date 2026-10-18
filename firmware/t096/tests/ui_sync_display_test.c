/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Actual presentation policy and renderer; no timing owner is substituted.
 */
#include "ui.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static uint8_t held[R2_UI_BYTES],lost[R2_UI_BYTES];
static void image(const char *dir,const char *name,const uint8_t *fb){
 char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<R2_UI_BYTES;i+=2){unsigned p=((unsigned)fb[i]<<8)|fb[i+1];
  fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}
 assert(!fclose(f));
}
static struct r2_ui_model good(uint64_t now){
 struct r2_ui_model m={.sampled_ms=now,.platform_ready=true,.bt_ready=true,
  .profile_supported=true,.profile_rc=true,.boundary_clock=true,.ble_active=true,
  .role=R2_UI_FOLLOWER,.scope_active=true,.authenticated_gm=true,
  .ble_time_selected=true,.ble_valid_until_us=(now+1)*1000,
  .clock={.valid=true,.utc_us=1792340000000000LL},
  .precision={.map_valid=true,.authorized=true,.locked=true}};
 return m;
}
static void loss(struct r2_ui_model *m,uint64_t now){
 m->sampled_ms=now;m->clock.valid=m->ble_time_selected=m->scope_active=false;
 m->precision.map_valid=m->precision.authorized=m->precision.locked=false;
 m->role=R2_UI_NO_ROLE;m->sync_attempt=true;
}
int main(int argc,char **argv){
 assert(argc==1||argc==2);
 struct r2_ui_sync_display c={0};struct r2_ui_model m=good(1000);int64_t utc;bool coarse;
 r2_ui_sync_display_update(&c,&m,1000,false);assert(!c.at_ms);
 loss(&m,1001);r2_ui_sync_display_update(&c,&m,1001,false);
 assert(!r2_ui_sync_display_time(&c,&m,1001,&utc)); /* Startup cannot invent time. */
 m=good(1000);r2_ui_sync_display_update(&c,&m,1000,true);assert(c.at_ms==1000);
 loss(&m,1020);r2_ui_sync_display_update(&c,&m,1020,false);
 for(uint64_t now=1020;now<1520;now+=20){
  m.sampled_ms=now;r2_ui_sync_display_update(&c,&m,now,false);
  assert(c.loss_ms==1020&&r2_ui_sync_display_time(&c,&m,now,&utc));
  assert(utc==1792340000000000LL+(int64_t)(now-1000)*1000);
  /* Grace never changes the model, source expiry, analog/lock or saver gate. */
  assert(!m.precision.locked&&!m.precision.authorized&&!m.precision.map_valid);
  assert(!r2_ui_analog(&m,now)&&!r2_ui_face_locked(&m,now)&&!r2_ui_saver_allowed(&m,now));
  assert(!r2_ui_display_time(&m,now,&utc,&coarse));
 }
 m.sampled_ms=1519;assert(r2_ui_sync_display_time(&c,&m,1519,&utc));
 struct r2_ui_status s={.sync_display=c};r2_ui_render(held,&m,&s,1519);
 s.sync_display=(struct r2_ui_sync_display){0};r2_ui_render(lost,&m,&s,1519);
 assert(memcmp(held,lost,sizeof held)); /* Actual face differs from Syncing. */
 if(argc==2){image(argv[1],"short-loss-face",held);image(argv[1],"continued-loss-syncing",lost);}
 assert(!r2_ui_sync_display_time(&c,&m,1520,&utc));
 r2_ui_sync_display_update(&c,&m,1520,false);assert(!c.at_ms);
 r2_ui_sync_display_update(&c,&m,1600,false);assert(!c.at_ms); /* Bad polls cannot renew. */
 /* A brief gap recovers immediately; the next loss gets one new interval. */
 m=good(2000);r2_ui_sync_display_update(&c,&m,2000,true);
 loss(&m,2020);r2_ui_sync_display_update(&c,&m,2020,false);
 m=good(2200);r2_ui_sync_display_update(&c,&m,2200,false);assert(c.at_ms==2200&&!c.loss_ms);
 loss(&m,2220);r2_ui_sync_display_update(&c,&m,2220,false);assert(c.loss_ms==2220);
 assert(r2_ui_sync_display_time(&c,&m,2719,&utc));assert(!r2_ui_sync_display_time(&c,&m,2720,&utc));
 /* Explicit profile/transport withdrawal and stale/backward observations cancel. */
 for(unsigned i=0;i<6;i++){
  m=good(3000);r2_ui_sync_display_update(&c,&m,3000,true);loss(&m,3020);
  if(i==0)m.profile_switching=true;
  if(i==1)m.profile_rc=false;
  if(i==2)m.ble_active=false;
  if(i==3)m.platform_ready=false;
  if(i==4)m.sampled_ms=0;
  uint64_t now=i==5?2999:3020;
  r2_ui_sync_display_update(&c,&m,now,false);assert(!c.at_ms);
  assert(!r2_ui_sync_display_time(&c,&m,now,&utc));
 }
 c=(struct r2_ui_sync_display){.utc_us=INT64_MAX-999,.at_ms=4000,.loss_ms=4001};
 m=good(4001);loss(&m,4001);assert(!r2_ui_sync_display_time(&c,&m,4001,&utc));
 puts("PASS actual RC display: 500ms loss deadline, startup/recovery/expiry/context, no lock/output extension");
}
