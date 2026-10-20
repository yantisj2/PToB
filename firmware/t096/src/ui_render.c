#if defined(PTOB_NAME_REGISTRY)
#include "ptob_registry.h"
#endif
/* SPDX-License-Identifier: Apache-2.0
 * Original pre-Zephyr watch/RF drawing, with an R2-only state adapter. */
#include "ui.h"
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define UI_FRAME_BYTES R2_UI_FRAME_BYTES
#define UI_COLOR(name) R2_UI_COLOR_##name
#define UI_PIXEL_WRITE(fb,offset,color) ((fb)[offset]=(uint8_t)(color))
#include "../legacy_ui/draw.inc"
#pragma GCC diagnostic pop

const char *r2_ui_reset_text(unsigned reason)
{
 switch(reason){
 case R2_UI_RESET_PIN:return "RESET PIN";
 case R2_UI_RESET_SOFTWARE:return "SOFTWARE";
 case R2_UI_RESET_POWER:return "POWER ON";
 case R2_UI_RESET_BROWNOUT:return "BROWNOUT";
 case R2_UI_RESET_WATCHDOG:return "WATCHDOG";
 case R2_UI_RESET_LOCKUP:return "CPU LOCKUP";
 case R2_UI_RESET_WAKE:return "WAKE";
 case R2_UI_RESET_DEBUG:return "DEBUG";
 case R2_UI_RESET_MULTIPLE:return "MULTIPLE";
 case R2_UI_RESET_WD_PROFILE:return "WD PROFILE";
 case R2_UI_RESET_WD_CORE:return "WD CPU";
 case R2_UI_RESET_WD_FAULT:return "WD FAULT";
 case R2_UI_RESET_WD_RECOVERY:return "WD RECOVERY";
 default:return "UNKNOWN";
 }
}
void r2_ui_uptime_text(char out[16],uint64_t uptime_ms)
{
 uint64_t seconds=uptime_ms/1000,days=seconds/86400;
 if(days>999999){snprintf(out,16,">999999d");return;}
 if(days>999){snprintf(out,16,"%llud %02uh",(unsigned long long)days,(unsigned)(seconds/3600%24));return;}
 snprintf(out,16,"%llud%02u:%02u:%02u",(unsigned long long)days,
  (unsigned)(seconds/3600%24),(unsigned)(seconds/60%60),(unsigned)(seconds%60));
}

