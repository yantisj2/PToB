/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: Apache-2.0.
 * Selected exact BLE policy functions from public pre-LoRa commit d5838882a2567a6e4b585d1559f85bfd2e3b4a0a
 * firmware/t096/src/ui_render.c, parent of initial RC8 LoRa integration.
 * Original 500ms anti-flicker behavior from8700e8d is preserved.
 * Names changed only to run beside current production functions.
 * Historical source SHA256 053572470f458021e3670ed9be500b938adf8bd6f22f4e1b0a96c821d99d714f. */
#pragma once
bool pre_lora_r2_ui_ble_current(const struct r2_ui_model *m,uint64_t now)
{
 return m->ble_time_selected&&now<=UINT64_MAX/1000&&now*1000<m->ble_valid_until_us;
}

bool pre_lora_r2_ui_analog(const struct r2_ui_model *m,uint64_t now)
{
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||!m->clock.valid||m->clock.utc_us<0||!m->scope_active)return false;
 if(m->role==R2_UI_GM)return m->clock.source_available||m->gm_source_fresh;
 if(m->boundary_clock)return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&pre_lora_r2_ui_ble_current(m,now)&&m->precision.map_valid;
 return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&pre_lora_r2_ui_ble_current(m,now)&&m->follower_pps_valid&&m->precision.map_valid&&m->sync_age_ms<=2500;
}

bool pre_lora_r2_ui_face_locked(const struct r2_ui_model *m,uint64_t now)
{
 if(m->role==R2_UI_GM||m->boundary_clock)return pre_lora_r2_ui_analog(m,now);
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||
    now>UINT64_MAX/1000||now*1000>=m->face_lock_until_us||m->face_rms_ns>200)return false;
 int64_t utc;bool coarse;
 return r2_ui_display_time(m,now,&utc,&coarse);
}

static bool pre_lora_sync_display_context(const struct r2_ui_model *m,uint64_t now){
 return m->profile_supported&&m->profile_rc&&m->boundary_clock&&
   !m->profile_switching&&m->platform_ready&&m->ble_active&&
   now>=m->sampled_ms&&now-m->sampled_ms<=2500;
}

void pre_lora_r2_ui_sync_display_update(struct r2_ui_sync_display *c,const struct r2_ui_model *m,uint64_t now,bool drawn){
 if(!pre_lora_sync_display_context(m,now)||(c->at_ms&&now<c->at_ms)){
  *c=(struct r2_ui_sync_display){0};return;
 }
 int64_t utc;bool coarse;
 if(pre_lora_r2_ui_analog(m,now)&&r2_ui_display_time(m,now,&utc,&coarse)){
  /* Startup gets no grace until an actual analog frame has completed. */
  if(drawn||c->at_ms)*c=(struct r2_ui_sync_display){.utc_us=utc,.at_ms=now};
 }else if(c->at_ms){
  if(!c->loss_ms)c->loss_ms=now;
  /* One original loss deadline; repeated bad models cannot move it. */
  if(now<c->loss_ms||now-c->loss_ms>=500)*c=(struct r2_ui_sync_display){0};
 }
}

bool pre_lora_r2_ui_sync_display_time(const struct r2_ui_sync_display *c,const struct r2_ui_model *m,uint64_t now,int64_t *utc){
 if(!pre_lora_sync_display_context(m,now)||!c->at_ms||!c->loss_ms||now<c->at_ms||
    now<c->loss_ms||now-c->loss_ms>=500||c->utc_us<0)return false;
 uint64_t delta=now-c->at_ms;
 if(delta>(uint64_t)(INT64_MAX-c->utc_us)/1000)return false;
 *utc=c->utc_us+(int64_t)(delta*1000);return true;
}
