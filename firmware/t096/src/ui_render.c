#if defined(PTOB_NAME_REGISTRY)
#include "ptob_registry.h"
#endif
/* SPDX-License-Identifier: Apache-2.0
 * Original pre-Zephyr watch/RF drawing, with an R2-only state adapter. */
#include "ui.h"
#ifdef R2_TZ_AUTO
#include "time_zone_config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* Single ordinary UI caller only. No callback runs in an ISR, a mutex region
 * or an active SPI/DMA call. Cancelled pixels remain unpublished scratch. */
#ifdef R2_UI_PARTIAL_JOBS
static struct {
 r2_ui_render_checkpoint_fn checkpoint;
 void *context;
 unsigned remaining;
 unsigned x,y,w,h;
 bool active,aborted,critical,drawing;
} ui_raster;
static bool raster_checkpoint(void){
 if(!ui_raster.active)return true;
 if(!ui_raster.remaining)ui_raster.remaining=R2_UI_WIDTH;
 if(--ui_raster.remaining)return !ui_raster.aborted;
 ui_raster.remaining=R2_UI_WIDTH;
 if(ui_raster.checkpoint&&!ui_raster.checkpoint(ui_raster.context,R2_UI_RENDER_PIXELS))ui_raster.aborted=true;
 return !ui_raster.aborted;
}
/* Palette-only bulk spans keep the identical pre-80th-pixel checkpoint.
 * A refused continuation cannot modify even the boundary pixel. There is
 * no per-pixel atomic read, extra canvas or change to the ready-PT yield. */
static bool raster_fill_row(uint8_t *fb,int x,int y,int width,uint16_t color){
 /* Sole callers are fill/round_panel, whose shared rectangle intersection
  * already guarantees screen/ROI bounds. Clip once per primitive, not row. */
 if(width<=0)return true;
 uint8_t *out=fb+(unsigned)y*R2_UI_WIDTH+(unsigned)x;
 if(ui_raster.aborted)return false;
 /* A screen-clipped row contains<=80 pixels, so at most one checkpoint
  * can occur. The remaining suffix never needs a generic chunk loop. */
 if(ui_raster.active&&(unsigned)width>=ui_raster.remaining){
  unsigned bulk=ui_raster.remaining-1;
  memset(out,(uint8_t)color,bulk);out+=bulk;width-=(int)bulk;
  ui_raster.remaining=1;
  if(!raster_checkpoint())return false;
  *out++=(uint8_t)color;--width;
 }
 memset(out,(uint8_t)color,(unsigned)width);
 if(ui_raster.active)ui_raster.remaining-=(unsigned)width;
 return true;
}
#define R2_UI_FILL_ROW(fb,x,y,w,color) raster_fill_row(fb,x,y,w,color)
#define R2_UI_DRAW_CALL(call) do { call; if(ui_raster.aborted)return; } while(0)
#define R2_UI_PIXEL_CHECKPOINT() do { if(!raster_checkpoint())return false; } while(0)
#define R2_UI_BOOL_ABORTED() ui_raster.aborted
static bool raster_boundary(enum r2_ui_render_stage stage){
 /* Adapter/calendar work stays base9 until the first ui_raster primitive. */
 if(stage==R2_UI_RENDER_PIXELS&&!ui_raster.critical&&!ui_raster.drawing)stage=R2_UI_RENDER_OPTIONAL;
 if(ui_raster.active&&ui_raster.checkpoint&&!ui_raster.checkpoint(ui_raster.context,stage))ui_raster.aborted=true;
 return !ui_raster.aborted;
}
#define R2_UI_OPTIONAL_CALL(call) do { if(!raster_boundary(R2_UI_RENDER_OPTIONAL))return false; call; } while(0)
#define R2_UI_PIXEL_VISIBLE(xp,yp) (!ui_raster.w||((xp)>=(int)ui_raster.x&&(yp)>=(int)ui_raster.y&&(xp)<(int)(ui_raster.x+ui_raster.w)&&(yp)<(int)(ui_raster.y+ui_raster.h)))
/* Intersect bulk primitives before their loops. A16-row loss job must not
 * spend critical CPU visiting the other144rows merely to reject each pixel. */
static void raster_bounds(int *x,int *y,int *w,int *h){
 int left=0,top=0,right_bound=R2_UI_WIDTH,bottom_bound=R2_UI_HEIGHT;
 if(ui_raster.active&&ui_raster.w){
  left=ui_raster.x;top=ui_raster.y;
  right_bound=left+ui_raster.w;bottom_bound=top+ui_raster.h;
 }
 int right=*x+*w,bottom=*y+*h;
 if(*x<left)*x=left;
 if(*y<top)*y=top;
 if(right>right_bound)right=right_bound;
 if(bottom>bottom_bound)bottom=bottom_bound;
 *w=right>*x?right-*x:0;*h=bottom>*y?bottom-*y:0;
}
#define R2_UI_RECT_BOUNDS(x,y,w,h) raster_bounds(&(x),&(y),&(w),&(h))
/* OPTIONAL keeps the worker UNSAFE/base9 until the next bounded pixel
 * checkpoint; no post-formatter promotion window is needed.
 * One guarded formatter entry avoids duplicating scheduling boundaries
 * at every optional label. No format conversion begins at critical priority. */
static bool __attribute__((format(printf,3,4))) raster_snprintf(
 char *out,size_t size,const char *format,...){
 if(ui_raster.active&&ui_raster.critical)return true;
 if(!raster_boundary(R2_UI_RENDER_OPTIONAL))return 0;
 va_list args;va_start(args,format);int result=vsnprintf(out,size,format,args);va_end(args);
 (void)result;return true;
}
#else
/* Legacy profiles have no partial scheduler or writable raster state. */
static bool raster_boundary(enum r2_ui_render_stage stage){(void)stage;return true;}
#define R2_UI_DRAW_CALL(call) do {call;}while(0)
#define R2_UI_PIXEL_CHECKPOINT() do {}while(0)
#define R2_UI_BOOL_ABORTED() false
#define R2_UI_PIXEL_VISIBLE(x,y) true
#define R2_UI_OPTIONAL_CALL(call) do {call;}while(0)
static bool __attribute__((format(printf,3,4))) raster_snprintf(char *out,size_t size,const char *format,...){
 va_list args;va_start(args,format);(void)vsnprintf(out,size,format,args);va_end(args);return true;
}
#endif
#define snprintf raster_snprintf
#ifdef R2_TZ_WIRE
static const char *tile_zone(const struct r2_ui_zone_cache *,const struct r2_ui_model *,
 uint64_t,int64_t,int64_t *);
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define UI_FRAME_BYTES R2_UI_FRAME_BYTES
#define UI_COLOR(name) R2_UI_COLOR_##name
#define UI_PIXEL_WRITE(fb,offset,color) ((fb)[offset]=(uint8_t)(color))
#include "../legacy_ui/draw.inc"
#pragma GCC diagnostic pop

static bool __attribute__((noinline)) watch_panel(uint8_t *fb,int y,int h){return round_panel(fb,3,y,74,h,C_PANEL,C_BEZEL);}
uint8_t r2_ui_menu_enabled(const struct r2_ui_model *m){
 uint8_t mask=(1u<<R2_MENU_POWER)|(1u<<R2_MENU_EXIT);
 if(m->profile_supported)mask|=1u<<R2_MENU_MODE;
 if(m->ble_rx_supported)mask|=1u<<R2_MENU_BLE;
#ifdef R2_LORA
 if(!m->lora.transmitter||!m->profile_rc)mask|=1u<<R2_MENU_LORA;
#endif
 return mask;
}
static bool menu_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){
 char text[24];
 R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(label(fb,28,4,"MENU",1,C_WHITE));
 R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%.5s %s",m->unit,m->profile_rc?"RC":"GM"));R2_UI_BOOL_CALL(row_label5(fb,20,text,C_DIM));
 R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%us",s->menu.seconds));R2_UI_BOOL_CALL(label(fb,63,20,text,1,C_DIM));
 R2_UI_BOOL_CALL(fill(fb,5,30,70,1,C_BEZEL));R2_UI_BOOL_CALL(fill(fb,5,30,14*s->menu.seconds,1,C_CYAN));
 unsigned row=0;uint8_t enabled=r2_ui_menu_enabled(m);
 for(unsigned i=0;i<R2_MENU_ITEMS;i++){
  if((i==R2_MENU_MODE&&!m->profile_supported)||(i==R2_MENU_BLE&&!m->ble_rx_supported))continue;
#ifndef R2_LORA
  if(i==R2_MENU_LORA)continue;
#endif
  const char *name="EXIT";
  switch(i){
   case R2_MENU_MODE:name=m->profile_rc?"GM MODE":"RC MODE";break;
   case R2_MENU_POWER:name="POWER OFF";break;
   case R2_MENU_BLE:name=m->ble_rx_requested?"BLE RX OFF":"BLE RX ON";break;
#ifdef R2_LORA
   case R2_MENU_LORA:
    R2_UI_BOOL_CALL(snprintf(text,sizeof text,"LoRa %s %s",m->lora.transmitter?"TX":"RX",!(enabled&(1u<<i))?"--":m->lora.requested?"OFF":"ON"));name=text;break;
#endif
  }
  int y=35+(int)row++*17;uint16_t ink=enabled&(1u<<i)?C_WHITE:C_DIM;
  if(i==s->menu.item){uint16_t selected=i==R2_MENU_POWER?C_AMBER:C_CYAN;R2_UI_BOOL_CALL(round_panel(fb,3,y,74,16,selected,selected));ink=C_BG;}
  R2_UI_BOOL_CALL(row_label7(fb,y+4,name,ink));
  if(i==s->menu.item&&s->menu.hold_percent){R2_UI_BOOL_CALL(fill(fb,7,y+13,66,1,C_BEZEL));R2_UI_BOOL_CALL(fill(fb,7,y+13,66*s->menu.hold_percent/100,1,C_BG));}
 }
 const char *result=NULL;uint16_t color=C_GREEN;
 if(m->profile_switching){result="SWITCHING";color=C_AMBER;}
 else if(now<s->menu_result_until){
  if(s->menu_result){result="CHANGE FAILED";color=C_AMBER;}
  else if(s->menu_result_item==R2_MENU_BLE)result=m->ble_rx_requested?"BLE RX ON":"BLE RX OFF";
  else if(s->menu_result_item==R2_MENU_MODE)result=m->profile_rc?"RC MODE":"GM MODE";
#ifdef R2_LORA
  else if(s->menu_result_item==R2_MENU_LORA){R2_UI_BOOL_CALL(snprintf(text,sizeof text,"LoRa %s %s",m->lora.transmitter?"TX":"RX",m->lora.requested?"ON":"OFF"));result=text;}
#endif
 }
#ifdef R2_LORA
 else if(m->lora.transmitter&&m->profile_rc){result="GM ONLY";color=C_AMBER;}
 else if(s->menu.item==R2_MENU_LORA){
  if(m->lora.fault){result="RADIO FAULT";color=C_AMBER;}
  else if(m->lora.requested&&!m->lora.active){result=m->lora.transmitter?"TX WAIT":"RX INHIBITED";color=C_AMBER;}
  else if(!m->lora.requested&&m->lora.active){result="STOPPING";color=C_AMBER;}
 }
#endif
 if(result)R2_UI_BOOL_CALL(label(fb,(80-((int)strlen(result)*6-1))/2,126,result,1,color));
 R2_UI_BOOL_CALL(label(fb,16,139,"TAP NEXT",1,C_DIM));R2_UI_BOOL_CALL(row_label7(fb,150,"HOLD SELECT",C_DIM));

 return true;
}