void r2_ui_identity_name(char out[25],uint64_t id,const char *name)
{
 /* Authenticated supplied names precede local display aliases. */
 if(name&&name[0]&&strncmp(name,"Clock-",6)){snprintf(out,25,"%s",name);return;}
#if defined(PTOB_NAME_REGISTRY)
 const char *alias=ptob_registry_name(id);
 if(alias){snprintf(out,25,"%s",alias);return;}
#endif
 if(id)snprintf(out,25,"%04x",(unsigned)(id&UINT16_MAX));
 else snprintf(out,25,"--");
}
/* Diagnostic evidence stays separate from timing selection and HOT authority. */
static void shadow_value(char out[16],uint32_t ns,bool valid);
static bool gps_pps_live(const struct r2_ui_model *m,bool fresh){
 return fresh&&(m->clock.pps_present||(m->pps_precision.epoch&&
   m->pps_precision.age_us>=0&&m->pps_precision.age_us<R2_PPS_FRESH_US));
}
static bool local_pps_current(const struct r2_ui_model *m,uint64_t now){
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!gps_pps_live(m,true))return false;
 int64_t age=m->clock.pps_present?m->clock.pps_age_us:m->pps_precision.age_us;
 return age>=0&&age<R2_PPS_FRESH_US&&
   (now-m->sampled_ms)*1000<(uint64_t)(R2_PPS_FRESH_US-age);
}
static bool hot_current(const struct r2_ui_model *m,uint64_t now){
 return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&
   !m->profile_rc&&!m->profile_switching&&m->scope_active&&m->role==R2_UI_FOLLOWER&&
   m->local_hot_standby&&m->precision.authorized&&m->precision.map_valid;
}
static bool active_gm_current(const struct r2_ui_model *m,uint64_t now){
 return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&
   !m->profile_rc&&!m->profile_switching&&m->scope_active&&m->role==R2_UI_GM;
}
void r2_ui_holds_update(struct r2_ui_holds *h,const struct r2_ui_model *m,uint64_t now){
 uint32_t stamp=now/1000<UINT32_MAX?(uint32_t)(now/1000)+1:UINT32_MAX;
 if(!local_pps_current(m,now))h->pps_since=0;else if(!h->pps_since||h->pps_since>stamp)h->pps_since=stamp;
 if(!hot_current(m,now))h->hot_since=0;else if(!h->hot_since||h->hot_since>stamp)h->hot_since=stamp;
 if(!active_gm_current(m,now))h->active_since=0;else if(!h->active_since||h->active_since>stamp)h->active_since=stamp;
}
bool r2_ui_witness_page(const struct r2_ui_model *m,unsigned page){
 return page==R2_UI_SHADOW_PAGE||(page==R2_UI_PRECISION_PAGE&&m->profile_supported&&!m->profile_rc);
}
static void hold_text(char out[24],const char *name,uint32_t since,uint64_t now){
 uint64_t seconds=since&&now/1000>=since-1?now/1000-(since-1):0;
 if(seconds/60>9999)snprintf(out,24,"%s >9999m",name);
 else snprintf(out,24,"%s %02u:%02u",name,(unsigned)(seconds/60),(unsigned)(seconds%60));
}
static void active_hold_label(uint8_t *fb,int y,const char *text){
 if(strlen(text)<=12){label(fb,4,y,text,1,C_GREEN);return;}
 /* Keep four-digit minutes and seconds on one line. Reuse the 5x7 font,
  * trimming only empty glyph columns for unusually long durations. */
 int x=4;
 for(;*text;++text){
  if(*text==' '){x+=3;continue;}
  const uint8_t *g=glyph(*text);int first=0,last=4;
  while(first<4&&!g[first])++first;
  while(last>first&&!g[last])--last;
  for(int col=first;col<=last;++col)for(int row=0;row<7;++row)
   if(g[col]&(1u<<row))pixel(fb,x+col-first,y+row,C_GREEN);
  x+=last-first+2;
 }
}
static void shadow_mean(char out[24],const struct r2_shadow_session_view *v,bool valid){
 if(!valid)snprintf(out,24,"--");
 else if(v->mean_tenth_ns>-10000&&v->mean_tenth_ns<10000){int64_t n=v->mean_tenth_ns;snprintf(out,24,"%s%u.%u ns",n<0?"-":"+",(unsigned)((n<0?-n:n)/10),(unsigned)((n<0?-n:n)%10));}
 else {int64_t n=v->mean_tenth_ns/10;char magnitude[16];shadow_value(magnitude,(uint32_t)(n<0?-n:n),true);snprintf(out,24,"%c%s",n<0?'-':'+',magnitude);}
}
static const char *hot_title(const struct r2_ui_model *m,bool fresh,bool hot){
 if(!fresh)return "HOT --";
 if(m->profile_rc||!m->gm_capable)return "HOT OFF";
 if(hot)return "HOT";
 return m->role==R2_UI_GM?"ACTIVE GM":"HOT WAIT";
}
static const char *hot_reason(const struct r2_ui_model *m,bool fresh,bool hot){
 if(!fresh)return "STATE STALE";
 if(m->profile_rc)return "RC MODE";
 if(hot)return "QUALIFIED";
 if(m->role==R2_UI_GM)return m->hot_ready?"PEER HOT":"NO HOT PEER";
 if(!m->gm_capable)return "CLIENT ONLY";
 if(!m->clock.fix_active)return m->clock.rmc_status=='V'?"GPS NO FIX":"GPS WAIT";
 if(!gps_pps_live(m,fresh))return "PPS WAIT";
 if(!m->source_eligible)return "SOURCE DWELL";
 if(m->hot_refusal&1)return "PEER WAIT";
 if(m->hot_refusal&(4|8))return "PEER SOURCE";
 if(m->hot_refusal&16)return "AUTH WAIT";
 if(m->hot_refusal&32)return "BLE MAP WAIT";
 return "CONTROL WAIT";
}
/* Select authenticated serving time independently of local GPS diagnostics. */
void r2_ui_select_ble_time(struct r2_ui_model *m,uint64_t utc,uint64_t at,uint64_t now,uint64_t until,bool valid)
{
 bool local_coarse=m->clock.valid&&!m->ble_time_selected;
 m->ble_time_selected=false;m->ble_valid_until_us=0;
 if(m->role!=R2_UI_FOLLOWER)return;
 m->clock.valid=local_coarse;
 /* The timing owner bounds until using the immutable delivered map/source
  * expiry; presentation does not invent a shorter50ms worker-cadence lease.
  * Keep independently valid local coarse time if BLE output is unavailable. */
 if(!m->scope_active||!m->authenticated_gm||!valid||now<at||now>=until||utc>INT64_MAX-(now-at))return;
 m->clock.utc_us=(int64_t)(utc+now-at);m->clock.valid=true;
 m->ble_time_selected=true;m->ble_valid_until_us=until;m->sync_age_ms=(uint32_t)((now-at)/1000);
}
bool r2_ui_ble_current(const struct r2_ui_model *m,uint64_t now)
{
 return m->ble_time_selected&&now<=UINT64_MAX/1000&&now*1000<m->ble_valid_until_us;
}
bool r2_ui_analog(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA)return false;
#endif
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||!m->clock.valid||m->clock.utc_us<0||!m->scope_active)return false;
 if(m->role==R2_UI_GM)return m->clock.source_available||m->gm_source_fresh;
 if(m->boundary_clock)return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&r2_ui_ble_current(m,now)&&m->precision.map_valid;
 return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&r2_ui_ble_current(m,now)&&m->follower_pps_valid&&m->precision.map_valid&&m->sync_age_ms<=2500;
}
bool r2_ui_face_locked(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA)return false;
#endif
 if(m->role==R2_UI_GM||m->boundary_clock)return r2_ui_analog(m,now);
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||
    now>UINT64_MAX/1000||now*1000>=m->face_lock_until_us||m->face_rms_ns>200)return false;
 int64_t utc;bool coarse;
 return r2_ui_display_time(m,now,&utc,&coarse);
}
const char *r2_ui_servo_state(const struct r2_ui_model *m,uint64_t now)
{
 if(m->boundary_clock)return r2_ui_analog(m,now)?(m->rc_source_held?"HELD SOURCE":"RECEIVING"):"ACQUIRING";
 if(r2_ui_face_locked(m,now))return "LOCKED";
 if(now<m->sampled_ms||now-m->sampled_ms>2500||(m->ble_time_selected&&!r2_ui_ble_current(m,now))||!m->scope_active||
    m->role!=R2_UI_FOLLOWER||!m->precision.authorized)return "UNAVAILABLE";
 if(m->precision.locked&&m->precision.map_valid)return "LOCKED";
 /* Initial authorized acquisition is distinct from a previously accepted stale map. */
 if(!m->precision.map_valid&&m->precision.correction_age_valid&&m->precision.correction_age_ms>=1500)return "UNAVAILABLE";
 return "ACQUIRING";
}
bool r2_ui_saver_allowed(const struct r2_ui_model *m,uint64_t now)
{
 /* Saver has no dial loss grace: wake on actual source/map/PLL withdrawal.
  * A follower's local GPS is a witness and cannot stand in for BLE sync. */
 if(!r2_ui_face_locked(m,now)||!r2_ui_analog(m,now)||!m->scope_active||
#ifndef R2_DC_PROFILE
    !m->ble_active||
#endif
    m->profile_switching)return false;
 if(m->role==R2_UI_GM){
  if(!m->gm_source_fresh||!m->clock.pps_present||m->clock.pps_age_us<0||m->clock.pps_age_us>=R2_PPS_FRESH_US)return false;
  return (now-m->sampled_ms)*1000<(uint64_t)(R2_PPS_FRESH_US-m->clock.pps_age_us);
 }
 return m->precision.authorized&&m->precision.map_valid&&
   (m->boundary_clock||(m->precision.locked&&m->follower_pps_valid));
}
bool r2_ui_display_time(const struct r2_ui_model *m,uint64_t now,int64_t *utc,bool *coarse)
{
 *coarse=false;
 if(now<m->sampled_ms||now-m->sampled_ms>2500)return false;
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA&&m->lora.utc_us&&now<=UINT64_MAX/1000&&now*1000>=m->lora.sampled_us&&now*1000<m->lora.until_us){
  uint64_t value=m->lora.utc_us+(now*1000-m->lora.sampled_us);
  if(value>INT64_MAX)return false;
  *utc=(int64_t)value;*coarse=true;return true;
 }
