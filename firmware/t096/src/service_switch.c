/* SPDX-License-Identifier: Apache-2.0
 * One active backend. Every mode change fences time and drains the old owner.
 * No flash, reset magic or healthy-service expiry; physical console boot stays.
 */
#include "service_backends.h"
#include "runtime_profile.h"
#include "rc_shadow.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/atomic.h>
static struct r2_identity physical;
static struct r2_enrollment enrollment;
static atomic_t rc_active;
static bool pending,target,resuming,diagnostic_used;
static uint64_t until,incarnation;
static uint32_t changes;
static int switch_error;
bool r2_switch_rc_active(void){return atomic_get(&rc_active)!=0;}
static uint64_t us(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
int r2_service_configure(const struct r2_identity *id,const struct r2_enrollment *e){
 if(!id||strcmp(id->silicon,"1000000000000002")||!e||!e->boundary_field||e->boundary_profile!=5)return -EACCES;
 physical=*id;enrollment=*e;incarnation=id->boot;
 struct r2_enrollment gm=*e;gm.boundary_field=gm.field_profile=false;gm.local_capabilities=3;
 return r2_normal_service_configure(id,&gm);
}
void r2_service_clock(const struct r2_clock *clock){
 if(r2_switch_rc_active())r2_field_service_clock(clock);else r2_normal_service_clock(clock);
 r2_service_supervise();
}
int r2_service_run(uint64_t at){return r2_normal_service_run(at);}
int r2_service_preflight(uint32_t seconds,uint64_t at){return r2_switch_rc_active()?r2_field_service_preflight(seconds,at):r2_normal_service_preflight(seconds,at);}
int r2_service_start(uint32_t seconds,uint64_t at){return r2_switch_rc_active()?r2_field_service_start(seconds,at):r2_normal_service_start(seconds,at);}
void r2_service_stop(void){pending=resuming=false;if(r2_switch_rc_active())r2_field_service_stop();else r2_normal_service_stop();}
int r2_service_close_result(void){return switch_error?switch_error:r2_switch_rc_active()?r2_field_service_close_result():r2_normal_service_close_result();}
void r2_service_snapshot(struct r2_service_status *out){
 if(r2_switch_rc_active())r2_field_service_snapshot(out);else r2_normal_service_snapshot(out);
 if(r2_switch_rc_active()||pending){out->profile_rc=r2_switch_rc_active();out->profile_target_rc=target;out->profile_changes=changes;out->profile_incarnation=incarnation;}
 out->profile_pending=pending||out->profile_pending;
 if(pending||switch_error){out->serving=out->follower=out->follower_clock_valid=out->hot_ready=out->standby_authorized=false;out->precision.map_valid=out->precision.authorized=out->precision.locked=out->precision.rms_valid=false;out->service_until=out->authority_until=out->role_until=0;}
 if(switch_error)out->error=switch_error;
}
void r2_service_ui(struct r2_ui_model *m){
 if(r2_switch_rc_active())r2_field_service_ui(m);else r2_normal_service_ui(m);
 m->profile_supported=true;if(r2_switch_rc_active())m->profile_rc=true;m->profile_switching=pending||m->profile_switching;
 if(pending||switch_error){m->scope_active=m->authenticated_gm=m->gm_source_fresh=m->ble_time_selected=false;m->clock.valid=false;m->role=R2_UI_NO_ROLE;m->precision.map_valid=m->precision.authorized=m->precision.locked=false;}
}
int r2_service_profile(unsigned mode,uint64_t arrival){
 uint64_t now=us();if(mode>R2_PROFILE_TOGGLE||arrival>now/1000||now/1000-arrival>=R2_REPLY_MS)return -EINVAL;
 if(pending||switch_error||changes>=65535)return -EBUSY;
 /* After the single forced-relay excursion, use the existing normal
  * switcher unchanged. Its RC mode chooses its upstream automatically. */
 if(diagnostic_used&&!r2_switch_rc_active())return r2_normal_service_profile(mode,arrival);
 bool next=mode==R2_PROFILE_TOGGLE?!r2_switch_rc_active():mode==R2_PROFILE_RC;
 if(next==r2_switch_rc_active())return 0;
 target=next;pending=true;until=now+2000000;r2_shadow_normal_withdraw();
 if(r2_switch_rc_active())r2_field_service_stop();else r2_normal_service_stop();return 0;
}
void r2_service_profile_status(uint32_t out[5]){
 if(diagnostic_used&&!r2_switch_rc_active()&&!pending){r2_normal_service_profile_status(out);return;}
 out[0]=r2_switch_rc_active();out[1]=pending;out[2]=target;out[3]=pending?1:0;out[4]=changes;
}
void r2_service_supervise(void){
 if(r2_switch_rc_active())r2_field_service_supervise();else r2_normal_service_supervise();
 if(!pending)return;
 if(resuming){
  struct r2_service_status s;r2_normal_service_snapshot(&s);
  if(s.error){switch_error=s.error;pending=resuming=false;return;}
  if(s.profile_pending||!s.active||s.closed){
   if(us()<until)return;
   switch_error=-ETIMEDOUT;pending=resuming=false;r2_normal_service_stop();return;
  }
  incarnation=s.profile_incarnation;++changes;pending=resuming=false;return;
 }
 int ret=r2_switch_rc_active()?r2_field_service_close_result():r2_normal_service_close_result();
 if(ret==-EAGAIN){if(us()<until)return;ret=-ETIMEDOUT;}
 if(ret){switch_error=ret;pending=false;return;}
 if(us()>=until){switch_error=-ETIMEDOUT;pending=false;return;}
 uint64_t next=r2_profile_incarnation(physical.boot,changes+1);
 if(!next){switch_error=-EOVERFLOW;pending=false;return;}
 if(target){
  struct r2_identity id=physical;id.boot=next;
  ret=r2_field_service_configure(&id,&enrollment);
  if(!ret){atomic_set(&rc_active,1);ret=r2_field_service_run(us()/1000);if(!ret){diagnostic_used=true;r2_shadow_enable(true);}}
 }else{
  r2_shadow_enable(false);atomic_clear(&rc_active);ret=r2_normal_service_resume_gm(next,changes);
  if(!ret){resuming=true;until=us()+2000000;return;}
 }
 if(ret){switch_error=ret;pending=false;return;}
 incarnation=next;++changes;pending=false;
}
int r2_service_name(const char *name){return r2_switch_rc_active()?r2_field_service_name(name):r2_normal_service_name(name);}
int r2_service_relay(uint64_t session,uint32_t seconds,uint64_t arrival){(void)session;(void)seconds;(void)arrival;return -ENOTSUP;}
int r2_service_relay_cancel(uint64_t session){(void)session;return -ENOTSUP;}
uint32_t r2_service_stack_unused(void){return r2_switch_rc_active()?r2_field_service_stack_unused():r2_normal_service_stack_unused();}