/* Public scalar helpers may run in main while UI is preempted. */
#undef snprintf
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
#ifdef R2_GM7_NATIVE
 if(m->gm7_owner)return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&!m->profile_rc&&!m->profile_switching&&m->local_hot_standby;
#endif
 return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&
   !m->profile_rc&&!m->profile_switching&&m->scope_active&&m->role==R2_UI_FOLLOWER&&
   m->local_hot_standby&&m->precision.authorized&&m->precision.map_valid;
}
static bool active_gm_current(const struct r2_ui_model *m,uint64_t now){
#ifdef R2_GM7_NATIVE
 if(m->gm7_owner)return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&!m->profile_rc&&!m->profile_switching&&(m->gm7_known&2);
#endif
 return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&
   !m->profile_rc&&!m->profile_switching&&m->scope_active&&m->role==R2_UI_GM;
}
void r2_ui_holds_update(struct r2_ui_holds *h,const struct r2_ui_model *m,uint64_t now){
 uint32_t stamp=now/1000<UINT32_MAX?(uint32_t)(now/1000)+1:UINT32_MAX;
 if(!local_pps_current(m,now))h->pps_since=0;else if(!h->pps_since||h->pps_since>stamp)h->pps_since=stamp;
 if(!hot_current(m,now))h->hot_since=0;else if(!h->hot_since||h->hot_since>stamp)h->hot_since=stamp;
 if(!active_gm_current(m,now))h->active_since=0;else if(!h->active_since||h->active_since>stamp)h->active_since=stamp;
#ifdef R2_GM7_NATIVE
 if(m->gm7_owner){
  uint32_t *holds[3]={&h->hot_since,&h->active_since,&h->pps_since};
  uint32_t durations[3]={m->gm7_hot_ms,m->gm7_active_ms,m->gm7_pps_ms};
  for(unsigned i=0;i<3;i++)if(*holds[i]&&(m->gm7_known&(1u<<i))&&durations[i]!=UINT32_MAX&&m->gm7_at_ms>=durations[i]){
   uint64_t start=(m->gm7_at_ms-durations[i])/1000+1;*holds[i]=start>UINT32_MAX?UINT32_MAX:(uint32_t)start;
  }
 }
#endif
}
bool r2_ui_witness_page(const struct r2_ui_model *m,unsigned page){
 (void)m;return page==R2_UI_SHADOW_PAGE||page==R2_UI_PRECISION_PAGE;
}
static void hold_text(char out[24],const char *name,uint32_t since,uint64_t now){
 uint64_t seconds=since&&now/1000>=since-1?now/1000-(since-1):0;
 if(seconds/60>9999)snprintf(out,24,"%s >9999m",name);
 else snprintf(out,24,"%s %02u:%02u",name,(unsigned)(seconds/60),(unsigned)(seconds%60));
}
static bool active_hold_label(uint8_t *fb,int y,const char *text){
 if(strlen(text)<=12){R2_UI_BOOL_CALL(row_label4(fb,y,text,C_GREEN));return true;}
 /* Keep four-digit minutes and seconds on one line. Reuse the 5x7 font,
  * trimming only empty glyph columns for unusually long durations. */
 int x=4;
 for(;*text;++text){
  if(*text==' '){x+=3;continue;}
  const uint8_t *g=glyph(*text);int first=0,last=4;
  while(first<4&&!g[first])++first;
  while(last>first&&!g[last])--last;
  for(int col=first;col<=last;++col)for(int row=0;row<7;++row)
   if(g[col]&(1u<<row))R2_UI_BOOL_CALL(pixel(fb,x+col-first,y+row,C_GREEN));
  x+=last-first+2;
 }

 return true;
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
 if(m->gm_cold)return "COLD STANDBY";
 return m->role==R2_UI_GM?"ACTIVE GM":"HOT WAIT";
}
static const char *hot_reason(const struct r2_ui_model *m,bool fresh,bool hot){
#ifdef R2_GM7_NATIVE
 if(m->gm7_owner&&!m->profile_rc&&(m->gm7_known&R2_UI_GM7_OWNER_FAULT))return "OWNER FAULT";
#endif
 if(!fresh)return "STATE STALE";
 if(m->profile_rc)return "RC MODE";
 if(hot)return "QUALIFIED";
 if(m->role==R2_UI_GM)return m->hot_ready?"PEER HOT":"NO HOT PEER";
 if(!m->gm_capable)return "CLIENT ONLY";
 if(m->gm_cold)return m->scope_active&&m->precision.authorized&&m->precision.map_valid?"TIMING SYNC":"SYNC WAIT";
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
#ifdef R2_LORA
static bool lora_watch_selected(const struct r2_ui_model *m){return m->lora.choice==RC8_SELECT_LORA||m->lora.display_held||m->lora.display_acquiring;}
static unsigned lora_sync_phase(const struct r2_ui_model *m,uint64_t now){
 const struct r2_lora_view *v=&m->lora;
 if(v->fault)return UI_LORA_SYNC_FAULT;
 if(!v->requested)return UI_LORA_SYNC_OFF;
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!v->active)return UI_LORA_SYNC_WAIT;
 if(v->metadata_cached&&v->observer_available){
  if((uint64_t)v->rx_age_ms+now-m->sampled_ms>=35000)return UI_LORA_SYNC_GAP;
  if(!v->context_known)return UI_LORA_SYNC_STALE;
 }
 if(!v->context_known)return UI_LORA_SYNC_META;
 if(!v->link_samples)return UI_LORA_SYNC_PAIR;
 uint64_t age=v->rx_age_ms;
 if(age!=UINT32_MAX)age+=now-m->sampled_ms;
 if(age==UINT32_MAX||age>35000)return UI_LORA_SYNC_GAP;
 return UI_LORA_SYNC_TRAIN;
}
static const char *lora_sync_text(unsigned phase){
 switch(phase){
 case UI_LORA_SYNC_META:return "WAIT META";case UI_LORA_SYNC_PAIR:return "WAIT PAIR";
 case UI_LORA_SYNC_TRAIN:return "BUILDING FIT";case UI_LORA_SYNC_GAP:return "PAIR GAP";
 case UI_LORA_SYNC_OFF:return "LORA OFF";case UI_LORA_SYNC_FAULT:return "RADIO FAULT";
 case UI_LORA_SYNC_STALE:return "META STALE";
 default:return "LISTENING";
 }
}
static unsigned lora_fit_seconds(const struct r2_lora_view *v){
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
 if(!v->context_known&&!v->metadata_cached)return 0;
 return v->metadata_cached?(v->fit_span_ms>300000?300:v->fit_span_ms/1000):v->fit_span_ms>180000?180:v->fit_span_ms/1000;
#else
 (void)v;return 0;
#endif
}
static void lora_sync_progress(char out[24],const struct r2_lora_view *v){
 snprintf(out,24,"%u/180s",lora_fit_seconds(v));
}
static bool lora_at(const struct r2_ui_model *m,uint64_t now,uint64_t until,int64_t *utc){
 const struct r2_lora_view *v=&m->lora;
 uint64_t at=v->sampled_us/1000,fraction=v->sampled_us%1000;
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||m->profile_switching||
    !v->requested||!v->active||v->fault||!v->utc_us||now>UINT64_MAX/1000||
    now<at||v->sampled_us>=until||now>=until/1000||v->utc_us<fraction||
    (now-at)*1000>(uint64_t)INT64_MAX||v->utc_us-fraction>(uint64_t)INT64_MAX-(now-at)*1000)return false;
 if(utc)*utc=(int64_t)(v->utc_us-fraction+(now-at)*1000);
 return true;
}
static bool lora_input_current(const struct r2_ui_model *m,uint64_t now){
 return m->lora.state==RC8_AVAILABLE&&lora_at(m,now,m->lora.until_us,NULL);
}
bool r2_ui_lora_primary(const struct r2_ui_model *m,uint64_t now){
 const struct r2_lora_view *v=&m->lora;
 bool explicit_lora=m->ble_rx_supported&&!m->ble_rx_requested&&v->requested&&!v->transmitter;
 if(!lora_watch_selected(m))return explicit_lora;
 if(explicit_lora)return true;
 bool ble_present=now>=m->sampled_ms&&now-m->sampled_ms<=2500&&m->platform_ready&&
  !m->profile_switching&&m->ble_active&&(!m->ble_rx_supported||m->ble_rx_requested)&&
  (r2_ui_ble_current(m,now)||m->sync_attempt);
 /* A retained estimate is still useful on the independent RF page, but may
  * not replace a live BLE acquisition screen. Existing qualified fallback
  * and its owner's return dwell are unchanged. */
 bool qualified=v->time_valid&&v->quality_known&&!v->display_held&&
  !v->display_acquiring&&lora_input_current(m,now);
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
 qualified=qualified&&v->context_known&&v->rx_age_ms<35000&&
  now-m->sampled_ms<35000-v->rx_age_ms;
#endif
 return !ble_present||qualified;
}
static bool lora_current(const struct r2_ui_model *m,uint64_t now){
 const struct r2_lora_view *v=&m->lora;
 if(!lora_watch_selected(m)||v->transmitter)return false;
 if(v->choice==RC8_SELECT_LORA&&v->state==RC8_AVAILABLE)return lora_at(m,now,v->until_us,NULL);
 #ifdef R2_LORA_PASSIVE_SLOW
 /* Explicit same-boot display cache; native freshness remains expired. */
 return v->choice==RC8_SELECT_NONE&&v->state==RC8_EXPIRED&&(v->display_held||v->display_acquiring)&&
  !v->time_valid&&!v->quality_known&&v->display_until_us==UINT64_MAX&&lora_at(m,now,v->display_until_us,NULL);
#else
 return v->choice==RC8_SELECT_NONE&&v->state==RC8_EXPIRED&&v->display_held&&
  v->until_us<=UINT64_MAX-(R2_LORA_DISPLAY_HOLD_US-RC8_HOLD_US)&&
  v->display_until_us<=v->until_us+R2_LORA_DISPLAY_HOLD_US-RC8_HOLD_US&&lora_at(m,now,v->display_until_us,NULL);
#endif
}
static bool lora_observer_current(const struct r2_ui_model *m,uint64_t now){
 return !m->lora.transmitter&&m->lora.observer_available&&lora_at(m,now,m->lora.display_until_us,NULL);
}
static uint32_t lora_metric_ns(const struct r2_lora_view *v){return v->metadata_cached?v->prediction_rms_ns:v->link_rms_ns;}
#ifdef R2_LORA_PASSIVE_SLOW
/* Age of the last accepted precision pair, never the frozen fit reference or
 * DISC/META traffic. Keep the UTC/TZ cache while expiring dial presentation. */