#endif
 if(m->clock.valid&&m->clock.utc_us>=0&&(!m->ble_time_selected||r2_ui_ble_current(m,now))){
  uint64_t delta=m->ble_time_selected?(now-m->sampled_ms)*1000:0;
  if(delta>(uint64_t)(INT64_MAX-m->clock.utc_us))return false;
  *utc=m->clock.utc_us+(int64_t)delta;return true;
 }
 if(!m->coarse_valid||now>UINT64_MAX/1000||now*1000<m->coarse_at_us||
    m->coarse_utc_us>INT64_MAX||now*1000-m->coarse_at_us>(uint64_t)INT64_MAX-m->coarse_utc_us)return false;
 *utc=(int64_t)(m->coarse_utc_us+now*1000-m->coarse_at_us);*coarse=true;return true;
}
static bool sync_display_context(const struct r2_ui_model *m,uint64_t now){
 return m->profile_supported&&m->profile_rc&&m->boundary_clock&&
   !m->profile_switching&&m->platform_ready&&m->ble_active&&
   now>=m->sampled_ms&&now-m->sampled_ms<=2500;
}
void r2_ui_sync_display_update(struct r2_ui_sync_display *c,const struct r2_ui_model *m,uint64_t now,bool drawn){
 if(!sync_display_context(m,now)||(c->at_ms&&now<c->at_ms)){
  *c=(struct r2_ui_sync_display){0};return;
 }
 int64_t utc;bool coarse;
 if(r2_ui_analog(m,now)&&r2_ui_display_time(m,now,&utc,&coarse)){
  /* Startup gets no grace until an actual analog frame has completed. */
  if(drawn||c->at_ms)*c=(struct r2_ui_sync_display){.utc_us=utc,.at_ms=now};
 }else if(c->at_ms){
  if(!c->loss_ms)c->loss_ms=now;
  /* One original loss deadline; repeated bad models cannot move it. */
  if(now<c->loss_ms||now-c->loss_ms>=500)*c=(struct r2_ui_sync_display){0};
 }
}
bool r2_ui_sync_display_time(const struct r2_ui_sync_display *c,const struct r2_ui_model *m,uint64_t now,int64_t *utc){
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA)return false;
#endif
 if(!sync_display_context(m,now)||!c->at_ms||!c->loss_ms||now<c->at_ms||
    now<c->loss_ms||now-c->loss_ms>=500||c->utc_us<0)return false;
 uint64_t delta=now-c->at_ms;
 if(delta>(uint64_t)(INT64_MAX-c->utc_us)/1000)return false;
 *utc=c->utc_us+(int64_t)(delta*1000);return true;
}
#ifdef R2_DC_PROFILE
static void dc_age(char out[12],uint32_t ms){
 if(ms==UINT32_MAX)snprintf(out,12,"--");else if(ms<1000)snprintf(out,12,"%ums",ms);
 else if(ms<1000000)snprintf(out,12,"%us",ms/1000);else snprintf(out,12,">999s");
}
#endif
static uint32_t age_ms(int64_t age){return age<0||age/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(age/1000);}
void r2_ui_reception(struct r2_ui_model *m,const struct r2_link_quality_view *q,uint64_t now){
 uint64_t age=q->last_rx&&now>=q->last_rx?(now-q->last_rx)/1000:UINT64_MAX;
 m->ble_rx_age_ms=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 m->ble_delivery_per_mille=q->delivery_per_mille>1000?1000:q->delivery_per_mille;
 m->ble_reception_valid=q->samples&&q->last_rx&&now>=q->last_rx;
}
static void shadow_value(char out[16],uint32_t ns,bool valid){
 if(!valid)snprintf(out,16,"--");
 else if(ns<1000)snprintf(out,16,"%u ns",ns);
 else if(ns<1000000)snprintf(out,16,"%u.%03u us",ns/1000,ns%1000);
 else snprintf(out,16,"%u.%03u ms",ns/1000000,(ns%1000000)/1000);
}
void r2_ui_rf_lamps(uint8_t out[8],const struct r2_ui_model *m,uint64_t now)
{
 memset(out,0,8);
 /* Alternate125ms ON/OFF frames: continuous traffic must visibly flash4Hz.
  * The activity latch still comes solely from observed packet events. */
 if(m->ble_active&&now>=m->sampled_ms&&now-m->sampled_ms<250&&!(now/125%2))
  memcpy(out,m->lamps,8);
}
void r2_ui_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *status,uint64_t now)
{
 bool model_fresh=now>=m->sampled_ms&&now-m->sampled_ms<=2500;
 bool fresh=model_fresh;
 if(m->ble_time_selected&&!r2_ui_ble_current(m,now))fresh=false;
 ui_snapshot_t s={0};
 s.unit_name=m->unit;s.page=status->page;s.rf_help=status->help;
 bool face_locked=r2_ui_face_locked(m,now);
 int64_t grace_utc=0;
 bool sync_grace=r2_ui_sync_display_time(&status->sync_display,m,now,&grace_utc);
 bool face_visible=face_locked||sync_grace;
 bool follower_face=face_locked&&m->role!=R2_UI_GM&&!m->boundary_clock;
 s.native_boundary_clock=m->boundary_clock;s.native_boundary_held=m->rc_source_held||sync_grace;
 s.native_profile_supported=m->profile_supported;s.native_profile_rc=m->profile_rc;
 s.native_rc_serving_clients=m->profile_rc&&!m->profile_switching&&fresh&&
     m->ble_active&&r2_ui_ble_current(m,now)&&m->rc_serving_clients;
 s.native_time_relay_hops=m->time_relay_hops;
 bool metric_current=!m->boundary_clock||r2_ui_ble_current(m,now);
 s.native_pll_locked=metric_current&&fresh&&m->scope_active&&m->role==R2_UI_FOLLOWER&&m->precision.authorized&&m->precision.map_valid&&m->precision.locked;
 s.native_rms_valid=metric_current&&(follower_face||(fresh&&m->scope_active&&m->precision.rms_valid));
 r2_precision_format(s.native_rms,sizeof(s.native_rms),follower_face?m->face_rms_ns:m->precision.rms_ns,s.native_rms_valid);
 if(s.native_rms_valid&&m->precision.rms_ns>9999999)snprintf(s.native_rms,sizeof(s.native_rms),">9999 us");
 s.die_temperature_valid=model_fresh&&m->temperature_valid;
 s.die_temperature_c=m->temperature_qc/4;
 s.gnss_native_health=true;s.native_served_time=face_visible||(fresh&&m->clock.valid);
 s.native_analog_eligible=face_visible;
 s.native_ble_time=(face_visible&&m->role!=R2_UI_GM)||(fresh&&m->ble_time_selected);
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA){
  s.native_pll_locked=s.native_rms_valid=s.native_served_time=s.native_ble_time=false;
 }
