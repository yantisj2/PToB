/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "direct_connect.h"
#include "app.h"
#include "time_service.h"
#include "capture.h"
#include "usb_source.h"
#include "ui.h"
#include <string.h>
static uint64_t silicon,boot;
static bool publishing;
static uint32_t generation,source_epoch,capture_epoch;
static struct r2_dc_rms rms;
static struct k_spinlock metric_lock;
void r2_dc_init(const struct r2_identity *id){
 if(!id||id->result)return;
 for(unsigned i=0;i<16;i++)silicon=(silicon<<4)|(id->silicon[i]<='9'?(unsigned)(id->silicon[i]-'0'):(unsigned)(id->silicon[i]-'a'+10));
 boot=id->boot;
}
void r2_dc_usb_observation(bool valid,uint64_t tick,uint64_t ns,uint16_t frame,
 uint32_t usb,uint32_t capture,uint32_t source,uint32_t generation){
 k_spinlock_key_t key=k_spin_lock(&metric_lock);
 if(valid)r2_dc_rms_add(&rms,tick,ns,frame,usb,capture,source,generation,(uint64_t)k_uptime_get());
 else rms=(struct r2_dc_rms){0};
 k_spin_unlock(&metric_lock,key);
}
void r2_dc_poll(bool allowed){
 struct r2_capture at;struct r2_uc_clock out;uint64_t now;
 struct r2_source_clock source;struct r2_clock clock;
 r2_time_copy(&clock);r2_time_source_snapshot(&source);
 bool valid=allowed&&r2_capture_now_at(&at,&now)&&r2_dc_clock(&source,&clock,at,now,silicon,boot,&out);
 if(valid){
  if(!publishing||generation!=out.map.generation||source_epoch!=out.origin.epoch||capture_epoch!=out.capture_epoch){
   r2_usb_source_publish(&out);generation=out.map.generation;source_epoch=out.origin.epoch;capture_epoch=out.capture_epoch;
  }
 }else if(publishing){r2_usb_source_withdraw();r2_dc_usb_observation(false,0,0,0,0,0,0,0);}
 publishing=valid;
}
void r2_dc_ui(struct r2_ui_model *m){
 m->bt_ready=false;m->profile_supported=false;m->scope_active=publishing;
 m->role=R2_UI_GM; /* Shared local-GNSS face; DC branding is compile-time. */
 m->gm_source_fresh=publishing;m->source_eligible=publishing;
 r2_time_precision_snapshot(&m->pps_precision);
 int64_t utc;
 if(publishing&&r2_time_source_utc_now(m->clock.source_epoch,&utc))m->clock.utc_us=utc;
 m->clock.valid=publishing;m->coarse_valid=false;
 uint32_t value=0;unsigned samples=0;
 m->precision.rms_valid=r2_dc_metric(&value,&samples);
 m->precision.rms_ns=value;m->precision.samples=samples;m->precision.map_valid=publishing;
}
bool r2_dc_metric(uint32_t *value,unsigned *samples){
 k_spinlock_key_t key=k_spin_lock(&metric_lock);
 bool valid=publishing&&r2_dc_rms_read(&rms,(uint64_t)k_uptime_get(),value,samples);
 k_spin_unlock(&metric_lock,key);return valid;
}
bool r2_dc_active(void){return publishing;}
/* Health formatter's absent BLE diagnostic writer has no stack to inspect. */
uint32_t r2_diag_stack_unused(void){return UINT32_MAX;}