static bool lora_precision_recent(const struct r2_ui_model *m,uint64_t now,uint32_t limit_ms){
 const struct r2_lora_view *v=&m->lora;uint64_t at=v->sampled_us/1000;
 return now>=at&&v->rx_age_ms<=limit_ms&&now-at<=limit_ms-v->rx_age_ms;
}
#endif
static bool lora_face_current(const struct r2_ui_model *m,uint64_t now){
 return lora_current(m,now)
#ifdef R2_LORA_PASSIVE_SLOW
  &&(m->lora.metadata_cached||lora_precision_recent(m,now,300000u))
#endif
  ;
}
static bool lora_dial_current(const struct r2_ui_model *m,uint64_t now){
 return lora_face_current(m,now)&&(!m->lora.metadata_cached||
  (m->lora.context_known&&m->lora.quality_known&&!m->lora.display_acquiring&&(uint64_t)m->lora.rx_age_ms+now-m->sampled_ms<35000));
}
static bool lora_rms_current(const struct r2_ui_model *m,uint64_t now){
 if(m->lora.metadata_cached)return m->lora.prediction_n&&m->lora.prediction_rms_ns!=UINT32_MAX&&
  (lora_observer_current(m,now)||lora_input_current(m,now)||lora_current(m,now));
 return !m->lora.transmitter&&lora_input_current(m,now)&&m->lora.link_samples>=4&&
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
  m->lora.quality_known&&
#endif
  m->lora.fit_age_ms<R2_LORA_FIT_HOLD_MS&&now-m->sampled_ms<R2_LORA_FIT_HOLD_MS-m->lora.fit_age_ms;
}
bool r2_ui_lora_tof_ns(const struct r2_ui_model *m,uint64_t now,uint32_t *ns){
 const struct r2_lora_view *v=&m->lora;
 if(!ns||!lora_input_current(m,now)||v->transmitter||v->choice!=RC8_SELECT_LORA||
    v->rtt_base_count!=2||!v->rtt_life_8s||v->rtt_value_ns>125000000u||
    (!v->rtt_positive&&v->rtt_value_ns<R2_LORA_TOF_FLOOR_NS)||
    (now-v->sampled_us/1000)*1000>=(uint64_t)v->rtt_life_8s*8000000)return false;
 *ns=v->rtt_positive?v->rtt_value_ns:0;return true;
}
#if !defined(R2_LORA_PASSIVE_FAST)&&!defined(R2_LORA_PASSIVE_SLOW)
static void lora_tof_text(char out[24],const char *name,char prefix,uint64_t magnitude){
 const char *unit=magnitude<1000?"ns":magnitude<1000000?"us":"ms";
 uint32_t scale=magnitude<1000?1:magnitude<1000000?1000:1000000;
 /* At most12 glyphs, including sign and units, on the80px display. */
 if(scale==1)snprintf(out,24,"%s %c%uns",name,prefix,(unsigned)magnitude);
 else if(magnitude%scale==0)snprintf(out,24,"%s %c%u%s",name,prefix,(unsigned)(magnitude/scale),unit);
 else if(magnitude/scale<100)snprintf(out,24,"%s %c%u.%02u%s",name,prefix,(unsigned)(magnitude/scale),(unsigned)(magnitude%scale*100/scale),unit);
 else snprintf(out,24,"%s %c%u.%u%s",name,prefix,(unsigned)(magnitude/scale),(unsigned)(magnitude%scale*10/scale),unit);
}
#endif
static void lora_header_name(char out[9],const char *name){
 size_t n=0;while(n<8&&name[n])++n;
 /* Truncate received UTF-8 only between code points. */
 while(n&&((unsigned char)name[n]&0xc0)==0x80)--n;
 memcpy(out,name,n);out[n]=0;
}

#endif
static bool ble_display_time(const struct r2_ui_model *m,uint64_t now,int64_t *utc,bool *coarse)
{
 *coarse=false;
 if(now<m->sampled_ms||now-m->sampled_ms>2500)return false;
 if(m->clock.valid&&m->clock.utc_us>=0&&(!m->ble_time_selected||r2_ui_ble_current(m,now))){
  uint64_t delta=m->ble_time_selected?(now-m->sampled_ms)*1000:0;
  if(delta>(uint64_t)(INT64_MAX-m->clock.utc_us))return false;
  *utc=m->clock.utc_us+(int64_t)delta;return true;
 }
 if(!m->coarse_valid||now>UINT64_MAX/1000||now*1000<m->coarse_at_us||
    m->coarse_utc_us>INT64_MAX||now*1000-m->coarse_at_us>(uint64_t)INT64_MAX-m->coarse_utc_us)return false;
 *utc=(int64_t)(m->coarse_utc_us+now*1000-m->coarse_at_us);*coarse=true;return true;
}
static bool ble_analog(const struct r2_ui_model *m,uint64_t now)
{
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||!m->clock.valid||m->clock.utc_us<0||!m->scope_active)return false;
 if(m->role==R2_UI_GM)return m->clock.source_available||m->gm_source_fresh;
 if(m->boundary_clock)return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&r2_ui_ble_current(m,now)&&m->precision.map_valid;
 return m->role==R2_UI_FOLLOWER&&m->authenticated_gm&&r2_ui_ble_current(m,now)&&m->follower_pps_valid&&m->precision.map_valid&&m->sync_age_ms<=2500;
}

bool r2_ui_analog(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now))return lora_dial_current(m,now);
#endif
 return ble_analog(m,now);
}
static bool ble_face_locked(const struct r2_ui_model *m,uint64_t now)
{
 if(m->role==R2_UI_GM||m->boundary_clock)return ble_analog(m,now);
 if(now<m->sampled_ms||now-m->sampled_ms>2500||!m->platform_ready||
    now>UINT64_MAX/1000||now*1000>=m->face_lock_until_us||m->face_rms_ns>200)return false;
 int64_t utc;bool coarse;
 return ble_display_time(m,now,&utc,&coarse);
}

bool r2_ui_face_locked(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 /* Dial permission is presentation, not BLE/PLL lock or source authority. */
 if(r2_ui_lora_primary(m,now))return lora_dial_current(m,now);
#endif
 return ble_face_locked(m,now);
}
static const char *ble_servo_state(const struct r2_ui_model *m,uint64_t now)
{
 if(m->boundary_clock)return ble_analog(m,now)?(m->ble_local_holdover?"LOCAL HOLD":m->rc_source_held?"HELD SOURCE":"RECEIVING"):"ACQUIRING";
 if(ble_face_locked(m,now))return "LOCKED";
 if(now<m->sampled_ms||now-m->sampled_ms>2500||(m->ble_time_selected&&!r2_ui_ble_current(m,now))||!m->scope_active||
    m->role!=R2_UI_FOLLOWER||!m->precision.authorized)return "UNAVAILABLE";
 if(m->precision.locked&&m->precision.map_valid)return "LOCKED";
 /* Initial authorized acquisition is distinct from a previously accepted stale map. */
 if(!m->precision.map_valid&&m->precision.correction_age_valid&&m->precision.correction_age_ms>=1500)return "UNAVAILABLE";
 return "ACQUIRING";
}

const char *r2_ui_servo_state(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now))return lora_current(m,now)?(!lora_face_current(m,now)||m->lora.display_acquiring?"ACQUIRING":m->lora.display_held?"HOLD":"RECEIVING"):"UNAVAILABLE";
#endif
 return ble_servo_state(m,now);
}
bool r2_ui_saver_allowed(const struct r2_ui_model *m,uint64_t now)
{
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now))return lora_current(m,now)&&(!m->lora.metadata_cached||lora_dial_current(m,now))
#ifdef R2_LORA_PASSIVE_SLOW
  &&lora_precision_recent(m,now,120000u)
#endif
  ;
#endif
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
 if(r2_ui_lora_primary(m,now)){
  if(!lora_current(m,now))return false;
  uint64_t value=m->lora.utc_us-m->lora.sampled_us%1000+(now-m->lora.sampled_us/1000)*1000;
  if(value>INT64_MAX)return false;
  *utc=(int64_t)value;*coarse=true;return true;
 }
#endif
 return ble_display_time(m,now,utc,coarse);
}

#ifdef R2_TZ_WIRE
/* Boot-local presentation only. Unknown updates carry no replacement zone. */
static struct r2_tz display_tz_input(const struct r2_ui_model *m,uint64_t now,uint8_t *source){
 *source=0;
 if(now<m->sampled_ms||now-m->sampled_ms>2500||now>UINT64_MAX/1000)return r2_tz_unknown();
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now)){
  if((now*1000<m->lora.tz_until_us||m->lora.metadata_cached)&&r2_tz_valid(m->lora.tz)){
   *source=R2_UI_TZ_LORA|(m->lora.tz_observed?R2_UI_TZ_OBSERVED:0)|(now*1000>=m->lora.tz_until_us?R2_UI_TZ_CACHED:0);return m->lora.tz;
  }
  return r2_tz_unknown();
 }
#endif
 if(now*1000<m->tz_until_us&&r2_tz_valid(m->tz)){
  *source=m->role==R2_UI_GM&&m->gm_source_fresh?R2_UI_TZ_LOCAL:R2_UI_TZ_NETWORK;return m->tz;
 }
 return r2_tz_unknown();
}
void r2_ui_display_tz_update(struct r2_ui_model *m,struct r2_tz previous,uint8_t source,bool fresh){
 m->display_tz=r2_tz_valid(previous)?previous:r2_tz_unknown();m->display_tz_source=m->display_tz.id?source:0;
 /* Boot lifetime RAM only. Unknown/expired metadata, unavailable UTC and
  * mode/stop transitions cannot erase a learned geographic preference. */
 if(!fresh)return;
 uint8_t incoming;struct r2_tz value=display_tz_input(m,m->sampled_ms,&incoming);
 if(r2_tz_valid(value)){m->display_tz=value;m->display_tz_source=incoming;}
}
struct r2_tz r2_ui_display_zone(const struct r2_ui_model *m,uint64_t now,int64_t utc,uint8_t *flags){
 *flags=0;
 if(now<m->sampled_ms||now-m->sampled_ms>2500)return r2_tz_unknown();
 struct r2_tz value=display_tz_input(m,now,flags);
 if(!r2_tz_valid(value)&&r2_tz_valid(m->display_tz)){
  value=m->display_tz;*flags=m->display_tz_source|R2_UI_TZ_CACHED;
 }
 if(!r2_tz_valid(value))return r2_tz_unknown();
#ifdef R2_TZ_AUTO
 if(!strcmp(r2_tz_db_name(value.id),"Unknown"))*flags|=R2_UI_TZ_FOREIGN;
 else if(R2_TZ_MODE!=R2_TZ_MANUAL_OFFSET){
  *flags|=R2_UI_TZ_CALENDAR;
  /* Keep identity for diagnostics when its calendar cannot safely convert. */
  if(utc<0||!r2_tz_db_offset(value.id,(uint64_t)utc,&value.offset_min))return value;
 }
#endif
 int64_t local;
 if(r2_tz_local_us(value,utc,&local))*flags|=R2_UI_TZ_LOCAL_VALID;
 return value;
}
/* The model, USB and every timing owner retain UTC. */
const char *r2_ui_clock_zone(const struct r2_ui_model *m,uint64_t now,int64_t utc,int64_t *shown){
 *shown=utc;uint8_t flags;struct r2_tz tz=r2_ui_display_zone(m,now,utc,&flags);
 if(!(flags&R2_UI_TZ_LOCAL_VALID)||!r2_tz_local_us(tz,utc,shown))return "UTC";
 if(tz.id==1)return "UTC";
 if(tz.id==2&&tz.offset_min==-300)return "CDT";
 if(tz.id==2&&tz.offset_min==-360)return "CST";
 return "LOC";
}
#endif
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
 if(r2_ui_lora_primary(m,now))return false;
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
#define snprintf raster_snprintf