#endif
 s.native_hot_standby=fresh&&m->scope_active&&m->role==R2_UI_FOLLOWER&&m->local_hot_standby&&m->precision.authorized&&m->precision.map_valid;
 int64_t display_utc=0;bool coarse=false;
 s.utc_valid=r2_ui_display_time(m,now,&display_utc,&coarse);
 if(sync_grace){s.utc_valid=true;display_utc=grace_utc;coarse=true;}
 s.utc_second=s.utc_valid?(uint32_t)(display_utc/1000000):0;
 s.subsecond_quarter=s.utc_valid?(display_utc%1000000)/250000:0;
 s.state=!s.utc_valid?CLOCK_NO_TIME:s.native_analog_eligible?CLOCK_LOCKED:CLOCK_ACQUIRING;
 bool settling=now>=m->sampled_ms&&now-m->sampled_ms<=2500&&(m->sync_attempt||(m->boundary_clock&&m->ble_active&&!r2_ui_ble_current(m,now)))&&!s.native_analog_eligible;
 if(settling)s.state=CLOCK_ACQUIRING;
 s.native_reason=settling?UI_NATIVE_NONE:coarse?UI_NATIVE_SOURCE_STALE:!fresh?UI_NATIVE_SOURCE_STALE:!s.utc_valid?UI_NATIVE_NO_TIME:
     m->role==R2_UI_FOLLOWER?UI_NATIVE_PPS_UNASSOCIATED:!m->scope_active?UI_NATIVE_NO_GM:UI_NATIVE_LOCAL_COARSE;
 s.boot_elapsed_seconds=now/1000;s.pps_seen=fresh&&m->clock.pps_present;
 s.pps_age_ms=age_ms(m->clock.pps_age_us);s.rmc_age_ms=age_ms(m->clock.gps_age_us);
 s.gnss_bytes_seen=m->clock.rx_age_us>=0;s.gnss_rx_age_ms=age_ms(m->clock.rx_age_us);
 s.gnss_sv_seen=m->clock.sats_used;s.gnss_signal_age_s=age_ms(m->clock.gga_age_us)/1000;
 /* The lamp reports only a recent local GNSS PPS edge in every clock role.
  * BLE UTC, map/PLL lock and HOT status cannot synthesize this indication. */
 s.pulse_indicator=s.pps_seen&&m->clock.pps_age_us>=0&&
     m->clock.pps_age_us<250000&&now-m->sampled_ms<250&&
     (uint64_t)m->clock.pps_age_us+(now-m->sampled_ms)*1000<250000?UI_PULSE_PPS:UI_PULSE_OFF;
 s.ble_mode_known=true;s.ble_mode_enabled=m->ble_active;s.ble_tx_active=m->ble_active;
 s.rf_ble_tx_mode=m->ble_active;
 s.ui_role.grandmaster=fresh&&m->scope_active&&m->role==R2_UI_GM;
 s.native_rx_known=model_fresh&&m->ble_active&&m->ble_reception_valid;
 if(s.native_rx_known&&m->ble_rx_age_ms<1500&&now-m->sampled_ms<1500-m->ble_rx_age_ms)
  s.native_rx_bars=m->ble_delivery_per_mille>=900?4:m->ble_delivery_per_mille>=750?3:m->ble_delivery_per_mille>=500?2:m->ble_delivery_per_mille?1:0;
 if(settling||(face_visible&&m->role!=R2_UI_GM)||(fresh&&m->role==R2_UI_FOLLOWER&&m->scope_active))
  snprintf(s.ui_role.header,sizeof(s.ui_role.header),"%s>%.8s",m->unit,m->relay_selected?m->upstream_name:m->root_name);
 else snprintf(s.ui_role.header,sizeof(s.ui_role.header),"%s",m->unit);
 /* DC displays the standard USB client sampling rate. The existing dial
  * renderer colors the entire badge from the live die temperature. */
