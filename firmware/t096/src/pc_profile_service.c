/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * PC has one leaf receiver. No GM backend, election or runtime role switch.
 */
#include "rc_dynamic_service.h"
#include "pc_experiment.h"
#include "capture.h"
#include "runtime_profile.h"
#include "watchdog.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
static struct r2_identity physical;
static struct r2_enrollment enrollment;
static bool paused,terminal;
static uint32_t starts;
static int failure;
bool r2_switch_rc_active(void){return true;}
int r2_service_configure(const struct r2_identity *id,const struct r2_enrollment *e){
 if(!id||!e||!e->rc7_leaf||!e->rc7_boot_rc||!e->rc7_boot_parent||e->rc7_exclude_direct_gm)return -EINVAL;
 /* The ordinary dispatcher configures the enrolled identity before bt_enable.
  * PC excludes that owner, so it must perform this same identity step itself.
  * The default generated GAP address is not the authenticated group identity. */
 uint64_t silicon=strtoull(id->silicon,NULL,16);uint8_t derived[16];
 if(!silicon||!r2_group_valid(&e->group))return -EACCES;
 r2_group_pair_key(derived,&e->group,silicon,silicon);
 bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM};memcpy(address.a.val,derived,6);address.a.val[5]|=0xc0;
 int identity_result=bt_id_create(&address,NULL);if(identity_result<0)return identity_result;
 if(identity_result!=0)return -EACCES;
 physical=*id;enrollment=*e;
 int rc=r2_dynamic_service_configure(id,e);
 return rc?rc:r2_dynamic_service_parent_target(e->rc7_boot_parent);
}
void r2_service_clock(const struct r2_clock *c){(void)c;r2_service_supervise();}
int r2_service_run(uint64_t at){return r2_dynamic_service_run(at);}
int r2_service_preflight(uint32_t s,uint64_t at){(void)s;(void)at;return -ENOTSUP;}
int r2_service_start(uint32_t s,uint64_t at){(void)s;(void)at;return -ENOTSUP;}
void r2_service_stop(void){terminal=true;r2_dynamic_service_stop();}
int r2_service_close_result(void){return r2_dynamic_service_close_result();}
void r2_service_snapshot(struct r2_service_status *s){
 r2_dynamic_service_snapshot(s);s->profile_rc=s->profile_target_rc=true;s->profile_pending=paused&&!terminal;
 s->profile_incarnation=physical.boot;s->profile_changes=starts;s->normal=true;
 s->serving=s->hot_ready=s->gm_capable=s->standby_authorized=false;
 if(failure)s->error=failure;
}
void r2_service_ui(struct r2_ui_model *m){r2_dynamic_service_ui(m);m->profile_supported=false;m->profile_switching=false;}
void r2_service_supervise(void){
 r2_dynamic_service_supervise();r2_pc_poll();
 if(terminal||failure)return;
 if(r2_pc_quiet()&&!paused){paused=true;r2_dynamic_service_stop();}
 if(!paused)return;
 int rc=r2_dynamic_service_close_result();
 if(rc==-EAGAIN)return;
 if(rc){failure=rc;return;}
 struct r2_capture now;if(!r2_capture_now(&now))return;
 r2_pc_closed(now.tick);r2_shadow_poll();
 if(r2_pc_quiet())return;
 struct r2_identity id=physical;
 if(starts==65535){failure=-EOVERFLOW;return;}
 id.boot=r2_profile_incarnation(physical.boot,++starts);
 rc=r2_dynamic_service_configure(&id,&enrollment);
 if(!rc)rc=r2_dynamic_service_parent_target(enrollment.rc7_boot_parent);
 if(!rc)rc=r2_dynamic_service_run(k_uptime_get());
 if(rc)failure=rc;else paused=false;
}
int r2_service_profile(unsigned m,uint64_t at){(void)m;(void)at;return -ENOTSUP;}
int r2_service_restart(uint64_t at){(void)at;return -ENOTSUP;}
void r2_service_profile_status(uint32_t out[5]){out[0]=out[2]=1;out[1]=paused&&!terminal;out[3]=0;out[4]=starts;}
int r2_service_test_parent(uint64_t p,uint64_t at){(void)p;(void)at;return -ENOTSUP;}
void r2_service_test_parent_status(uint64_t *p,bool *r){*p=enrollment.rc7_boot_parent;*r=false;}
void r2_service_test_transport_status(uint32_t out[13]){r2_dynamic_service_transport_snapshot(out);}
void r2_service_test_acquisition_status(uint32_t out[20]){r2_dynamic_service_acquisition_snapshot(out);}
void r2_service_test_gm_admission_status(uint32_t out[7]){r2_dynamic_service_gm_admission_snapshot(out);}
void r2_service_test_connection_status(uint32_t out[20]){r2_dynamic_service_connection_snapshot(out);}
int r2_service_name(const char *s){(void)s;return -ENOTSUP;}
int r2_service_relay(uint64_t s,uint32_t n,uint64_t at){(void)s;(void)n;(void)at;return -ENOTSUP;}
int r2_service_relay_cancel(uint64_t s){(void)s;return -ENOTSUP;}
uint32_t r2_service_stack_unused(void){return r2_dynamic_service_stack_unused();}
uint32_t r2_radio_service_stack_unused(void){return UINT32_MAX;}