static bool __attribute__((noinline)) render_snapshot(ui_snapshot_t *s,
 const struct r2_ui_model *m,const struct r2_ui_status *status,uint64_t now,
 bool critical,const struct r2_ui_zone_cache *zone,struct r2_ui_tile_view *view,
 const char **label_zone,unsigned *facts){
#ifndef R2_TZ_WIRE
 (void)zone;
#endif
 bool model_fresh=now>=m->sampled_ms&&now-m->sampled_ms<=2500;
 bool fresh=model_fresh;
 if(m->ble_time_selected&&!r2_ui_ble_current(m,now))fresh=false;
 *s=(ui_snapshot_t){0};
 s->unit_name=m->unit;s->page=status->page;s->rf_help=status->help;
 bool native_precision=status->page==2&&!m->profile_rc;
 bool face_locked=native_precision?ble_face_locked(m,now):r2_ui_face_locked(m,now);
 int64_t grace_utc=0;
 bool sync_grace=r2_ui_sync_display_time(&status->sync_display,m,now,&grace_utc);
 bool face_visible=face_locked||sync_grace;
 bool follower_face=face_locked&&m->role!=R2_UI_GM&&!m->boundary_clock;
 s->native_boundary_clock=m->boundary_clock;s->native_boundary_held=m->rc_source_held||sync_grace;
 s->native_profile_supported=m->profile_supported;s->native_profile_rc=m->profile_rc;
 s->native_rc_serving_clients=m->profile_rc&&!m->profile_switching&&fresh&&
     m->ble_active&&r2_ui_ble_current(m,now)&&m->rc_serving_clients;
 s->native_time_relay_hops=m->time_relay_hops;
 bool metric_current=!m->boundary_clock||r2_ui_ble_current(m,now);
 s->native_pll_locked=metric_current&&fresh&&m->scope_active&&m->role==R2_UI_FOLLOWER&&m->precision.authorized&&m->precision.map_valid&&m->precision.locked;
 s->native_rms_valid=metric_current&&(follower_face||(fresh&&m->scope_active&&m->precision.rms_valid));
 if(!critical)R2_UI_OPTIONAL_CALL(r2_precision_format(s->native_rms,sizeof(s->native_rms),follower_face?m->face_rms_ns:m->precision.rms_ns,s->native_rms_valid));
 if(s->native_rms_valid&&m->precision.rms_ns>9999999)R2_UI_BOOL_CALL(snprintf(s->native_rms,sizeof(s->native_rms),">9999 us"));
 s->die_temperature_valid=model_fresh&&m->temperature_valid;
 s->die_temperature_c=m->temperature_qc/4;
 s->gnss_native_health=true;s->native_served_time=face_visible||(fresh&&m->clock.valid);
 s->native_analog_eligible=face_visible;
 s->native_ble_time=(face_visible&&m->role!=R2_UI_GM)||(fresh&&m->ble_time_selected);
#ifdef R2_LORA
 if(!native_precision&&r2_ui_lora_primary(m,now)){
  s->native_pll_locked=s->native_rms_valid=s->native_ble_time=false;
  s->native_rms_valid=lora_rms_current(m,now);
  if(!critical)R2_UI_OPTIONAL_CALL(r2_precision_format(s->native_rms,sizeof s->native_rms,m->lora.link_rms_ns,s->native_rms_valid));
  s->native_served_time=face_locked;
 }
#endif
 s->native_hot_standby=fresh&&m->scope_active&&m->role==R2_UI_FOLLOWER&&m->local_hot_standby&&m->precision.authorized&&m->precision.map_valid;
#ifdef R2_LORA
 if(!native_precision&&r2_ui_lora_primary(m,now))s->native_hot_standby=false;
#endif
 int64_t display_utc=0;bool coarse=false;
 s->utc_valid=r2_ui_display_time(m,now,&display_utc,&coarse);
 if(sync_grace){s->utc_valid=true;display_utc=grace_utc;coarse=true;}
 if(view){view->utc_us=display_utc;view->valid=s->utc_valid;view->coarse=coarse;}
 const char *clock_zone=NULL;
#ifdef R2_TZ_WIRE
 if(status->page==0||status->screensaver){
  int64_t shown=display_utc;clock_zone=s->utc_valid?(critical?tile_zone(zone,m,now,display_utc,&shown):r2_ui_clock_zone(m,now,display_utc,&shown)):"UTC";
  /* Normalize signed local dates before converting to the dial's day seconds-> */
  int64_t day=INT64_C(86400000000),tod=shown%day;
  if(tod<0)tod+=day;
  if(s->utc_valid)display_utc=tod;
 }
#endif
 s->utc_second=s->utc_valid?(uint32_t)(display_utc/1000000):0;
 s->subsecond_quarter=s->utc_valid?(display_utc%1000000)/250000:0;
 s->state=!s->utc_valid?CLOCK_NO_TIME:s->native_analog_eligible?CLOCK_LOCKED:CLOCK_ACQUIRING;
 if(m->ble_local_holdover&&s->utc_valid)s->state=CLOCK_HOLDOVER;
 bool settling=now>=m->sampled_ms&&now-m->sampled_ms<=2500&&(m->sync_attempt||(m->boundary_clock&&m->ble_active&&!r2_ui_ble_current(m,now)))&&!s->native_analog_eligible;
 if(settling)s->state=CLOCK_ACQUIRING;
 s->native_reason=settling?UI_NATIVE_NONE:coarse&&!face_locked?UI_NATIVE_SOURCE_STALE:!fresh?UI_NATIVE_SOURCE_STALE:!s->utc_valid?UI_NATIVE_NO_TIME:
     m->role==R2_UI_FOLLOWER?UI_NATIVE_PPS_UNASSOCIATED:!m->scope_active?UI_NATIVE_NO_GM:UI_NATIVE_LOCAL_COARSE;
#ifdef R2_LORA_PASSIVE_SLOW
 if(r2_ui_lora_primary(m,now)&&(m->lora.display_held||m->lora.display_acquiring)&&lora_current(m,now)){
  s->state=lora_face_current(m,now)?CLOCK_HOLDOVER:CLOCK_ACQUIRING;
  s->native_reason=lora_face_current(m,now)?UI_NATIVE_SOURCE_STALE:UI_NATIVE_NONE;
 }
#endif
 s->boot_elapsed_seconds=now/1000;s->pps_seen=fresh&&m->clock.pps_present;
 s->pps_age_ms=age_ms(m->clock.pps_age_us);s->rmc_age_ms=age_ms(m->clock.gps_age_us);
 s->gnss_bytes_seen=m->clock.rx_age_us>=0;s->gnss_rx_age_ms=age_ms(m->clock.rx_age_us);
 s->gnss_sv_seen=m->clock.sats_used;s->gnss_signal_age_s=age_ms(m->clock.gga_age_us)/1000;
 /* The lamp reports only a recent local GNSS PPS edge in every clock role.
  * BLE UTC, map/PLL lock and HOT status cannot synthesize this indication. */
 s->pulse_indicator=r2_ui_local_pulse(m,now)?UI_PULSE_PPS:UI_PULSE_OFF;
 s->ble_mode_known=true;s->ble_mode_enabled=m->ble_active;s->ble_tx_active=m->ble_active;
 s->rf_ble_tx_mode=m->ble_active;
 s->ui_role.grandmaster=fresh&&m->scope_active&&m->role==R2_UI_GM;
 s->native_rx_known=model_fresh&&m->ble_active&&m->ble_reception_valid;
 if(s->native_rx_known&&m->ble_rx_age_ms<1500&&now-m->sampled_ms<1500-m->ble_rx_age_ms)
  s->native_rx_bars=m->ble_delivery_per_mille>=900?4:m->ble_delivery_per_mille>=750?3:m->ble_delivery_per_mille>=500?2:m->ble_delivery_per_mille?1:0;
 if(settling||(face_visible&&m->role!=R2_UI_GM)||(fresh&&m->role==R2_UI_FOLLOWER&&m->scope_active))
  R2_UI_BOOL_CALL(snprintf(s->ui_role.header,sizeof(s->ui_role.header),"%s>%.8s",m->unit,m->relay_selected?m->upstream_name:m->root_name));
 else R2_UI_BOOL_CALL(snprintf(s->ui_role.header,sizeof(s->ui_role.header),"%s",m->unit));
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now)){
  if(!critical){
  char provider[9];R2_UI_OPTIONAL_CALL(lora_header_name(provider,m->upstream_name[0]?m->upstream_name:"--"));
  R2_UI_BOOL_CALL(snprintf(s->ui_role.header,sizeof s->ui_role.header,"%s>%s",m->unit,provider));
  }
  s->ui_role.grandmaster=false;
  s->native_lora_source=true;
  s->native_lora_acquiring=m->lora.display_acquiring||
   (!m->lora.display_held&&!lora_input_current(m,now))||!lora_face_current(m,now);
  s->native_lora_phase=(uint8_t)lora_sync_phase(m,now);s->native_lora_samples=m->lora.context_known||m->lora.metadata_cached?m->lora.link_samples:0;
  s->native_lora_fit_s=(uint16_t)lora_fit_seconds(&m->lora);
  s->native_lora_observed=m->lora.metadata_cached&&lora_observer_current(m,now);
  s->native_lora_pair_s=m->lora.rx_age_ms/1000;s->native_lora_error_s=m->lora.prediction_age_ms==UINT32_MAX?UINT32_MAX:m->lora.prediction_age_ms/1000;
  if(s->native_lora_acquiring||(s->native_lora_observed&&(!m->lora.quality_known||!m->lora.context_known||m->lora.rx_age_ms>=35000)))s->native_analog_eligible=false;
  s->native_rms_valid=lora_rms_current(m,now);
  if(!critical)R2_UI_OPTIONAL_CALL(r2_precision_format(s->native_rms,sizeof s->native_rms,lora_metric_ns(&m->lora),s->native_rms_valid));
  if(s->native_lora_observed&&!m->lora.prediction_n)R2_UI_BOOL_CALL(snprintf(s->native_rms,sizeof s->native_rms,"NEXT PAIR"));
  bool signal=model_fresh&&m->lora.requested&&m->lora.active&&!m->lora.fault&&m->lora.rssi_valid&&
   m->lora.rx_age_ms<R2_LORA_SIGNAL_HOLD_MS&&now-m->sampled_ms<R2_LORA_SIGNAL_HOLD_MS-m->lora.rx_age_ms;
  s->native_rx_known=signal;s->native_rx_bars=signal?(m->lora.rssi_half_dbm>=-160?4:m->lora.rssi_half_dbm>=-190?3:m->lora.rssi_half_dbm>=-220?2:1):0;
#ifdef R2_LORA_PASSIVE_SLOW
  if(!m->lora.metadata_cached&&s->native_lora_acquiring){
   if(s->native_lora_phase==UI_LORA_SYNC_TRAIN||s->native_lora_phase==UI_LORA_SYNC_GAP)
    R2_UI_BOOL_CALL(snprintf(s->native_rms,sizeof s->native_rms,"%u/180s",s->native_lora_fit_s));
   else R2_UI_BOOL_CALL(snprintf(s->native_rms,sizeof s->native_rms,"%s",lora_sync_text(s->native_lora_phase)));
  }else if(!m->lora.metadata_cached&&m->lora.display_held&&lora_current(m,now)){
  uint64_t age=(uint64_t)m->lora.age_ms+(now-m->sampled_ms);
  unsigned seconds=age/1000>UINT32_MAX?UINT32_MAX:(unsigned)(age/1000);
  R2_UI_BOOL_CALL(snprintf(s->native_rms,sizeof s->native_rms,"%us",seconds));
  }
#endif
 }
#endif
 if(facts)*facts=(fresh?1u:0u)|(native_precision?2u:0u)|(face_locked?4u:0u);
 *label_zone=clock_zone;
 if(view){
  view->pulse=s->pulse_indicator==UI_PULSE_PPS;view->edge=m->clock.pps_edges;
  view->analog=s->native_analog_eligible&&s->native_served_time&&s->utc_valid;
 }
 if(critical)return true;
 /* DC displays the standard USB client sampling rate. The existing dial
  * renderer colors the entire badge from the live die temperature. */
#ifdef R2_DC_PROFILE
 s->active_ble_slots=32;
#else
 /* Beat value comes from timing owner, not display cadence or control traffic. */
 s->active_ble_slots=fresh?m->precision_beat_hz:0;