#ifdef R2_DC_PROFILE
 s.active_ble_slots=32;
#else
 /* Beat value comes from timing owner, not display cadence or control traffic. */
 s.active_ble_slots=fresh?m->precision_beat_hz:0;
#endif
 s.rf_sources.ble=(ui_rf_source_t){.available=fresh&&m->scope_active&&m->ble_time_selected&&s.utc_valid,
   .fresh=fresh&&m->scope_active&&m->ble_time_selected,.utc=true,.second=s.utc_second};
#ifdef R2_LORA
 if(m->lora.choice==RC8_SELECT_LORA)s.rf_sources.ble=(ui_rf_source_t){0};
#endif
 /* Transport activity remains visible while time/map qualification is absent.
  * A stalled model cannot keep lamps on; no time authority is inferred. */
 r2_ui_rf_lamps(s.rf_lamps[UI_RF_BLE],m,now);
 fill(fb,0,0,80,160,C_BG);
#ifdef R2_DC_PROFILE
 if(status->help){
  label(fb,5,10,"DC GNSS USB",1,C_WHITE);label(fb,5,35,"PRESS: PAGE",1,C_DIM);
  label(fb,5,60,"4 SEC: OFF",1,C_AMBER);label(fb,5,85,"FLASH ONLY",1,C_DIM);
  label(fb,5,110,"USB RMS IS",1,C_WHITE);label(fb,5,125,"SOF CADENCE",1,C_WHITE);
  label(fb,5,148,"PRESS:BACK",1,C_DIM);return;
 }
 if(status->page!=0&&!status->screensaver){
  char value[24];label(fb,4,4,"DC GNSS USB",1,C_WHITE);
  if(status->page==3){
   label(fb,4,25,"UPTIME",1,C_DIM);r2_ui_uptime_text(value,now);label(fb,4,39,value,1,C_WHITE);
   label(fb,4,60,"LAST RESET",1,C_DIM);label(fb,4,74,r2_ui_reset_text(m->reset_kind),1,C_WHITE);
   if(m->temperature_valid)snprintf(value,sizeof value,"DIE %d C",m->temperature_qc/4);else snprintf(value,sizeof value,"DIE --");
   label(fb,4,97,value,1,C_DIM);label(fb,4,119,"BLE OFF",1,C_DIM);
   label(fb,4,136,"FLASH ONLY",1,C_DIM);label(fb,4,151,"4 SEC: OFF",1,C_DIM);return;
  }
  label(fb,4,24,fresh&&m->clock.fix_active?"GPS FIX":"GPS WAIT",1,C_DIM);
  label(fb,4,40,gps_pps_live(m,fresh)?"PPS LIVE":"PPS WAIT",1,C_DIM);
  snprintf(value,sizeof value,"SV USED %u",fresh?m->clock.sats_used:0);label(fb,4,56,value,1,C_WHITE);
  if(status->page==2){
   dc_age(value,fresh?age_ms(m->clock.gps_age_us):UINT32_MAX);label(fb,4,77,"RMC AGE",1,C_DIM);label(fb,4,91,value,1,C_WHITE);
   dc_age(value,fresh?age_ms(m->clock.pps_age_us):UINT32_MAX);label(fb,4,111,"PPS AGE",1,C_DIM);label(fb,4,125,value,1,C_WHITE);
   label(fb,4,149,"UTC ACC --",1,C_DIM);return;
  }
  label(fb,4,76,m->scope_active&&fresh?"USB SOURCE":"SOURCE WAIT",1,C_DIM);
  label(fb,4,92,"USB RMS",1,C_DIM);label(fb,4,106,s.native_rms,1,s.native_rms_valid?C_CYAN:C_DIM);
  snprintf(value,sizeof value,"N %u",fresh?m->precision.samples:0);label(fb,4,123,value,1,C_DIM);
  label(fb,4,138,"HOPS 0",1,C_DIM);label(fb,4,151,"UTC ACC --",1,C_DIM);return;
 }
#endif
 if(status->screensaver&&r2_ui_saver_allowed(m,now)){
  /* Existing Casio materials/geometry, rigidly translated as one assembly.
   * 13px source strip +2px gap +62px dial +2px gap +13px identity.
   * No digital/dashboard panels are drawn in the saver. */
  int top=(int)r2_ui_saver_top(status->screensaver_position);
  fill(fb,0,0,80,160,UI_COLOR(BLACK));
  round_panel(fb,3,top,74,13,C_PANEL,C_BEZEL);
  label(fb,8,top+3,"UTC",1,C_DIM);
  label(fb,36,top+3,s.native_ble_time?(s.native_time_relay_hops==1?"RC1":s.native_time_relay_hops==2?"RC2":"BLE"):"PPS",1,C_GREEN);
  ui_render_pulse_at(fb,s.pulse_indicator,66,(unsigned)(top+2));
  round_panel(fb,3,top+15,74,62,C_PANEL,C_BEZEL);
  analog_clock(fb,&s,top+44);
  role_header_at(fb,&s,top+79);return;
 }