#endif
 /* RF diagnostics retain each input's own UTC even when another input is
  * selected for the watch. Never substitute selected LoRa/local pixels-> */
 bool ble_rf=model_fresh&&m->scope_active&&m->clock.valid&&m->clock.utc_us>=0&&
  (r2_ui_ble_current(m,now)||active_gm_current(m,now));
 uint64_t ble_utc=ble_rf?(uint64_t)m->clock.utc_us+(now-m->sampled_ms)*1000:0;
 s->rf_sources.ble=(ui_rf_source_t){.available=ble_rf,.fresh=ble_rf,.utc=true,.second=(uint32_t)(ble_utc/1000000)};
#ifdef R2_LORA
 s->lora_enabled=model_fresh&&m->lora.requested;s->lora_active=model_fresh&&m->lora.active&&!m->lora.fault;
 s->lora_fault=model_fresh&&m->lora.fault;
 s->lora_tx_mode=s->lora_enabled&&m->lora.transmitter;
 s->lora_locked=s->lora_active&&m->lora.time_valid&&m->lora.state==RC8_AVAILABLE;
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
 s->lora_locked=s->lora_locked&&m->lora.age_ms<LP_CADENCE_BOUND_US/1000&&
  now-m->sampled_ms<LP_CADENCE_BOUND_US/1000-m->lora.age_ms;
#endif
 int64_t lora_utc=0;bool lora_fresh=lora_input_current(m,now)&&(!m->lora.metadata_cached||m->lora.rx_age_ms<35000);
 bool lora_observed=lora_observer_current(m,now);
 bool observer_fresh=m->lora.metadata_cached&&lora_observed&&m->lora.context_known&&
  m->lora.rx_age_ms<35000&&now-m->sampled_ms<35000-m->lora.rx_age_ms;
 lora_fresh=lora_fresh||observer_fresh;
 s->native_rf_lora_estimated=observer_fresh&&!(m->lora.time_valid&&m->lora.quality_known&&m->lora.state==RC8_AVAILABLE);
 bool lora_time=!m->lora.transmitter&&(lora_fresh||lora_current(m,now)||lora_observed);
 if(lora_time)lora_at(m,now,lora_observed?m->lora.display_until_us:(m->lora.display_held||m->lora.display_acquiring)?m->lora.display_until_us:m->lora.until_us,&lora_utc);
 s->rf_sources.lora=(ui_rf_source_t){.available=lora_time,.fresh=lora_fresh,.utc=true,.second=(uint32_t)(lora_utc/1000000)};
 const struct r2_ui_rf_comparison *comparison=&m->rf_comparison;
 bool compare=model_fresh&&ble_rf&&lora_time&&lora_fresh&&now<=UINT64_MAX/1000&&
  now*1000>=comparison->sampled_us&&now*1000<comparison->until_us;
 s->rf_sources.delta_valid=compare&&(comparison->flags&R2_UI_RF_DELTA);
 s->rf_sources.drift_valid=s->rf_sources.delta_valid&&(comparison->flags&R2_UI_RF_RATE);
 s->rf_sources.delta_ns=comparison->delta_ns;s->rf_sources.drift_ppb=comparison->drift_ppb;
 s->native_rf_comparison_estimated=s->rf_sources.delta_valid&&(comparison->flags&R2_UI_RF_ESTIMATE);
 if(s->lora_active&&s->lora_enabled&&m->lora.activity_age_ms<250&&now>=m->sampled_ms&&now-m->sampled_ms<250-m->lora.activity_age_ms&&!(now/125%2)
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
    &&(m->lora.activity_kind==1||m->lora.activity_kind==3)
#endif
   ){
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
  unsigned kind=m->lora.activity_kind==1?UI_RF_TIME:UI_RF_OTHER;
#else
  unsigned kind=m->lora.activity_kind==1?UI_RF_TIME:UI_RF_PPS;
#endif
  s->rf_lamps[UI_RF_LORA][kind]=m->lora.transmitter?UI_RF_TX:UI_RF_RX;
 }
#endif
 /* Transport activity remains visible while time/map qualification is absent.
  * A stalled model cannot keep lamps on; no time authority is inferred. */
 r2_ui_rf_lamps(s->rf_lamps[UI_RF_BLE],m,now);

 return true;
}

static bool render_full(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *status,uint64_t now)
{
 ui_snapshot_t s;unsigned facts=0;const char *clock_zone=NULL;
 R2_UI_BOOL_CALL(render_snapshot(&s,m,status,now,false,NULL,NULL,&clock_zone,&facts));

#ifdef R2_UI_PARTIAL_JOBS
 ui_raster.drawing=true;
#endif
 if(!raster_boundary(R2_UI_RENDER_PIXELS))return false;
 bool fresh=(facts&1u)!=0,native_precision=(facts&2u)!=0,face_locked=(facts&4u)!=0;
 R2_UI_BOOL_CALL(fill(fb,0,0,80,160,C_BG));
 if(status->menu.open){R2_UI_BOOL_CALL(menu_render(fb,m,status,now));return true;}
#ifdef R2_DC_PROFILE
 if(status->help){
  R2_UI_BOOL_CALL(row_label5(fb,10,"DC GNSS USB",C_WHITE));R2_UI_BOOL_CALL(row_label5(fb,35,"PRESS: PAGE",C_DIM));
  R2_UI_BOOL_CALL(row_label5(fb,60,"4 SEC: OFF",C_AMBER));R2_UI_BOOL_CALL(row_label5(fb,85,"FLASH ONLY",C_DIM));
  R2_UI_BOOL_CALL(row_label5(fb,110,"USB RMS IS",C_WHITE));R2_UI_BOOL_CALL(row_label5(fb,125,"SOF CADENCE",C_WHITE));
  R2_UI_BOOL_CALL(row_label5(fb,148,"PRESS:BACK",C_DIM));return true;
 }
 if(status->page!=0&&!status->screensaver){
  char value[24];R2_UI_BOOL_CALL(row_label4(fb,4,"DC GNSS USB",C_WHITE));
  if(status->page==3){
   R2_UI_BOOL_CALL(row_label4(fb,25,"UPTIME",C_DIM));R2_UI_OPTIONAL_CALL(r2_ui_uptime_text(value,now));R2_UI_BOOL_CALL(row_label4(fb,39,value,C_WHITE));
   R2_UI_BOOL_CALL(row_label4(fb,60,"LAST RESET",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,74,r2_ui_reset_text(m->reset_kind),C_WHITE));
   if(m->temperature_valid)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"DIE %d C",m->temperature_qc/4));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"DIE --"));
   R2_UI_BOOL_CALL(row_label4(fb,97,value,C_DIM));R2_UI_BOOL_CALL(row_label4(fb,119,"BLE OFF",C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,136,"FLASH ONLY",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,151,"4 SEC: OFF",C_DIM));return true;
  }
  R2_UI_BOOL_CALL(row_label4(fb,24,fresh&&m->clock.fix_active?"GPS FIX":"GPS WAIT",C_DIM));
  R2_UI_BOOL_CALL(row_label4(fb,40,gps_pps_live(m,fresh)?"PPS LIVE":"PPS WAIT",C_DIM));
  R2_UI_BOOL_CALL(snprintf(value,sizeof value,"SV USED %u",fresh?m->clock.sats_used:0));R2_UI_BOOL_CALL(row_label4(fb,56,value,C_WHITE));
  if(status->page==2){
   dc_age(value,fresh?age_ms(m->clock.gps_age_us):UINT32_MAX);R2_UI_BOOL_CALL(row_label4(fb,77,"RMC AGE",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,91,value,C_WHITE));
   dc_age(value,fresh?age_ms(m->clock.pps_age_us):UINT32_MAX);R2_UI_BOOL_CALL(row_label4(fb,111,"PPS AGE",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,125,value,C_WHITE));
   R2_UI_BOOL_CALL(row_label4(fb,149,"UTC ACC --",C_DIM));return true;
  }
  R2_UI_BOOL_CALL(row_label4(fb,76,m->scope_active&&fresh?"USB SOURCE":"SOURCE WAIT",C_DIM));
  R2_UI_BOOL_CALL(row_label4(fb,92,"USB RMS",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,106,s.native_rms,s.native_rms_valid?C_CYAN:C_DIM));
  R2_UI_BOOL_CALL(snprintf(value,sizeof value,"N %u",fresh?m->precision.samples:0));R2_UI_BOOL_CALL(row_label4(fb,123,value,C_DIM));
  R2_UI_BOOL_CALL(row_label4(fb,138,"HOPS 0",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,151,"UTC ACC --",C_DIM));return true;
 }
#endif
 if(status->screensaver&&r2_ui_saver_allowed(m,now)){
  /* Existing Casio materials/geometry, rigidly translated as one assembly.
   * 13px source strip +2px gap +62px dial +2px gap +13px identity.
   * No digital/dashboard panels are drawn in the saver. */
  int top=(int)r2_ui_saver_top(status->screensaver_position);
  R2_UI_BOOL_CALL(fill(fb,0,0,80,160,UI_COLOR(BLACK)));
  R2_UI_BOOL_CALL(watch_panel(fb,top,13));
  R2_UI_BOOL_CALL(row_label8(fb,top+3,clock_zone?clock_zone:"UTC",C_DIM));
 R2_UI_BOOL_CALL(label(fb,36,top+3,
#ifdef R2_LORA
    r2_ui_lora_primary(m,now)&&lora_current(m,now)?(m->lora.display_acquiring?"ACQ":m->lora.display_held?"HOLD":"LOR"):
#endif
    s.native_ble_time?(s.native_time_relay_hops==1?"RC1":s.native_time_relay_hops==2?"RC2":"BLE"):"PPS",1,
#ifdef R2_LORA
    r2_ui_lora_primary(m,now)&&(m->lora.display_held||m->lora.display_acquiring)?C_AMBER:
#endif
    C_GREEN));
  R2_UI_BOOL_CALL(draw_pulse_at(fb,s.pulse_indicator,66,(unsigned)(top+2)));
  R2_UI_BOOL_CALL(watch_panel(fb,top+15,62));
  if(s.native_lora_source&&s.native_lora_acquiring)R2_UI_BOOL_CALL(lora_sync_panel(fb,&s,top+15));
  else R2_UI_BOOL_CALL(analog_clock(fb,&s,top+44));
  R2_UI_BOOL_CALL(role_header_at(fb,&s,top+79));return true;
 }
#ifdef R2_LORA
 if(status->page==R2_UI_LORA_PAGE){
  const struct r2_lora_view *v=&m->lora;char text[24];
  R2_UI_BOOL_CALL(watch_panel(fb,1,13));
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
  R2_UI_BOOL_CALL(row_label4(fb,4,"LoRa TIMING",C_WHITE));
#else
  R2_UI_BOOL_CALL(label(fb,v->transmitter?13:4,4,v->transmitter?"LoRa RC8":"LoRa RTT EST",1,C_WHITE));
#endif
  R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%s %s",v->transmitter?"TX":"RX",v->requested?"ON":"OFF"));
  R2_UI_BOOL_CALL(label(fb,5,22,text,2,v->requested?C_CYAN:C_DIM));
  const char *state=v->fault?"RADIO FAULT":!v->requested?"DISABLED":!v->active?"INHIBITED":v->transmitter?(s.lora_locked?"TRAIN ACTIVE":v->time_valid?"WAIT TRAIN":"WAIT SOURCE"):v->metadata_cached&&v->observer_available?(v->rx_age_ms>=35000?"HOLD / EST":!v->context_known?"META STALE":!v->quality_known?"ACQ / EST":"MATCHED TIME"):v->coord_follow?"COORD FOLLOW":v->state==RC8_AVAILABLE?"MATCHED TIME":v->state==RC8_EXPIRED?(v->display_acquiring?"UTC ESTIMATE":v->display_held?"DISPLAY HOLD":"HOLD EXPIRED"):"WAIT PACKETS";
  R2_UI_BOOL_CALL(row_label4(fb,44,state,C_WHITE));
  int64_t observed_utc=0;bool observer_clock=lora_observer_current(m,now)&&lora_at(m,now,v->display_until_us,&observed_utc);
  if(observer_clock||(v->requested&&v->active&&v->utc_us&&v->until_us>now*1000)){
   uint64_t sec=observer_clock?(uint64_t)observed_utc/1000000:(v->utc_us+(now*1000>=v->sampled_us?now*1000-v->sampled_us:0))/1000000;
   R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%02u:%02u:%02u",(unsigned)(sec/3600%24),(unsigned)(sec/60%60),(unsigned)(sec%60)));
  }else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"--:--:--"));
  R2_UI_BOOL_CALL(row_label4(fb,58,text,C_WHITE));
  const char *source=v->kind==RC8_SELECTED_BLE?"BLE":v->kind==RC8_OWN_GM?"GM":"NONE";
  if(v->transmitter){
   R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%s SOURCE",source));R2_UI_BOOL_CALL(row_label4(fb,74,text,C_CYAN));
   R2_UI_BOOL_CALL(row_label4(fb,86,v->state==RC8_AVAILABLE?"TIME VALID":"TIME INVALID",v->state==RC8_AVAILABLE?C_GREEN:C_AMBER));
  }else{
   bool measured=lora_rms_current(m,now);
   uint32_t metric=lora_metric_ns(v);
   if(measured){
    if(metric<1000)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"RMS %uns",metric));
    else if(metric<1000000)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"RMS %u.%uus",metric/1000,metric%1000/100));
    else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"RMS %u.%ums",metric/1000000,metric%1000000/100000));
   }else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%s",v->observer_available?"NEXT PAIR":v->state==RC8_AVAILABLE&&v->requested&&v->active&&!v->fault?"COLLECTING":"NO TIMING"));
   R2_UI_BOOL_CALL(row_label4(fb,74,text,measured?C_GREEN:C_AMBER));
   if(v->metadata_cached&&measured)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"N%u ERR%us",v->link_samples>99?99:v->link_samples,v->prediction_age_ms/1000>9999?9999:v->prediction_age_ms/1000));
   else if(measured)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"N%u A%u.%us",v->link_samples,v->age_ms/1000,v->age_ms%1000/100));
   else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"LINK RMS"));
   R2_UI_BOOL_CALL(row_label4(fb,86,text,C_DIM));
  }
  if(v->parent_tag)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"R%04X P%04X",v->root_tag,v->parent_tag));else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"R%04X P----",v->root_tag));R2_UI_BOOL_CALL(row_label4(fb,100,text,C_DIM));
  if(v->parent_quality)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%s C%uA%u",source,v->parent_class,v->parent_accuracy));
  else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"%s META %s",source,v->context_known?"OK":v->metadata_cached?"OLD":"?"));
  R2_UI_BOOL_CALL(row_label4(fb,112,text,C_WHITE));
  if(!v->transmitter){
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
   R2_UI_BOOL_CALL(row_label4(fb,124,"PASSIVE RX",C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,136,"UTC UNQUAL",C_DIM));
#else
   uint32_t tof;bool estimated=r2_ui_lora_tof_ns(m,now,&tof);
   bool learning=!estimated&&v->rtt_base_count<2&&v->choice==RC8_SELECT_LORA&&lora_input_current(m,now);
   if(estimated)R2_UI_OPTIONAL_CALL(lora_tof_text(text,"ToF",tof?'+':'<',tof?tof:v->rtt_value_ns));
   else if(learning)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"ToF BASE %u/2",v->rtt_base_count));
   else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"ToF --"));
   uint16_t color=estimated?(tof?C_CYAN:C_WHITE):learning?C_AMBER:C_DIM;
   R2_UI_BOOL_CALL(row_label4(fb,124,text,color));
   if(estimated)R2_UI_OPTIONAL_CALL(lora_tof_text(text,"RTT",tof?'+':'<',(uint64_t)(tof?tof:v->rtt_value_ns)*2));
   else if(learning)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"RTT BASE %u/2",v->rtt_base_count));
   else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"RTT --"));
   R2_UI_BOOL_CALL(row_label4(fb,136,text,color));
#endif
  }else R2_UI_BOOL_CALL(row_label4(fb,136,v->choice==RC8_SELECT_BLE?"BLE SELECTED":v->choice==RC8_SELECT_LOCAL_GM?"GM SELECTED":"NO CLOCK",C_DIM));
  R2_UI_BOOL_CALL(row_label4(fb,150,"HOLD: MENU",C_AMBER));return true;
 }
#endif
 if(status->page==R2_UI_SHADOW_PAGE&&!status->help){
  const struct r2_shadow_session_view *v=&status->shadow;char text[24];
  R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(label(fb,10,4,"SHADOW PPS",1,C_WHITE));
  const char *state=v->state==R2_SHADOW_GM?"GM: NO TEST":v->state==R2_SHADOW_DWELL?"START IN 1S":v->state==R2_SHADOW_WAIT_PPS?"WAIT PPS":v->state==R2_SHADOW_WAIT_MAP?"WAIT BLE MAP":v->state==R2_SHADOW_SETTLING?"SETTLING 10S":v->state==R2_SHADOW_RUNNING?"MEASURING":v->state==R2_SHADOW_FULL?"SESSION FULL":"READY";
  R2_UI_BOOL_CALL(row_label4(fb,20,state,v->state==R2_SHADOW_RUNNING?C_GREEN:C_AMBER));
  uint64_t secs=v->elapsed_ms/1000;
  if(secs<360000)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"TIME %02u:%02u",(unsigned)(secs/60),(unsigned)(secs%60)));else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"TIME >99H"));
  R2_UI_BOOL_CALL(row_label4(fb,32,text,C_WHITE));
  R2_UI_BOOL_CALL(row_label4(fb,47,"MEAN",C_DIM));
  R2_UI_OPTIONAL_CALL(shadow_mean(text,v,v->valid));
  R2_UI_BOOL_CALL(row_label4(fb,58,text,v->valid?C_CYAN:C_WHITE));
  const unsigned y[3]={72,97,122};const uint32_t values[3]={v->p95_ns,v->p99_ns,v->max_ns};const char *names[3]={"P95 ABS","P99 ABS","MAX ABS"};
  for(unsigned i=0;i<3;i++){char magnitude[16];const char *name=v->approximate&&i<2?(i?"P99 APX":"P95 APX"):names[i];R2_UI_BOOL_CALL(row_label4(fb,y[i],name,C_DIM));R2_UI_OPTIONAL_CALL(shadow_value(magnitude,values[i],v->valid));R2_UI_BOOL_CALL(row_label4(fb,y[i]+11,magnitude,v->valid?C_CYAN:C_WHITE));}
  if(v->state==R2_SHADOW_SETTLING)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"WAIT %us W%u",v->settling_ms>=10000?0:10-v->settling_ms/1000,v->warmup));
  else if(v->samples<100000&&v->skipped<100)R2_UI_BOOL_CALL(snprintf(text,sizeof text,"N%u S%u",v->samples,v->skipped));else R2_UI_BOOL_CALL(snprintf(text,sizeof text,"N %u",v->samples));
  R2_UI_BOOL_CALL(row_label4(fb,150,text,C_DIM));return true;
 }
#ifdef R2_GM7_NATIVE
 if(m->gm7_owner&&!m->profile_rc&&!status->help&&status->page==5){
  char value[25],peer[25];R2_UI_BOOL_CALL(round_panel(fb,1,1,78,13,C_PANEL,C_BEZEL));R2_UI_BOOL_CALL(row_label5(fb,4,"GM PEER",C_WHITE));
  R2_UI_OPTIONAL_CALL(r2_ui_identity_name(peer,m->gm7_peer_valid?m->gm7_peer_id:0,NULL));R2_UI_BOOL_CALL(row_label3(fb,23,peer,C_WHITE));
  R2_UI_BOOL_CALL(row_label3(fb,40,!m->gm7_peer_valid?"NO REPORT":m->gm7_peer_current?"CURRENT SCOPE":m->gm7_peer_role==255?(m->gm7_peer_fresh?"FRESH CTRL":"HEARD ONLY"):"HISTORY",C_DIM));
  const char *roles[]={"STANDBY","ACTIVE","PREPARING","HOT","RC"};
  R2_UI_BOOL_CALL(row_label3(fb,57,m->gm7_peer_valid&&m->gm7_peer_role<5?roles[m->gm7_peer_role]:m->gm7_peer_valid?(m->gm7_peer_eligible_claim?"CLAIM ELIG":"UNQUALIFIED"):"ROLE --",m->gm7_peer_current?C_GREEN:C_AMBER));
  uint32_t durations[3]={m->gm7_peer_hot_ms,m->gm7_peer_active_ms,m->gm7_peer_pps_ms};const char *names[3]={"HOT","ACTIVE","PPS"};
  for(unsigned i=0;i<3;i++){
   bool known=m->gm7_peer_valid&&(m->gm7_peer_known&(1u<<i))&&durations[i]!=UINT32_MAX;
   if(known){uint32_t seconds=durations[i]/1000;if(seconds/60>9999)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s >9999m",names[i]));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s %02u:%02u",names[i],seconds/60,seconds%60));}else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s --",names[i]));
   R2_UI_BOOL_CALL(active_hold_label(fb,78+17*i,value));
  }
  R2_UI_BOOL_CALL(row_label3(fb,136,"AT SAMPLE",C_DIM));R2_UI_BOOL_CALL(row_label3(fb,149,"NO AUTHORITY",C_DIM));return true;
 }