#ifdef R2_LORA
 if(status->page==R2_UI_LORA_PAGE){
  const struct r2_lora_view *v=&m->lora;char text[24];
  round_panel(fb,3,1,74,13,C_PANEL,C_BEZEL);label(fb,13,4,"LoRa RC8",1,C_WHITE);
  snprintf(text,sizeof text,"%s %s",v->transmitter?"TX":"RX",v->requested?"ON":"OFF");
  label(fb,5,22,text,2,v->requested?C_CYAN:C_DIM);
  const char *state=v->fault?"RADIO FAULT":!v->requested?"DISABLED":!v->active?"INHIBITED":v->transmitter?(v->state==RC8_AVAILABLE?"SOURCE READY":"WAIT SOURCE"):v->state==RC8_AVAILABLE?"MATCHED TIME":v->state==RC8_EXPIRED?"HOLD EXPIRED":"WAIT PACKETS";
  label(fb,4,44,state,1,C_WHITE);
  if(v->requested&&v->active&&v->utc_us&&v->until_us>now*1000){
   uint64_t sec=(v->utc_us+(now*1000>=v->sampled_us?now*1000-v->sampled_us:0))/1000000;
   snprintf(text,sizeof text,"%02u:%02u:%02u",(unsigned)(sec/3600%24),(unsigned)(sec/60%60),(unsigned)(sec%60));
  }else snprintf(text,sizeof text,"--:--:--");
  label(fb,4,63,text,1,C_WHITE);
  label(fb,4,80,"UNKNOWN",1,C_AMBER);label(fb,4,92,v->quality_known?"TIME BOUND":"META UNKNOWN",1,C_DIM);
  if(v->parent_tag)snprintf(text,sizeof text,"R%04X P%04X",v->root_tag,v->parent_tag);else snprintf(text,sizeof text,"R%04X P----",v->root_tag);label(fb,4,104,text,1,C_DIM);
  if(!v->transmitter){if(v->age_ms>=12000)snprintf(text,sizeof text,"AGE >12s");else snprintf(text,sizeof text,"AGE %u.%us",v->age_ms/1000,(v->age_ms%1000)/100);label(fb,4,116,text,1,C_WHITE);}
  label(fb,4,130,v->choice==RC8_SELECT_BLE?"BLE SELECTED":v->choice==RC8_SELECT_LOCAL_GM?"GM SELECTED":v->choice==RC8_SELECT_LORA&&v->requested&&!v->fault?"RC8 SELECTED":"NO CLOCK",1,C_DIM);
  label(fb,4,145,"HOLD: TOGGLE",1,C_AMBER);return;
 }