#endif
 if(status->help||status->page==5){
  if(status->page==1){R2_UI_BOOL_CALL(rf_live(fb,&s));return true;}
  R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(row_label7(fb,4,"CLOCK KEY",C_WHITE));
  R2_UI_BOOL_CALL(row_label5(fb,24,"PRESS: NEXT",C_WHITE));R2_UI_BOOL_CALL(row_label5(fb,40,"HOLD: MENU",C_WHITE));
  R2_UI_BOOL_CALL(row_label5(fb,56,"MENU: OFF",C_AMBER));R2_UI_BOOL_CALL(row_label5(fb,79,"DIAL NEEDS",C_DIM));
  R2_UI_BOOL_CALL(row_label5(fb,93,"VALID TIME",C_WHITE));R2_UI_BOOL_CALL(row_label5(fb,107,"GM BLE LoRa",C_WHITE));
  R2_UI_BOOL_CALL(row_label5(fb,131,"GM/RC: MODE",C_DIM));R2_UI_BOOL_CALL(row_label5(fb,148,"PRESS:BACK",C_DIM));return true;
 }
 if(status->page==0){R2_UI_BOOL_CALL(front_clock(fb,&s,clock_zone));

#ifdef R2_PC_PROFILE
  R2_UI_BOOL_CALL(fill(fb,0,0,80,15,C_BG));R2_UI_BOOL_CALL(label(fb,10,4,"PC AWAKE",1,C_AMBER));
#endif
  if(m->profile_supported&&now<status->profile_flash_until&&((now/400)%2)==0){
   R2_UI_BOOL_CALL(fill(fb,0,0,80,15,C_BG));R2_UI_BOOL_CALL(label(fb,status->profile_target_rc?1:10,4,status->profile_target_rc?"RELAY PROFILE":"GM PROFILE",1,C_AMBER));
  }
  return true;}
 if(status->page==1){R2_UI_BOOL_CALL(rf_live(fb,&s));return true;}
 if(status->page==2){
#ifdef R2_LORA
  if(m->profile_rc&&r2_ui_lora_primary(m,now)){
   char value[24];bool live=lora_current(m,now),fit=lora_rms_current(m,now);
   R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(label(fb,10,4,"LOR TIMING",1,C_WHITE));
   if(m->lora.metadata_cached){
    const struct r2_lora_view *v=&m->lora;
    R2_UI_BOOL_CALL(row_label4(fb,20,v->fault?"RADIO FAULT":!live?"LORA SYNC":v->rx_age_ms>=35000?"HOLD / EST":!v->context_known?"META STALE":v->quality_known?"TRACK / REL":"ACQ / EST",v->fault?C_RED:C_AMBER));
    R2_UI_BOOL_CALL(row_label4(fb,33,"PRED RMS",C_DIM));
    if(live&&!v->prediction_n)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"NEXT PAIR"));else R2_UI_OPTIONAL_CALL(shadow_value(value,v->prediction_rms_ns,fit));
    R2_UI_BOOL_CALL(row_label4(fb,47,value,fit?C_CYAN:C_AMBER));
    R2_UI_BOOL_CALL(snprintf(value,sizeof value,"N%u SPAN%us",v->link_samples>99?99:v->link_samples,lora_fit_seconds(v)));R2_UI_BOOL_CALL(row_label4(fb,61,value,C_DIM));
    if(s.native_rx_known)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"RSSI %ddBm",v->rssi_half_dbm/2));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"RSSI --"));R2_UI_BOOL_CALL(row_label4(fb,77,value,C_WHITE));
    if(v->rx_age_ms==UINT32_MAX)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"PAIR WAIT"));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"PAIR %us",v->rx_age_ms/1000>9999?9999:v->rx_age_ms/1000));R2_UI_BOOL_CALL(row_label4(fb,91,value,C_DIM));
    if(v->prediction_age_ms==UINT32_MAX)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"ERR UNKNOWN"));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"ERR AGE%us",v->prediction_age_ms/1000>9999?9999:v->prediction_age_ms/1000));R2_UI_BOOL_CALL(row_label4(fb,106,value,C_DIM));
    R2_UI_BOOL_CALL(row_label4(fb,121,v->context_known?"META LIVE":"META CACHED",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,136,"UTC/RF --",C_DIM));
    R2_UI_BOOL_CALL(row_label4(fb,150,m->profile_rc?"RC MODE":"GM MODE",C_DIM));return true;
   }
   bool syncing=!fit&&!m->lora.display_held;unsigned phase=lora_sync_phase(m,now);
   R2_UI_BOOL_CALL(row_label4(fb,20,m->lora.fault?"RADIO FAULT":syncing?"LORA SYNC":live?(m->lora.display_held?"DISPLAY HOLD":"RECEIVING"):"WAIT PACKETS",m->lora.fault?C_RED:fit?C_GREEN:C_AMBER));
   R2_UI_BOOL_CALL(row_label4(fb,33,syncing?lora_sync_text(phase):"FIT RMS",C_DIM));
   if(syncing)R2_UI_OPTIONAL_CALL(lora_sync_progress(value,&m->lora));else R2_UI_OPTIONAL_CALL(shadow_value(value,m->lora.link_rms_ns,fit));
   R2_UI_BOOL_CALL(row_label4(fb,47,value,fit?C_CYAN:C_AMBER));
   R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%u PAIRS",m->lora.context_known?m->lora.link_samples:0));R2_UI_BOOL_CALL(row_label4(fb,61,value,C_DIM));
   if(s.native_rx_known)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"RSSI %ddBm",m->lora.rssi_half_dbm/2));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"RSSI --"));R2_UI_BOOL_CALL(row_label4(fb,77,value,C_WHITE));
   if(!m->lora.context_known||m->lora.rx_age_ms==UINT32_MAX)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"PAIR WAIT"));
   else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"PAIR %u.%us",m->lora.rx_age_ms/1000,m->lora.rx_age_ms%1000/100));
   R2_UI_BOOL_CALL(row_label4(fb,91,value,C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,106,local_pps_current(m,now)?"PPS LIVE":"PPS WAIT",local_pps_current(m,now)?C_GREEN:C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,121,"UTC ACC --",C_DIM));R2_UI_BOOL_CALL(row_label4(fb,136,m->profile_rc?"RC MODE":"GM MODE",C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,150,"RELATIVE FIT",C_DIM));return true;
  }
#endif
  bool gm=m->role==R2_UI_GM;
  bool active=active_gm_current(m,now);
  bool authorized=fresh&&m->scope_active&&m->precision.authorized;
  const char *state=gm?(fresh&&m->gm_source_fresh&&m->pps_precision.rate_valid?"PPS TRACK":"SOURCE WAIT"):
   native_precision?ble_servo_state(m,now):r2_ui_servo_state(m,now);
  char value[24];
  R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(label(fb,13,4,m->boundary_clock?"RC TIMING":"PRECISION",1,C_WHITE));
  if(!fresh||!m->scope_active)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"NO TIMING"));
  else if(active)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"GRANDMASTER"));
  else if(gm)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"GPS GM"));
  else if(m->time_relay_hops)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"BLE RC%u",m->time_relay_hops));
  else R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"BLE GM"));
  bool strict_gm=false;
#if defined(R2_GM7_NATIVE)
  strict_gm=m->gm7_owner;
#endif
  bool locked=!gm&&authorized&&m->precision.map_valid&&(m->precision.locked||(!strict_gm&&(face_locked||s.native_hot_standby)));
  R2_UI_BOOL_CALL(row_label4(fb,20,value,active||locked||(gm&&fresh&&m->gm_source_fresh)?C_GREEN:C_AMBER));
  if(locked&&!m->boundary_clock){R2_UI_BOOL_CALL(pair_lock(fb,4,32,C_GREEN));R2_UI_BOOL_CALL(label(fb,17,33,"SoftPLL",1,C_GREEN));}
  else R2_UI_BOOL_CALL(row_label4(fb,33,state,m->boundary_clock?(r2_ui_analog(m,now)&&!m->rc_source_held?C_GREEN:C_AMBER):(gm&&fresh&&m->gm_source_fresh&&m->pps_precision.rate_valid?C_GREEN:C_AMBER)));
  if(m->profile_rc){
   const struct r2_shadow_session_view *v=&status->shadow;
   bool stream=authorized&&m->ble_active&&r2_ui_ble_current(m,now)&&m->precision.map_valid&&m->precision.rms_valid&&m->precision.rms_ns!=UINT32_MAX;
   bool valid=fresh&&local_pps_current(m,now)&&m->clock.fix_active&&m->precision.map_valid&&v->active&&v->valid;
   R2_UI_BOOL_CALL(row_label4(fb,50,"BLE RMS",C_DIM));R2_UI_OPTIONAL_CALL(shadow_value(value,m->precision.rms_ns,stream));R2_UI_BOOL_CALL(row_label4(fb,64,value,stream?C_CYAN:C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,80,"SHD PPS MEAN",C_DIM));R2_UI_OPTIONAL_CALL(shadow_mean(value,v,valid));R2_UI_BOOL_CALL(row_label4(fb,92,value,valid?C_CYAN:C_DIM));
   if(valid){char rms[16];R2_UI_OPTIONAL_CALL(shadow_value(rms,v->rms_ns,true));R2_UI_BOOL_CALL(snprintf(value,sizeof value,"S-RMS %s",rms));}
   else if(!local_pps_current(m,now))R2_UI_BOOL_CALL(snprintf(value,sizeof value,"WAIT PPS"));
   else if(!m->clock.fix_active)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"WAIT GPS FIX"));
   else if(!fresh||!m->precision.map_valid)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"WAIT BLE MAP"));
   else if(v->state==R2_SHADOW_SETTLING)R2_UI_BOOL_CALL(snprintf(value,sizeof value,"SETTLE %us",v->settling_ms>=10000?0:10-v->settling_ms/1000));
   else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"STARTING"));
   R2_UI_BOOL_CALL(row_label4(fb,104,value,C_DIM));
   R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s SV%u",!fresh?"GPS --":m->clock.fix_active?"GPS FIX":"GPS WAIT",fresh?m->clock.sats_used:0));R2_UI_BOOL_CALL(row_label4(fb,116,value,fresh&&m->clock.fix_active?C_GREEN:C_AMBER));
   R2_UI_OPTIONAL_CALL(hold_text(value,"PPS",local_pps_current(m,now)?status->holds.pps_since:0,now));R2_UI_BOOL_CALL(row_label4(fb,128,value,C_DIM));
   R2_UI_BOOL_CALL(row_label4(fb,140,"RC MODE",C_DIM));return true;
  }
  bool ble_rms=!gm&&authorized&&m->ble_active&&r2_ui_ble_current(m,now)&&
    m->precision.map_valid&&m->precision.rms_valid&&m->precision.rms_ns!=UINT32_MAX;
  bool beat=gm&&authorized&&m->ble_active&&m->gm_source_fresh&&m->precision.rms_valid&&m->precision.rms_ns!=UINT32_MAX;
  if(gm){R2_UI_BOOL_CALL(bluetooth_badge_at(fb,4,49,beat));R2_UI_BOOL_CALL(label(fb,19,51,"BEAT RMS",1,C_DIM));}
  else R2_UI_BOOL_CALL(row_label4(fb,51,"BLE RMS",C_DIM));
  R2_UI_OPTIONAL_CALL(shadow_value(value,m->precision.rms_ns,gm?beat:ble_rms));
  R2_UI_BOOL_CALL(row_label4(fb,64,value,(gm?beat:ble_rms)?C_CYAN:C_DIM));
  R2_UI_BOOL_CALL(row_label4(fb,83,!fresh?"GPS --":m->clock.fix_active?"GPS FIX":"GPS NO FIX",fresh&&m->clock.fix_active?C_GREEN:C_AMBER));
  R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SV USED %u",fresh?m->clock.sats_used:0));R2_UI_BOOL_CALL(row_label4(fb,96,value,C_WHITE));
  bool pps=local_pps_current(m,now);
  R2_UI_OPTIONAL_CALL(hold_text(value,"PPS",pps?status->holds.pps_since:0,now));R2_UI_BOOL_CALL(row_label4(fb,109,value,pps?C_GREEN:C_AMBER));
  if(active)R2_UI_OPTIONAL_CALL(hold_text(value,"ACTIVE",status->holds.active_since,now));
  else if(hot_current(m,now))R2_UI_OPTIONAL_CALL(hold_text(value,"HOT",status->holds.hot_since,now));
  else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s",hot_title(m,fresh,s.native_hot_standby)));
  if(active)R2_UI_BOOL_CALL(active_hold_label(fb,128,value));else R2_UI_BOOL_CALL(row_label4(fb,128,value,s.native_hot_standby?C_GREEN:m->gm_cold?C_CYAN:C_AMBER));
  const char *why=hot_reason(m,fresh,s.native_hot_standby);
  if(!strcmp(why,"SOURCE DWELL"))R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"DWELL %us",(m->admission_ms+999)/1000));
  else R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"%s",why));
  R2_UI_BOOL_CALL(row_label4(fb,141,value,C_DIM));return true;
 }
 if(status->page==3){
  char value[24];
  R2_UI_BOOL_CALL(round_panel(fb,1,1,78,13,C_PANEL,C_BEZEL));R2_UI_BOOL_CALL(label(fb,2,4,"CLOCK QUALITY",1,C_WHITE));
  R2_UI_BOOL_CALL(row_label3(fb,20,!fresh?"STATE STALE":m->clock.fix_active?"GPS FIX":"GPS NO FIX",fresh&&m->clock.fix_active?C_GREEN:C_AMBER));
  R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SV USED %u",fresh?m->clock.sats_used:0));R2_UI_BOOL_CALL(row_label3(fb,33,value,C_WHITE));
  R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"RMC %c MODE %c",fresh&&m->clock.rmc_status?m->clock.rmc_status:'-',fresh&&m->clock.rmc_mode?m->clock.rmc_mode:'-'));R2_UI_BOOL_CALL(row_label3(fb,46,value,C_DIM));
  if(fresh&&m->clock.gps_age_us>=0&&m->clock.gps_age_us<10000000)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"GPS %ums",age_ms(m->clock.gps_age_us)));else R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"GPS STALE"));
  R2_UI_BOOL_CALL(row_label3(fb,63,value,C_WHITE));
  R2_UI_BOOL_CALL(row_label3(fb,76,gps_pps_live(m,fresh)?"PPS LIVE":"PPS WAIT",gps_pps_live(m,fresh)?C_GREEN:C_AMBER));
  int64_t pps_age=m->pps_precision.epoch?m->pps_precision.age_us:m->clock.pps_age_us;
  if(fresh&&pps_age>=0&&pps_age<10000000)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"PPS %ums",age_ms(pps_age)));else R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"PPS STALE"));
  R2_UI_BOOL_CALL(row_label3(fb,89,value,C_WHITE));
  if(!fresh)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SOURCE --"));
  else if(m->profile_rc||!m->gm_capable)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SOURCE OFF"));
  else if(m->source_eligible)R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SOURCE READY"));
  else if(m->clock.fix_active&&gps_pps_live(m,fresh))R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"DWELL %us",(m->admission_ms+999)/1000));
  else R2_UI_BOOL_CALL(snprintf(value,sizeof(value),"SOURCE WAIT"));
  R2_UI_BOOL_CALL(row_label3(fb,107,value,fresh&&m->source_eligible?C_GREEN:C_AMBER));
  if(hot_current(m,now))R2_UI_OPTIONAL_CALL(hold_text(value,"HOT",status->holds.hot_since,now));else R2_UI_BOOL_CALL(snprintf(value,sizeof value,"%s",hot_title(m,fresh,false)));
  R2_UI_BOOL_CALL(row_label3(fb,120,value,hot_current(m,now)?C_GREEN:C_AMBER));
  R2_UI_BOOL_CALL(row_label3(fb,133,hot_reason(m,fresh,s.native_hot_standby),C_DIM));
  R2_UI_BOOL_CALL(row_label3(fb,149,"UTC ACC --",C_DIM));return true;
 }
 R2_UI_BOOL_CALL(watch_panel(fb,1,13));R2_UI_BOOL_CALL(row_label7(fb,4,"SYSTEM",C_WHITE));
 char text[16];R2_UI_BOOL_CALL(row_label5(fb,23,"UPTIME",C_DIM));
 R2_UI_OPTIONAL_CALL(r2_ui_uptime_text(text,now));R2_UI_BOOL_CALL(row_label5(fb,35,text,C_WHITE));
 R2_UI_BOOL_CALL(row_label5(fb,51,"LAST RESET",C_DIM));
 R2_UI_BOOL_CALL(row_label5(fb,63,r2_ui_reset_text(m->reset_kind),C_WHITE));
 R2_UI_BOOL_CALL(snprintf(text,sizeof(text),"SAT %u",fresh?m->clock.sats_used:0));R2_UI_BOOL_CALL(row_label5(fb,82,text,C_WHITE));
 R2_UI_BOOL_CALL(snprintf(text,sizeof(text),"RX %u",m->rx));R2_UI_BOOL_CALL(row_label5(fb,94,text,C_WHITE));
 R2_UI_BOOL_CALL(snprintf(text,sizeof(text),"AUTH %u",m->authenticated));R2_UI_BOOL_CALL(row_label5(fb,106,text,C_WHITE));
 R2_UI_BOOL_CALL(snprintf(text,sizeof(text),"DROP %u",m->queue_drops));R2_UI_BOOL_CALL(row_label5(fb,118,text,C_WHITE));
 R2_UI_BOOL_CALL(row_label5(fb,134,m->ble_active?"BLE RUNNING":"BLE STOPPED",C_CYAN));
 R2_UI_BOOL_CALL(row_label5(fb,149,"HOLD: MENU",C_DIM));

 return true;
}