#endif
 if(status->page==R2_UI_SHADOW_PAGE&&!status->help){
  const struct r2_shadow_session_view *v=&status->shadow;char text[24];
  round_panel(fb,3,1,74,13,C_PANEL,C_BEZEL);label(fb,10,4,"SHADOW PPS",1,C_WHITE);
  const char *state=v->state==R2_SHADOW_GM?"GM: NO TEST":v->state==R2_SHADOW_DWELL?"START IN 1S":v->state==R2_SHADOW_WAIT_PPS?"WAIT PPS":v->state==R2_SHADOW_WAIT_MAP?"WAIT BLE MAP":v->state==R2_SHADOW_SETTLING?"SETTLING 10S":v->state==R2_SHADOW_RUNNING?"MEASURING":v->state==R2_SHADOW_FULL?"SESSION FULL":"READY";
  label(fb,4,20,state,1,v->state==R2_SHADOW_RUNNING?C_GREEN:C_AMBER);
  uint64_t secs=v->elapsed_ms/1000;
  if(secs<360000)snprintf(text,sizeof text,"TIME %02u:%02u",(unsigned)(secs/60),(unsigned)(secs%60));else snprintf(text,sizeof text,"TIME >99H");
  label(fb,4,32,text,1,C_WHITE);
  label(fb,4,47,"MEAN",1,C_DIM);
  shadow_mean(text,v,v->valid);
  label(fb,4,58,text,1,v->valid?C_CYAN:C_WHITE);
  const unsigned y[3]={72,97,122};const uint32_t values[3]={v->p95_ns,v->p99_ns,v->max_ns};const char *names[3]={"P95 ABS","P99 ABS","MAX ABS"};
  for(unsigned i=0;i<3;i++){char magnitude[16];const char *name=v->approximate&&i<2?(i?"P99 APX":"P95 APX"):names[i];label(fb,4,y[i],name,1,C_DIM);shadow_value(magnitude,values[i],v->valid);label(fb,4,y[i]+11,magnitude,1,v->valid?C_CYAN:C_WHITE);}
  if(v->state==R2_SHADOW_SETTLING)snprintf(text,sizeof text,"WAIT %us W%u",v->settling_ms>=10000?0:10-v->settling_ms/1000,v->warmup);
  else if(v->samples<100000&&v->skipped<100)snprintf(text,sizeof text,"N%u S%u",v->samples,v->skipped);else snprintf(text,sizeof text,"N %u",v->samples);
  label(fb,4,150,text,1,C_DIM);return;
 }
 if(status->help||status->page==5){
  if(status->page==1){rf_live(fb,&s);return;}
  round_panel(fb,3,1,74,13,C_PANEL,C_BEZEL);label(fb,7,4,"CLOCK KEY",1,C_WHITE);
  label(fb,5,24,"PRESS: NEXT",1,C_WHITE);label(fb,5,40,"HOLD: HELP",1,C_WHITE);
  label(fb,5,56,"4 SEC: OFF",1,C_AMBER);label(fb,5,79,"DIAL NEEDS",1,C_DIM);
  label(fb,5,93,"SERVING GM",1,C_WHITE);label(fb,5,107,"OR BLE SYNC",1,C_WHITE);
  label(fb,5,131,"GM/RC: MODE",1,C_DIM);label(fb,5,148,"PRESS:BACK",1,C_DIM);return;
 }
 if(status->page==0){front_clock(fb,&s);
#ifdef R2_LORA
  if(m->lora.choice==RC8_SELECT_LORA&&m->lora.until_us>now*1000){
   fill(fb,0,0,80,15,C_BG);label(fb,4,4,"RC8 UNKNOWN",1,C_AMBER);
  }
#endif
#ifdef R2_PC_PROFILE
  fill(fb,0,0,80,15,C_BG);label(fb,10,4,"PC AWAKE",1,C_AMBER);
#endif
  if(m->profile_supported&&now<status->profile_flash_until&&((now/400)%2)==0){
   fill(fb,0,0,80,15,C_BG);label(fb,status->profile_target_rc?1:10,4,status->profile_target_rc?"RELAY PROFILE":"GM PROFILE",1,C_AMBER);
  }
  return;}
 if(status->page==1){rf_live(fb,&s);return;}
 if(status->page==2){
  bool gm=m->role==R2_UI_GM;
  bool active=active_gm_current(m,now);
  bool authorized=fresh&&m->scope_active&&m->precision.authorized;
  const char *state=gm?(fresh&&m->gm_source_fresh&&m->pps_precision.rate_valid?"PPS TRACK":"SOURCE WAIT"):r2_ui_servo_state(m,now);
  char value[24];
  round_panel(fb,3,1,74,13,C_PANEL,C_BEZEL);label(fb,13,4,m->boundary_clock?"RC TIMING":"PRECISION",1,C_WHITE);
  if(!fresh||!m->scope_active)snprintf(value,sizeof(value),"NO TIMING");
  else if(active)snprintf(value,sizeof(value),"GRANDMASTER");
  else if(gm)snprintf(value,sizeof(value),"GPS GM");
  else if(m->time_relay_hops)snprintf(value,sizeof(value),"BLE RC%u",m->time_relay_hops);
  else snprintf(value,sizeof(value),"BLE GM");
  bool locked=!gm&&authorized&&m->precision.map_valid&&(m->precision.locked||face_locked||s.native_hot_standby);
  label(fb,4,20,value,1,active||locked||(gm&&fresh&&m->gm_source_fresh)?C_GREEN:C_AMBER);
  if(locked&&!m->boundary_clock){pair_lock(fb,4,32,C_GREEN);label(fb,17,33,"SoftPLL",1,C_GREEN);}
  else label(fb,4,33,state,1,m->boundary_clock?(r2_ui_analog(m,now)&&!m->rc_source_held?C_GREEN:C_AMBER):(gm&&fresh&&m->gm_source_fresh&&m->pps_precision.rate_valid?C_GREEN:C_AMBER));
  if(r2_ui_witness_page(m,status->page)){
   if(gm){
    bool beat=authorized&&m->ble_active&&m->gm_source_fresh&&m->precision.rms_valid&&m->precision.rms_ns!=UINT32_MAX;
    bluetooth_badge_at(fb,4,49,beat);label(fb,19,51,"BEAT RMS",1,C_DIM);
    shadow_value(value,m->precision.rms_ns,beat);label(fb,4,64,value,1,beat?C_CYAN:C_DIM);
    if(beat)snprintf(value,sizeof value,"N %u",m->precision.samples);else snprintf(value,sizeof value,"N --");
    label(fb,4,84,value,1,C_DIM);
    label(fb,4,104,!fresh?"STATE STALE":!m->ble_active?"BLE OFF":!m->gm_source_fresh?"SOURCE WAIT":beat?"TX BEAT":"WAIT BEATS",1,C_DIM);
   }else{
   const struct r2_shadow_session_view *v=&status->shadow;
   bool valid=fresh&&local_pps_current(m,now)&&m->clock.fix_active&&m->precision.map_valid&&v->active&&v->valid;
   label(fb,4,50,"SHD PPS MEAN",1,C_DIM);
   shadow_mean(value,v,valid);label(fb,4,64,value,1,valid?C_CYAN:C_DIM);
   label(fb,4,80,"RMS",1,C_DIM);shadow_value(value,v->rms_ns,valid);label(fb,4,92,value,1,valid?C_CYAN:C_DIM);
   if(!local_pps_current(m,now))snprintf(value,sizeof value,"WAIT PPS");
   else if(!m->clock.fix_active)snprintf(value,sizeof value,"WAIT GPS FIX");
   else if(!fresh||!m->precision.map_valid)snprintf(value,sizeof value,"WAIT BLE MAP");
   else if(v->state==R2_SHADOW_SETTLING)snprintf(value,sizeof value,"SETTLE %us",v->settling_ms>=10000?0:10-v->settling_ms/1000);
   else if(!valid)snprintf(value,sizeof value,"STARTING");
   else snprintf(value,sizeof value,"N %u",v->samples);
   label(fb,4,104,value,1,C_DIM);
   }
   snprintf(value,sizeof value,"%s SV%u",!fresh?"GPS --":m->clock.fix_active?"GPS FIX":"GPS WAIT",fresh?m->clock.sats_used:0);label(fb,4,116,value,1,fresh&&m->clock.fix_active?C_GREEN:C_AMBER);
   bool pps=local_pps_current(m,now),hot=hot_current(m,now);
   hold_text(value,"PPS",pps?status->holds.pps_since:0,now);label(fb,4,128,value,1,pps?C_GREEN:C_AMBER);
   if(active)hold_text(value,"ACTIVE",status->holds.active_since,now);
   else if(hot)hold_text(value,"HOT",status->holds.hot_since,now);else snprintf(value,sizeof value,"%s",hot_title(m,fresh,false));
   if(active)active_hold_label(fb,140,value);else label(fb,4,140,value,1,hot?C_GREEN:C_AMBER);
   const char *why=hot_reason(m,fresh,hot);
   if(!strcmp(why,"SOURCE DWELL"))snprintf(value,sizeof value,"DWELL %us",(m->admission_ms+999)/1000);
   else snprintf(value,sizeof value,"%s",why);
   label(fb,4,152,value,1,C_DIM);return;
  }
  bool ble_rms=!gm&&authorized&&m->ble_active&&r2_ui_ble_current(m,now)&&
    m->precision.map_valid&&m->precision.rms_valid&&m->precision.rms_ns!=UINT32_MAX;
  label(fb,4,51,gm?"SOURCE PPS":"BLE RMS",1,C_DIM);
  if(gm)snprintf(value,sizeof(value),"%s",gps_pps_live(m,fresh)?"LIVE":"WAIT");
  else shadow_value(value,m->precision.rms_ns,ble_rms);
  label(fb,4,64,value,1,gm?C_WHITE:ble_rms?C_CYAN:C_DIM);
  label(fb,4,83,!fresh?"GPS --":m->clock.fix_active?"GPS FIX":"GPS NO FIX",1,fresh&&m->clock.fix_active?C_GREEN:C_AMBER);
  snprintf(value,sizeof(value),"SV USED %u",fresh?m->clock.sats_used:0);label(fb,4,96,value,1,C_WHITE);
  label(fb,4,109,gps_pps_live(m,fresh)?"PPS LIVE":"PPS WAIT",1,gps_pps_live(m,fresh)?C_GREEN:C_AMBER);
  if(active)hold_text(value,"ACTIVE",status->holds.active_since,now);else snprintf(value,sizeof value,"%s",hot_title(m,fresh,s.native_hot_standby));
  if(active)active_hold_label(fb,128,value);else label(fb,4,128,value,1,s.native_hot_standby?C_GREEN:C_AMBER);
  const char *why=hot_reason(m,fresh,s.native_hot_standby);
  if(!strcmp(why,"SOURCE DWELL"))snprintf(value,sizeof(value),"DWELL %us",(m->admission_ms+999)/1000);
  else snprintf(value,sizeof(value),"%s",why);
  label(fb,4,141,value,1,C_DIM);return;
 }
 if(status->page==3){
  char value[24];
  round_panel(fb,1,1,78,13,C_PANEL,C_BEZEL);label(fb,2,4,"CLOCK QUALITY",1,C_WHITE);
  label(fb,3,20,!fresh?"STATE STALE":m->clock.fix_active?"GPS FIX":"GPS NO FIX",1,fresh&&m->clock.fix_active?C_GREEN:C_AMBER);
  snprintf(value,sizeof(value),"SV USED %u",fresh?m->clock.sats_used:0);label(fb,3,33,value,1,C_WHITE);
  snprintf(value,sizeof(value),"RMC %c MODE %c",fresh&&m->clock.rmc_status?m->clock.rmc_status:'-',fresh&&m->clock.rmc_mode?m->clock.rmc_mode:'-');label(fb,3,46,value,1,C_DIM);
  if(fresh&&m->clock.gps_age_us>=0&&m->clock.gps_age_us<10000000)snprintf(value,sizeof(value),"GPS %ums",age_ms(m->clock.gps_age_us));else snprintf(value,sizeof(value),"GPS STALE");
  label(fb,3,63,value,1,C_WHITE);
  label(fb,3,76,gps_pps_live(m,fresh)?"PPS LIVE":"PPS WAIT",1,gps_pps_live(m,fresh)?C_GREEN:C_AMBER);
  int64_t pps_age=m->pps_precision.epoch?m->pps_precision.age_us:m->clock.pps_age_us;
  if(fresh&&pps_age>=0&&pps_age<10000000)snprintf(value,sizeof(value),"PPS %ums",age_ms(pps_age));else snprintf(value,sizeof(value),"PPS STALE");
  label(fb,3,89,value,1,C_WHITE);
  if(!fresh)snprintf(value,sizeof(value),"SOURCE --");
  else if(m->profile_rc||!m->gm_capable)snprintf(value,sizeof(value),"SOURCE OFF");
  else if(m->source_eligible)snprintf(value,sizeof(value),"SOURCE READY");
  else if(m->clock.fix_active&&gps_pps_live(m,fresh))snprintf(value,sizeof(value),"DWELL %us",(m->admission_ms+999)/1000);
  else snprintf(value,sizeof(value),"SOURCE WAIT");
  label(fb,3,107,value,1,fresh&&m->source_eligible?C_GREEN:C_AMBER);
  if(hot_current(m,now))hold_text(value,"HOT",status->holds.hot_since,now);else snprintf(value,sizeof value,"%s",hot_title(m,fresh,false));
  label(fb,3,120,value,1,hot_current(m,now)?C_GREEN:C_AMBER);
  label(fb,3,133,hot_reason(m,fresh,s.native_hot_standby),1,C_DIM);
  label(fb,3,149,"UTC ACC --",1,C_DIM);return;
 }
 round_panel(fb,3,1,74,13,C_PANEL,C_BEZEL);label(fb,7,4,"SYSTEM",1,C_WHITE);
 char text[16];label(fb,5,23,"UPTIME",1,C_DIM);
 r2_ui_uptime_text(text,now);label(fb,5,35,text,1,C_WHITE);
 label(fb,5,51,"LAST RESET",1,C_DIM);
 label(fb,5,63,r2_ui_reset_text(m->reset_kind),1,C_WHITE);
 snprintf(text,sizeof(text),"SAT %u",fresh?m->clock.sats_used:0);label(fb,5,82,text,1,C_WHITE);
 snprintf(text,sizeof(text),"RX %u",m->rx);label(fb,5,94,text,1,C_WHITE);
 snprintf(text,sizeof(text),"AUTH %u",m->authenticated);label(fb,5,106,text,1,C_WHITE);
 snprintf(text,sizeof(text),"DROP %u",m->queue_drops);label(fb,5,118,text,1,C_WHITE);
 label(fb,5,134,m->ble_active?"BLE RUNNING":"BLE STOPPED",1,C_CYAN);
 label(fb,5,149,"4 SEC: OFF",1,C_DIM);
}