bool r2_ui_local_pulse(const struct r2_ui_model *m,uint64_t now){
 return now>=m->sampled_ms&&now-m->sampled_ms<=2500&&
  (!m->ble_time_selected||r2_ui_ble_current(m,now))&&m->clock.pps_present&&
  m->clock.pps_age_us>=0&&m->clock.pps_age_us<250000&&now-m->sampled_ms<250&&
  (uint64_t)m->clock.pps_age_us+(now-m->sampled_ms)*1000<250000;
}
void r2_ui_zone_cache_update(struct r2_ui_zone_cache *c,const struct r2_ui_model *m,uint64_t now){
#ifdef R2_TZ_WIRE
 int64_t utc=0;bool coarse;if(!r2_ui_display_time(m,now,&utc,&coarse)||utc<0)return;
 *c=(struct r2_ui_zone_cache){0};
 c->zone=r2_ui_display_zone(m,now,utc,&c->flags);
 if(!(c->flags&R2_UI_TZ_LOCAL_VALID))return;
 c->until_us=UINT64_MAX;
#ifdef R2_TZ_AUTO
 if(c->flags&R2_UI_TZ_CALENDAR){
  if(!r2_tz_db_offset_span(c->zone.id,(uint64_t)utc,&c->zone.offset_min,&c->first_us,&c->until_us))
   c->until_us=0;
 }
#endif
#else
 *c=(struct r2_ui_zone_cache){0};(void)m;(void)now;
#endif
}
#ifdef R2_TZ_WIRE
static const char *tile_zone(const struct r2_ui_zone_cache *c,const struct r2_ui_model *m,
 uint64_t now,int64_t utc,int64_t *shown){
 *shown=utc;
#ifdef R2_TZ_WIRE
 if(now<m->sampled_ms||now-m->sampled_ms>2500)return "UTC";
 uint8_t flags;struct r2_tz value=display_tz_input(m,now,&flags);
 if(!r2_tz_valid(value)&&r2_tz_valid(m->display_tz))value=m->display_tz;
 if(!c||utc<0||!r2_tz_valid(value)||value.id!=c->zone.id||
    !(c->flags&R2_UI_TZ_LOCAL_VALID)||(uint64_t)utc<c->first_us||
    (uint64_t)utc>=c->until_us||
    (!(c->flags&R2_UI_TZ_CALENDAR)&&value.offset_min!=c->zone.offset_min)||
    !r2_tz_local_us(c->zone,utc,shown))return "UTC";
 if(c->zone.id==1)return "UTC";
 if(c->zone.id==2&&c->zone.offset_min==-300)return "CDT";
 if(c->zone.id==2&&c->zone.offset_min==-360)return "CST";
 return "LOC";
#else
 (void)c;(void)m;(void)now;return "UTC";
#endif
}
#endif
#ifdef R2_UI_PARTIAL_JOBS
static bool tile_draw(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *status,
 uint64_t now,enum r2_ui_tile tile,const struct r2_ui_zone_cache *zone,struct r2_ui_tile_view *view){
 if(tile==R2_UI_TILE_CLEAR){*view=(struct r2_ui_tile_view){0};
  return !fb||fill(fb,ui_raster.x,ui_raster.y,ui_raster.w,ui_raster.h,C_BG);}
 ui_snapshot_t s={0};const char *z;
 R2_UI_BOOL_CALL(render_snapshot(&s,m,status,now,true,zone,view,&z,NULL));
 if(!fb)return true;
 if(tile==R2_UI_TILE_PPS){
  R2_UI_BOOL_CALL(draw_pulse_at(fb,s.pulse_indicator,66,
   status->screensaver?r2_ui_saver_top(status->screensaver_position)+2:19));return true;
 }
 if(tile==R2_UI_TILE_TIME){
  R2_UI_BOOL_CALL(fill(fb,0,16,80,32,C_BG));
  R2_UI_BOOL_CALL(front_clock_time(fb,&s,z));return true;
 }
 if(tile==R2_UI_TILE_LOSS){
  if(view->analog)return true;
  int top=status->screensaver?(int)r2_ui_saver_top(status->screensaver_position)+15:54;
  R2_UI_BOOL_CALL(fill(fb,0,0,80,160,C_BG));
  R2_UI_BOOL_CALL(watch_panel(fb,top,62));
  if(!view->analog){
   uint16_t color=s.state==CLOCK_NO_TIME?C_RED:C_AMBER;
   R2_UI_BOOL_CALL(fill(fb,5,top+1,70,57,C_PANEL));
   R2_UI_BOOL_CALL(fill(fb,5,top+1,2,57,color));
   const char *heading=s.native_lora_source?"LORA SYNC":native_clock_reason(&s);
   unsigned n=0;while(heading[n])++n;
   R2_UI_BOOL_CALL(label(fb,(80-(int)n*6)/2,top+8,heading,1,color));
  }
 }

 return true;
}
bool r2_ui_tile_render_rect(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,
 uint64_t now,enum r2_ui_tile tile,const struct r2_ui_zone_cache *zone,struct r2_ui_tile_view *view,
 r2_ui_render_checkpoint_fn checkpoint,void *context,struct r2_ui_rect rect){
 if(ui_raster.active||!rect.w||!rect.h||rect.x+rect.w>80||rect.y+rect.h>160)return false;
 ui_raster.checkpoint=checkpoint;ui_raster.context=context;ui_raster.remaining=R2_UI_WIDTH;
 ui_raster.active=true;ui_raster.aborted=false;ui_raster.critical=true;ui_raster.drawing=false;ui_raster.x=0;ui_raster.y=0;ui_raster.w=0;ui_raster.h=0;
 ui_raster.x=rect.x;ui_raster.y=rect.y;ui_raster.w=rect.w;ui_raster.h=rect.h;
 bool complete=tile_draw(fb,m,s,now,tile,zone,view);
 ui_raster.active=false;ui_raster.aborted=false;ui_raster.critical=false;ui_raster.w=0;ui_raster.checkpoint=NULL;ui_raster.context=NULL;
 return complete;
}
bool r2_ui_tile_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,
 uint64_t now,enum r2_ui_tile tile,const struct r2_ui_zone_cache *zone,struct r2_ui_tile_view *view,
 r2_ui_render_checkpoint_fn checkpoint,void *context){
 struct r2_ui_rect r={0,16,80,32};
 if(tile==R2_UI_TILE_PPS)r=(struct r2_ui_rect){66,s->screensaver?r2_ui_saver_top(s->screensaver_position)+2:19,9,9};
 else if(tile==R2_UI_TILE_LOSS)r=(struct r2_ui_rect){0,s->screensaver?r2_ui_saver_top(s->screensaver_position)+15:54,80,62};
 return r2_ui_tile_render_rect(fb,m,s,now,tile,zone,view,checkpoint,context,r);
}
bool r2_ui_render_try(uint8_t *fb,const struct r2_ui_model *m,
 const struct r2_ui_status *s,uint64_t now,r2_ui_render_checkpoint_fn checkpoint,
 void *context){
 if(ui_raster.active)return false;
 ui_raster.checkpoint=checkpoint;ui_raster.context=context;
 ui_raster.remaining=R2_UI_WIDTH;ui_raster.aborted=false;ui_raster.active=true;ui_raster.critical=false;ui_raster.drawing=false;ui_raster.w=0;
 bool complete=render_full(fb,m,s,now);
 ui_raster.active=false;ui_raster.aborted=false;ui_raster.checkpoint=NULL;ui_raster.context=NULL;
 return complete;
}

#endif

void r2_ui_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now){(void)render_full(fb,m,s,now);}
