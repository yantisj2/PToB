/* SPDX-License-Identifier: Apache-2.0 */
#include "ui.h"
#include "rc_shadow.h"
#include "shadow_settle.h"
#ifdef R2_GM7_NATIVE
#include "ble_service.h"
#include "rf_clock.h"
#include "capture.h"
#endif
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/random/random.h>
#include <errno.h>
#include <stdio.h>
#if defined(R2_UI_PARTIAL_JOBS)
#define R2_UI_CRITICAL 1
#endif
#define R2_LOCK_UNIT 12u
#include "health_lock.h"
#if defined(CONFIG_ARM) && defined(R2_GM7_NATIVE) && defined(R2_LORA) && defined(R2_TZ_WIRE)
_Static_assert(sizeof(struct r2_ui_model)==752,"logical split must not grow model");
_Static_assert(sizeof(struct r2_ui_status)==248,"logical status uses existing padding");
_Static_assert(offsetof(struct r2_ui_status,logic_flags)==55,"logical flags padding");
_Static_assert(offsetof(struct r2_ui_status,logic_age_ms)==212,"logical age padding");
#endif
static struct r2_ui_model model;
static struct r2_ui_status status;
static struct k_thread thread;
#ifdef R2_UI_CRITICAL
#include "bounded_json.h"
/* Private named phases: contiguous controller outcomes compact comparisons.
 * No external consumer serializes these values; initial UNSAFE stays zero. */
enum ui_phase {UI_UNSAFE,UI_LOCK,UI_OWNER_LOCK,UI_WAIT9,UI_WAIT8,UI_SPI,
 UI_READY,UI_RASTER,UI_RELEASING,UI_OWNER_RELEASING};
_Static_assert(UI_UNSAFE==0,"Initial zeroed worker must remain nonpromotable");
static atomic_t ui_worker_phase,ui_critical_dirty;
static atomic_t ui_read_owner;
static bool worker_current(void){return k_current_get()==&thread;}
static void critical_priority_tick(void);
static void critical_observe(uint64_t);
static bool optional_enter(void);
static bool optional_owner_enter(void);
static bool read_boundary(void *,unsigned,bool);
#endif
/* UI state is shared only by ordinary threads. Keep radio interrupts enabled
 * during model, hold and calendar calculations. Priority inheritance bounds
 * thread contention; display I/O and sleeping remain outside this mutex. */
/* Main is the sole raw/debounce/menu sampler. Only the tiny idle/stable
 * view is shared with the opportunistic renderer, under the same short mutex. */
static atomic_t input_ready,input_error;
static uint64_t input_due,input_changed,input_last_poll,input_idle_since,input_virtual_until;
static int input_last,input_stable;
static bool input_wake_press;
#if defined(R2_DC_PROFILE) || defined(R2_PC_PROFILE)
static uint64_t input_pressed;static bool input_held;
#else
static struct r2_ui_button_state input_button;
#endif
static K_MUTEX_DEFINE(ui_lock);
static unsigned ui_take(void){
#ifdef R2_UI_CRITICAL
 if(worker_current())atomic_set(&ui_worker_phase,UI_LOCK);
#endif
 (void)k_mutex_lock(&ui_lock,K_FOREVER);return 0;
}
static void ui_drop(unsigned key){
 (void)key;
#ifdef R2_UI_CRITICAL
 if(worker_current())atomic_set(&ui_worker_phase,UI_RELEASING);
#endif
 (void)k_mutex_unlock(&ui_lock);
}
static atomic_t stop_requested,profile_toggle,ble_toggle,press_request;
static atomic_t poweroff_request,close_state;
/* One pending visual job; replaced requests coalesce, never catch up. Only
 * main writes the context tuple. Worker reads its atomic revision/flags. */
static atomic_t model_requested,context_revision=1,context_flags,source_hint;
static atomic_t source_loss_ms,source_loss_revision,model_interval=250;
/* Single main consumer; even source churn cannot accelerate optional jobs. */
static uint64_t model_not_before;
static struct r2_ui_context published_context;
static bool context_seen;
static uint32_t model_context=1,model_hint;
static uint64_t model_serial;
static uint32_t control_serial;
#ifdef R2_UI_CRITICAL
static atomic_t ui_control_revision,ui_policy_revision=1,ui_urgent_revision;
#endif
static void control_change(void){
 ++control_serial;
#ifdef R2_UI_CRITICAL
 atomic_set(&ui_control_revision,(atomic_val_t)control_serial);
#endif
}
/* Routine clock publications do not invalidate a healthy in-flight frame. */
static uint32_t logic_revision=1,logic_signature;
static uint64_t model_lora_emitter,logic_bad_at;
/* Descriptive factory keys only, with no time/boot/epoch lease. */
static struct r2_ui_label_ids model_labels;
static uint32_t logic_bad_policy;
static struct {
 int64_t utc_us;uint64_t at_ms,completed_ms;
 uint32_t context,hint,policy;bool pending;
} analog_ack;
/* Main-only cadence; no worker, demand flag or rejection can postpone it. */
static uint64_t logic_due_ms;

/* Tags travel with copied status, so lock-free main invalidations cannot
 * resurrect old reported validity after a preempted UI commit. */
static uint32_t drawn_context=1,drawn_hint,history_context=1;
#ifdef R2_TZ_WIRE
static atomic_t learned_tz;
static uint32_t tz_pack(struct r2_tz t,uint8_t source){
 if(!r2_tz_valid(t))return 0;
 uint32_t flags=(source&3u)|((source&R2_UI_TZ_CACHED)?4u:0u)|
   ((source&R2_UI_TZ_OBSERVED)?8u:0u);
 return t.id|((uint32_t)(t.offset_min+840)<<16)|(flags<<27);
}
static struct r2_tz tz_unpack(uint32_t v,uint8_t *source){
 unsigned flags=v>>27;
 *source=(flags&3u)|((flags&4u)?R2_UI_TZ_CACHED:0u)|((flags&8u)?R2_UI_TZ_OBSERVED:0u);
 return v?(struct r2_tz){(uint16_t)v,(int16_t)((int32_t)((v>>16)&2047u)-840)}:r2_tz_unknown();
}
void r2_ui_learn_tz(struct r2_tz t,uint8_t source){
 uint32_t packed=tz_pack(t,source);
 if(packed&&(uint32_t)atomic_get(&learned_tz)!=packed)atomic_set(&learned_tz,(int32_t)packed);
}
#endif
void r2_ui_context_update(const struct r2_ui_context *c){
 /* Main is the sole tuple writer. An unchanged tuple needs no publication
  * mutex; changed tuples still serialize with optional readers below. */
 if(context_seen&&published_context.incarnation==c->incarnation&&
    published_context.root==c->root&&published_context.boot==c->boot&&
    published_context.source_epoch==c->source_epoch&&
    published_context.train==c->train&&published_context.flags==c->flags)return;
 unsigned key=ui_take();
 published_context=*c;context_seen=true;
 atomic_set(&context_flags,c->flags);
 uint32_t revision=(uint32_t)atomic_get(&context_revision);
 if(revision!=UINT32_MAX)atomic_set(&context_revision,(int32_t)(revision+1));
 ui_drop(key);atomic_set(&model_requested,1);
}
bool r2_ui_model_requested(uint64_t now){
 if(now<model_not_before||!atomic_cas(&model_requested,1,0))return false;
 unsigned interval=(unsigned)atomic_get(&model_interval);
 model_not_before=now+(interval==125?125:250);return true;
}
void r2_ui_source_hint(unsigned flags){
 uint32_t old=(uint32_t)atomic_get(&source_hint);
 if((old&15u)==flags||(old&UINT32_C(0xfffffff0))==UINT32_C(0xfffffff0))return;
 uint32_t next=((old&UINT32_C(0xfffffff0))+16u)|flags;
 if((next&UINT32_C(0xfffffff0))==UINT32_C(0xfffffff0))next=UINT32_C(0xfffffff0);
 bool lost=((old&R2_UI_HINT_BLE_VALID)&&!(flags&R2_UI_HINT_BLE_VALID))||
   ((old&R2_UI_HINT_LOCAL_VALID)&&!(flags&R2_UI_HINT_LOCAL_VALID));
 if(lost&&!atomic_get(&source_loss_revision)){
  atomic_set(&source_loss_ms,(int32_t)(uint32_t)k_uptime_get());
  atomic_set(&source_loss_revision,(int32_t)next);
 }
 atomic_set(&source_hint,(int32_t)next);atomic_set(&model_requested,1);
}
static void sync_update(struct r2_ui_sync_display *c,const struct r2_ui_model *m,
 uint64_t now,bool analog_drawn,uint32_t hint){
 r2_ui_sync_display_update(c,m,now,analog_drawn);
 if(!c->at_ms||!c->loss_ms)return;
 uint32_t lost=(uint32_t)atomic_get(&source_loss_revision);
 uint32_t at=(uint32_t)atomic_get(&source_loss_ms);
 /* Source-hint generations never wrap. The main-observed first loss can
  * shorten the visual deadline, and cannot restart it after worker starvation. */
 if(!lost||(lost&UINT32_C(0xfffffff0))>(hint&UINT32_C(0xfffffff0))||
    (uint32_t)atomic_get(&source_hint)!=hint)return;
 uint32_t elapsed=(uint32_t)now-at;
 if(elapsed>INT32_MAX||now<elapsed){*c=(struct r2_ui_sync_display){0};return;}
 uint64_t loss=now-elapsed;
 if(loss>=c->at_ms&&loss<c->loss_ms)c->loss_ms=loss;
 if(now<c->loss_ms||now-c->loss_ms>=500)*c=(struct r2_ui_sync_display){0};
}
#ifdef R2_LORA
static void transmitter_withdraw(struct r2_ui_model *m){
 if(m->lora.transmitter){m->lora.time_valid=false;m->lora.state=RC8_ACQUIRING;m->lora.choice=RC8_SELECT_NONE;}
}
static void transmitter_age(struct r2_ui_model *m,uint64_t now,unsigned cadence){
 if(now<m->sampled_ms||now-m->sampled_ms>cadence)transmitter_withdraw(m);
}
#endif
static void model_fence(struct r2_ui_model *m,uint32_t revision,uint32_t previous_hint){
 uint32_t current=(uint32_t)atomic_get(&context_revision);
#ifdef R2_LORA
 uint32_t source=(uint32_t)atomic_get(&source_hint);
 bool source_valid=!(source&R2_UI_HINT_KNOWN)||
   ((source&R2_UI_HINT_LOCAL)?(source&R2_UI_HINT_LOCAL_VALID):(source&R2_UI_HINT_BLE_VALID));
 if(current!=revision||current==UINT32_MAX||source!=previous_hint||!source_valid||
    (source&UINT32_C(0xfffffff0))==UINT32_C(0xfffffff0))transmitter_withdraw(m);
#endif
 unsigned flags=(unsigned)atomic_get(&context_flags);
 if(current!=revision||current==UINT32_MAX||(flags&(R2_UI_CONTEXT_SWITCH|R2_UI_CONTEXT_STOP))){
 bool previous_rc=m->profile_rc;
 m->profile_rc=!!(flags&R2_UI_CONTEXT_RC);
 m->profile_switching=!!(flags&R2_UI_CONTEXT_SWITCH);
 m->scope_active=m->authenticated_gm=m->gm_source_fresh=m->ble_time_selected=false;
 m->clock.valid=m->coarse_valid=m->follower_pps_valid=false;
 m->face_lock_until_us=0;m->precision.map_valid=m->precision.authorized=m->precision.locked=false;
 bool live=!!(flags&R2_UI_CONTEXT_LIVE)&&!(flags&(R2_UI_CONTEXT_STOP|R2_UI_CONTEXT_BLE_FAULT));
 m->ble_active=live;m->sync_attempt=live&&!(flags&R2_UI_CONTEXT_SERVING);
 m->local_hot_standby=false;m->precision_beat_hz=0;m->role=R2_UI_NO_ROLE;
 /* A label assigned by the independent RC LoRa view survives unrelated
  * BLE replacement. A cached BLE label must never become its emitter name. */
 bool keep_lora_label=previous_rc&&m->upstream_is_lora&&(flags&R2_UI_CONTEXT_RC)&&
   !(flags&(R2_UI_CONTEXT_SWITCH|R2_UI_CONTEXT_STOP));
 m->root_name[0]=m->coordinator_name[0]=0;
 if(!keep_lora_label)m->upstream_name[0]=0;
#ifdef R2_GM7_NATIVE
 m->gm7_known=0;m->gm7_peer_valid=false;
#endif
#ifdef R2_LORA
 /* A different BLE root does not erase an independent live LoRa HOLD. A
  * profile/stop transition fences every old visual source immediately. */
 if(flags&(R2_UI_CONTEXT_SWITCH|R2_UI_CONTEXT_STOP)){
  m->lora.choice=RC8_SELECT_NONE;m->lora.time_valid=false;
 }
#endif
 }
 unsigned hint=(unsigned)atomic_get(&source_hint);
 bool exhausted=(hint&UINT32_C(0xfffffff0))==UINT32_C(0xfffffff0);
 if(exhausted){
  m->clock.valid=m->coarse_valid=m->gm_source_fresh=false;
  m->ble_valid_until_us=m->face_lock_until_us=0;
  m->precision.map_valid=m->precision.locked=m->follower_pps_valid=false;
  return;
 }
 if(!(hint&R2_UI_HINT_KNOWN))return;
 /* These hints only withdraw cached permissions. A fresh hint cannot restore
  * a masked deadline, model or history; only a fresh owner projection can. */
 if(hint&R2_UI_HINT_LOCAL){
  if(!(hint&R2_UI_HINT_LOCAL_VALID)||hint!=previous_hint){
   m->gm_source_fresh=m->clock.source_available=m->clock.valid=false;
  }
 }else if((!(hint&R2_UI_HINT_BLE_VALID)||hint!=previous_hint)&&m->ble_time_selected){
  /* The RC owner may freshly project its existing display-only local HOLD.
   * Never recover a pre-withdraw copy or extend that owner's original expiry;
   * original map/auth/scope and analog helpers still decide the visible face. */
  bool fresh_hold=m->profile_rc&&m->boundary_clock&&m->ble_local_holdover&&
    current==revision&&hint==previous_hint&&
    r2_ui_ble_current(m,(uint64_t)k_uptime_get());
  if(!fresh_hold){
   m->ble_valid_until_us=0;m->follower_pps_valid=false;
   m->precision.map_valid=m->precision.locked=false;
  }
 }
}
#ifndef R2_UI_CRITICAL
static bool frame_current(uint32_t policy,uint32_t revision,unsigned page,uint32_t control,unsigned hint){
 unsigned key=ui_take();bool current=policy!=UINT32_MAX&&logic_revision==policy&&status.page==page&&control_serial==control;ui_drop(key);
 return current&&(uint32_t)atomic_get(&context_revision)==revision&&revision!=UINT32_MAX&&(unsigned)atomic_get(&source_hint)==hint;
}
#endif
bool r2_ui_logic_due(uint64_t now,uint64_t owner_until,bool owner_valid){
 /* Main alone calls this and publishes these timing fields. The optional
  * worker writes only model name arrays; keep timing ownership in main. */
 uint32_t revision=(uint32_t)atomic_get(&context_revision),hint=(uint32_t)atomic_get(&source_hint);
 bool changed=model_context!=revision||model_hint!=hint;
 uint64_t until=model.ble_valid_until_us;
 bool held=model.profile_rc&&model.ble_local_holdover;
 bool near=now<=(UINT64_MAX-1000)/1000&&until&&until<=now*1000+1000&&
  ((owner_valid&&owner_until>until)||held);
 if(now<logic_due_ms&&!changed&&!near)return false;
 logic_due_ms=now+20;return true;
}
static uint32_t logical_signature(const struct r2_ui_model *m,uint64_t now){
 int64_t utc;bool coarse;
 uint32_t bits=(r2_ui_display_time(m,now,&utc,&coarse)?1u:0)|
  (r2_ui_analog(m,now)?2u:0)|(r2_ui_face_locked(m,now)?4u:0)|
  (r2_ui_ble_current(m,now)?8u:0)|(m->scope_active?16u:0)|
  (m->follower_pps_valid?32u:0)|(m->profile_rc?64u:0)|
  (m->ble_local_holdover?128u:0)|(m->gm_source_fresh?256u:0)|
  ((uint32_t)(m->role&3u)<<9);
#ifdef R2_LORA
 bits|=(r2_ui_lora_primary(m,now)?2048u:0)|((uint32_t)m->lora.choice<<12)|
   ((uint32_t)(m->lora.time_valid!=0)<<15)|((uint32_t)m->lora.state<<16)|
   ((uint32_t)m->lora.transmitter<<24);
#endif
 return bits;
}
static unsigned logical_flags(const struct r2_ui_model *m,const struct r2_ui_sync_display *c,uint64_t now){
 int64_t utc;bool coarse;unsigned flags=model_serial?R2_UI_LOGIC_MAINTAINED:0;
 if(logic_revision==UINT32_MAX)return flags;
 if(now<m->sampled_ms||now-m->sampled_ms>2500)return flags;
 flags|=R2_UI_LOGIC_FRESH;
 bool valid=r2_ui_display_time(m,now,&utc,&coarse);
 if(r2_ui_sync_display_time(c,m,now,&utc)){valid=true;flags|=R2_UI_LOGIC_GRACE;}
 if(valid)flags|=R2_UI_LOGIC_VALID;
 if(r2_ui_ble_current(m,now))flags|=R2_UI_LOGIC_BLE;
 if(m->role==R2_UI_GM&&m->gm_source_fresh)flags|=R2_UI_LOGIC_GM;
 if(m->ble_local_holdover)flags|=R2_UI_LOGIC_HOLD;
#ifdef R2_LORA
 if(r2_ui_lora_primary(m,now))flags|=R2_UI_LOGIC_LORA;
#endif
 return flags;
}

bool r2_ui_take_ble_toggle(void){return atomic_cas(&ble_toggle,1,0);}
int r2_ui_press(unsigned kind){
 if(kind>2)return -EINVAL;
 unsigned key=ui_take();
 int rc=status.stopped||!status.ready?-EACCES:kind&&status.button_down?-EBUSY:0;
 if(!rc&&!kind)atomic_set(&press_request,0);
 else if(!rc&&!atomic_cas(&press_request,0,(int)kind))rc=-EBUSY;
 ui_drop(key);return rc;
}
void r2_ui_menu_result(unsigned item,int result){
 unsigned key=ui_take();control_change();status.menu_result_item=(uint8_t)item;status.menu_result=result;status.menu_result_until=k_uptime_get()+1500;ui_drop(key);
}
#ifdef R2_LORA
static atomic_t lora_toggle;
bool r2_ui_take_lora_toggle(void){return atomic_cas(&lora_toggle,1,0);}
#endif
static bool started;
static uint8_t framebuffer[R2_UI_FRAME_BYTES];
static struct r2_shadow_session shadow_session;
static struct rc_shadow_population shadow_scope;
#ifdef R2_UI_CRITICAL
static struct {
 struct r2_ui_shadow_window published;
 uint32_t start_sequence,context,hint,control;
} ui_shadow_observer;
/* Sole main clock-query consumer; reused between sequential serializers. No
 * renderer/IRQ/provider can enter this scratch or retain a pointer into it. */
static union {
 struct r2_ui_critical_status critical;
 struct {struct r2_shadow_session_view view;struct r2_ui_shadow_window window;} shadow;
} ui_query_scratch;
#endif
/* Caller holds ui_lock; no additional collection or serialization work. */
static void shadow_publish_locked(const struct r2_shadow_session_view *v){
 status.shadow=*v;
#ifdef R2_UI_CRITICAL
 ui_shadow_observer.published=(struct r2_ui_shadow_window){
  shadow_session.start_tick,shadow_session.epoch,shadow_session.session,
  ui_shadow_observer.start_sequence,shadow_session.cursor,shadow_session.samples,
  ui_shadow_observer.context,ui_shadow_observer.hint,ui_shadow_observer.control};
#endif
}
static uint64_t shadow_due;
static bool shadow_cache_startup(void){
 const struct r2_shadow_settle *s=&shadow_session.startup;
 uint32_t settling=r2_shadow_settle_ms(s);
 unsigned key=ui_take();
 status.shadow.warmup=s->seen;status.shadow.warmup_skipped=s->skipped;status.shadow.warmup_missing=s->missing;
 status.shadow.warmup_resets=s->resets;status.shadow.settling_ms=settling;
 ui_drop(key);
#ifdef R2_UI_CRITICAL
 return optional_enter();
#else
 return true;
#endif
}
static bool shadow_view(struct r2_shadow_session_view *v){
 if(!shadow_session.settled&&!shadow_cache_startup())return false;
 unsigned key=ui_take();*v=status.shadow;ui_drop(key);
#ifdef R2_UI_CRITICAL
 if(!optional_enter())return false;
#endif
 r2_shadow_session_refresh(&shadow_session,v);
 return true;
}
K_THREAD_STACK_DEFINE(ui_stack,4096);
static void add(uint32_t *v){if(*v<UINT32_MAX)++*v;}
static bool name_known(const char *name){return name[0]&&strcmp(name,"--");}
#ifdef R2_GM7_NATIVE
static bool labels_equal(struct r2_ui_label_ids a,struct r2_ui_label_ids b){
 return a.root==b.root&&a.upstream==b.upstream&&a.coordinator==b.coordinator;
}
#endif
static void name_hold(char out[25],const char cached[25],uint64_t id,uint64_t cached_id){
 if(id&&id!=cached_id)return;
 if(!name_known(out))memcpy(out,cached,25);
}
void r2_ui_publish_owned(const struct r2_ui_model *m,uint64_t emitter,const struct r2_ui_label_ids *labels){
 struct r2_ui_model next=*m;
 unsigned key=ui_take();uint64_t serial=model_serial;
#ifdef R2_TZ_WIRE
 uint64_t previous_ms=model.sampled_ms;
#endif
 bool switching=model.profile_switching;
 struct r2_ui_sync_display sync=status.sync_display;
 struct r2_ui_holds holds=status.holds;
 uint32_t history=history_context;
 struct r2_ui_label_ids previous=model_labels;
 bool same_namespace=next.upstream_is_lora==model.upstream_is_lora;
 bool lora_name=next.upstream_is_lora&&name_known(next.upstream_name)&&strncmp(next.upstream_name,"Clock-",6);
 if(labels){
  /* Only actual owner-supplied LoRa names enter mandatory projection. BLE
   * names are learned by the optional provider and merged at final commit. */
  name_hold(next.root_name,model.root_name,labels->root,previous.root);
  name_hold(next.coordinator_name,model.coordinator_name,labels->coordinator,previous.coordinator);
  if(next.upstream_is_lora&&!lora_name)next.upstream_name[0]=0;
  if(same_namespace||labels->upstream)name_hold(next.upstream_name,model.upstream_name,labels->upstream,previous.upstream);
 }
 ui_drop(key);
#ifdef R2_TZ_WIRE
 uint8_t source;struct r2_tz learned=tz_unpack((uint32_t)atomic_get(&learned_tz),&source);
 r2_ui_display_tz_update(&next,learned,source,m->sampled_ms>=previous_ms);
 r2_ui_learn_tz(next.display_tz,next.display_tz_source);
#endif
 uint32_t revision=(uint32_t)atomic_get(&context_revision);
 uint32_t projection_hint=(uint32_t)atomic_get(&source_hint);
 model_fence(&next,revision,projection_hint);
 /* Alias fallback is rare: only a nonzero actual factory replacement.
  * No name formatting or registry scan occurs in the healthy50Hz merge. */
 if(labels){
  if(labels->root&&labels->root!=previous.root&&!name_known(next.root_name))r2_ui_identity_name(next.root_name,labels->root,NULL);
  if(labels->coordinator&&labels->coordinator!=previous.coordinator&&!name_known(next.coordinator_name))r2_ui_identity_name(next.coordinator_name,labels->coordinator,NULL);
  if(labels->upstream&&labels->upstream!=previous.upstream&&!name_known(next.upstream_name))r2_ui_identity_name(next.upstream_name,labels->upstream,NULL);
 }
 uint64_t now=(uint64_t)k_uptime_get();
 if(history!=revision){sync=(struct r2_ui_sync_display){0};holds.hot_since=holds.active_since=0;}
 uint32_t signature=logical_signature(&next,now);
 r2_ui_holds_update(&holds,&next,now);

 bool local=(projection_hint&R2_UI_HINT_LOCAL)!=0;
 bool recovered=local?(projection_hint&R2_UI_HINT_LOCAL_VALID)&&next.role==R2_UI_GM&&next.gm_source_fresh&&next.clock.valid:
   (projection_hint&R2_UI_HINT_BLE_VALID)&&r2_ui_ble_current(&next,next.sampled_ms);
 key=ui_take();
 /* A newer publication wins. No retry or stale-model history replay. */
 if(model_serial==serial&&m->sampled_ms>=model.sampled_ms&&
    (uint32_t)atomic_get(&context_revision)==revision&&(uint32_t)atomic_get(&source_hint)==projection_hint){
  if(switching&&!m->profile_switching&&status.menu.open)status.menu_result_until=m->sampled_ms+1500;
  if(logic_signature!=signature||model_lora_emitter!=emitter){
   if((logic_signature&2u)&&!(signature&2u)){logic_bad_at=now;logic_bad_policy=logic_revision;}
   if(logic_revision!=UINT32_MAX)++logic_revision;
#ifdef R2_UI_CRITICAL
   atomic_set(&ui_policy_revision,(atomic_val_t)logic_revision);
#endif
   logic_signature=signature;
  }
  /* Only an actual completed analog frame may create the first seed. A
   * just-observed loss may consume a prior completed seed, never an ABA one. */
  if(analog_ack.pending){
   bool same=analog_ack.context==revision;
   bool current=analog_ack.hint==projection_hint&&analog_ack.policy==logic_revision;
   bool before_loss=!(signature&2u)&&analog_ack.policy==logic_bad_policy&&
      analog_ack.completed_ms<logic_bad_at&&logic_bad_at<=now&&
      (uint32_t)atomic_get(&source_loss_revision)==projection_hint;
   if(!sync.at_ms&&same&&(current||before_loss)){
    sync=(struct r2_ui_sync_display){.utc_us=analog_ack.utc_us,.at_ms=analog_ack.at_ms};
   }
   analog_ack.pending=false;
  }
  sync_update(&sync,&next,now,false,projection_hint);
  status.sync_display=sync;status.holds=holds;history_context=revision;
  if(labels){
   struct r2_ui_label_ids resolved=*labels;
   if(!resolved.root)resolved.root=model_labels.root;
   if(!resolved.coordinator)resolved.coordinator=model_labels.coordinator;
   if(!resolved.upstream&&next.upstream_is_lora==model.upstream_is_lora)resolved.upstream=model_labels.upstream;
   if(resolved.root==model_labels.root&&name_known(model.root_name))memcpy(next.root_name,model.root_name,25);
   if(resolved.coordinator==model_labels.coordinator&&name_known(model.coordinator_name))memcpy(next.coordinator_name,model.coordinator_name,25);
   if(resolved.upstream==model_labels.upstream&&!lora_name&&name_known(model.upstream_name))memcpy(next.upstream_name,model.upstream_name,25);
   if(!resolved.upstream)next.upstream_name[0]=0;
   model_labels=resolved;
  }
  model_lora_emitter=emitter;
  model=next;model_context=(uint32_t)atomic_get(&context_revision);model_hint=(uint32_t)atomic_get(&source_hint);++model_serial;
  if(recovered&&projection_hint==model_hint)atomic_set(&source_loss_revision,0);
#ifdef R2_UI_CRITICAL
  critical_observe(now);
#endif
 }
 ui_drop(key);
}

void r2_ui_publish(const struct r2_ui_model *m){r2_ui_publish_owned(m,0,NULL);}
void r2_ui_diag_snapshot(int *error,uint32_t *max_frame_ms,uint32_t *missed){
 unsigned key=ui_take();
 *error=status.error;*max_frame_ms=status.max_frame_ms;*missed=status.missed;
 ui_drop(key);
}
void r2_ui_snapshot(struct r2_ui_status *out){
 unsigned key=ui_take();*out=status;
 uint64_t now=(uint64_t)k_uptime_get();uint64_t age=now>=model.sampled_ms?now-model.sampled_ms:UINT64_MAX;
 out->logic_age_ms=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 out->logic_flags=(uint8_t)logical_flags(&model,&status.sync_display,now);
 uint32_t logical_context=model_context,logical_hint=model_hint;
 uint32_t revision=drawn_context,hint=drawn_hint,history=history_context;ui_drop(key);
 uint32_t current=(uint32_t)atomic_get(&context_revision);
 if(logical_context!=current||current==UINT32_MAX||logical_hint!=(unsigned)atomic_get(&source_hint))
  out->logic_flags&=(uint8_t)~(R2_UI_LOGIC_VALID|R2_UI_LOGIC_BLE|R2_UI_LOGIC_GRACE|R2_UI_LOGIC_GM);
 if(revision!=current||current==UINT32_MAX||hint!=(unsigned)atomic_get(&source_hint)){
  out->analog=out->drawn_scope=out->drawn_ble=out->drawn_pps=out->drawn_map=false;
  out->drawn_time=out->drawn_sync_grace=false;out->drawn_role=R2_UI_NO_ROLE;
 }
 if(history!=current||current==UINT32_MAX){
  out->sync_display=(struct r2_ui_sync_display){0};out->holds.hot_since=out->holds.active_since=0;
 }
}
static bool shadow_page(const struct r2_ui_model *m,unsigned page){return page==R2_UI_SHADOW_PAGE||(m->profile_rc&&page==R2_UI_PRECISION_PAGE);}
static bool shadow_selected(unsigned page){
 bool rc=model.profile_rc;
 if(model_context!=(uint32_t)atomic_get(&context_revision))
  rc=((unsigned)atomic_get(&context_flags)&R2_UI_CONTEXT_RC)!=0;
 return page==R2_UI_SHADOW_PAGE||(rc&&page==R2_UI_PRECISION_PAGE);
}
int r2_ui_page(unsigned page){if(page>=R2_UI_PAGES)return -EINVAL;unsigned k=ui_take();control_change();status.page=page;input_idle_since=k_uptime_get();status.help=false;status.menu.open=false;if(r2_ui_witness_page(&model,page)){status.screensaver=false;status.screensaver_position=0;}if(!shadow_selected(page)){status.shadow.active=false;status.shadow.state=R2_SHADOW_OFF;}ui_drop(k);return 0;}
bool r2_ui_shadow_requested(void){
 unsigned key=ui_take();bool gm=model.scope_active&&model.role==R2_UI_GM;
 if(model_context!=(uint32_t)atomic_get(&context_revision)){
  unsigned flags=(unsigned)atomic_get(&context_flags);
  gm=(flags&R2_UI_CONTEXT_SERVING)&&(flags&R2_UI_CONTEXT_LIVE)&&!(flags&(R2_UI_CONTEXT_STOP|R2_UI_CONTEXT_BLE_FAULT));
 }
 bool requested=shadow_selected(status.page)&&!status.help&&!status.stopped&&!gm;
 ui_drop(key);return requested;
}
bool r2_ui_rf_requested(void){unsigned key=ui_take();bool requested=status.page==1&&status.ready&&!status.help&&!status.stopped&&!status.screensaver&&!status.menu.open;ui_drop(key);return requested;}
bool r2_ui_take_profile_toggle(void){return atomic_cas(&profile_toggle,1,0);}
void r2_ui_profile_flash(bool rc){unsigned key=ui_take();control_change();status.profile_target_rc=rc;status.profile_flash_until=k_uptime_get()+4000;ui_drop(key);}
/* Keep raw-record scratch out of the simultaneous raster/SPI stack chain. */
static void __attribute__((noinline)) shadow_tick(const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now,bool display_ok,
 uint32_t context,uint32_t hint,uint32_t control){
#ifdef R2_UI_CRITICAL
 if(shadow_session.active&&(ui_shadow_observer.context!=context||ui_shadow_observer.hint!=hint||
    ui_shadow_observer.control!=control))r2_shadow_session_stop(&shadow_session,now);
#endif
 bool fresh=now>=m->sampled_ms&&now-m->sampled_ms<=2500;
 bool visible=shadow_page(m,s->page)&&!s->help&&s->ready&&shadow_page(m,s->drawn_page)&&display_ok&&fresh&&!m->profile_switching;
 bool gm=m->scope_active&&m->role==R2_UI_GM;
 bool map_valid=m->precision.map_valid;
#ifdef R2_LORA
 map_valid=map_valid||(m->lora.choice==RC8_SELECT_LORA&&m->lora.state==RC8_AVAILABLE);
#endif
#ifdef R2_DC_PROFILE
 gm=true; /* No mesh witness session, including while GNSS is unavailable. */
#endif
 bool pps=m->clock.pps_present&&m->clock.pps_age_us>=0&&m->clock.pps_age_us<R2_PPS_FRESH_US&&
   (now-m->sampled_ms)*1000<(uint64_t)(R2_PPS_FRESH_US-m->clock.pps_age_us);
 if(s->page==R2_UI_PRECISION_PAGE)pps=pps&&m->clock.fix_active;
 unsigned previous=shadow_session.state;
 bool entered=shadow_session.entered;
 bool arm=r2_shadow_session_step(&shadow_session,visible,gm,pps,now);
 struct r2_shadow_settle *settle=&shadow_session.startup;
 if(!entered&&shadow_session.entered)memset(settle,0,sizeof *settle);
 if(!shadow_session.settled&&(!visible||gm))settle->collecting=false;
 bool collecting=!shadow_session.settled&&settle->collecting;
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
 /* UI owns normal witness collection; an actual GM is separately fenced. */
 if(!shadow_session.active&&!collecting&&!arm)r2_shadow_enable(false);
#endif
 if(arm&&!collecting){
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
  r2_shadow_enable(true);
#endif
  struct rc_shadow_record r;struct rc_shadow_status st;
  (void)r2_shadow_read(UINT32_MAX,&r,&st);
  if(st.enabled&&st.now_tick&&st.epoch){r2_shadow_settle_begin(settle,&st);collecting=true;shadow_scope.valid=false;shadow_due=now;}
 }
 bool warming=collecting&&!shadow_session.active;
 if(warming&&(!pps||!map_valid))r2_shadow_settle_reset(settle);
 bool refresh=visible&&now>=shadow_due;
 if(refresh)shadow_due=now+250;
 if(warming&&refresh){
  if(!pps||!map_valid){
   struct rc_shadow_record unused;struct rc_shadow_status st;
   (void)r2_shadow_read(UINT32_MAX,&unused,&st);
   /* Do not restart a ready streak using queued edges from before a loss. */
   if(st.epoch==settle->epoch)settle->boundary=st.now_tick;
  }
  for(unsigned i=0;i<RC_SHADOW_RECORDS;i++){
   struct rc_shadow_record r;struct rc_shadow_status st;
   if(!r2_shadow_read(settle->cursor,&r,&st))break;
   if(r.edge.epoch!=settle->epoch){r2_shadow_session_stop(&shadow_session,now);settle->collecting=false;collecting=false;break;}
   bool usable=pps&&map_valid&&r.valid&&r.reason==RC_SHADOW_OK&&r.reference_seen&&r.reference_fix&&r.reference_pps;
   if(!r2_shadow_settle_observe(settle,&r,usable))continue;
   if(!shadow_cache_startup())return;
   if(r2_shadow_session_begin(&shadow_session,(uint64_t)r.arrival_us/1000,r.edge.tick,r.edge.epoch,r.sequence)){
    /* The last warmup PPS is the exact hardware start boundary. Every later
     * eligible edge is measured, irrespective of its error magnitude. */
     rc_shadow_population_set(&shadow_scope,&r.map);
#ifdef R2_UI_CRITICAL
     ui_shadow_observer.start_sequence=r.sequence;
     /* Private until the existing complete summary publication below. */
     ui_shadow_observer.context=context;ui_shadow_observer.hint=hint;ui_shadow_observer.control=control;
#endif
     break;
   }
  }
 }
 if(collecting&&!shadow_session.active)shadow_session.state=!pps?R2_SHADOW_WAIT_PPS:!map_valid?R2_SHADOW_WAIT_MAP:R2_SHADOW_SETTLING;
 if(shadow_session.active&&refresh){
  for(unsigned i=0;i<RC_SHADOW_RECORDS;i++){
   struct rc_shadow_record r;struct rc_shadow_status st;
   if(!r2_shadow_read(shadow_session.cursor,&r,&st))break;
   if(r.edge.epoch!=shadow_session.epoch){r2_shadow_session_stop(&shadow_session,now);break;}
   bool usable=r.valid&&r.reason==RC_SHADOW_OK&&r.reference_seen&&r.reference_fix&&r.reference_pps;
    if(usable&&shadow_scope.valid&&!rc_shadow_population_same(&shadow_scope,&r.map)){
    r2_shadow_session_stop(&shadow_session,now);break;
   }
    if(usable&&!shadow_scope.valid)rc_shadow_population_set(&shadow_scope,&r.map);
   r2_shadow_session_sample(&shadow_session,r.sequence,r.edge.tick,r.edge.epoch,usable,r.phase_ns);
  }
  if(pps&&!map_valid)shadow_session.state=R2_SHADOW_WAIT_MAP;
 }
 if(refresh||previous!=shadow_session.state||arm){
  struct r2_shadow_session_view v;if(!shadow_view(&v))return;
   unsigned key=ui_take();shadow_publish_locked(&v);ui_drop(key);
 }
}
/* Constant-size GPIO/control work only. No rendering, SPI, projection,
 * calendar, sleeps, catch-up loop or model/status-sized copy in main. */
void __attribute__((noinline)) r2_ui_input_tick(uint64_t now){
#ifdef R2_UI_CRITICAL
 critical_priority_tick();
#endif
 if(!atomic_get(&input_ready)||atomic_get(&stop_requested)||now<input_due)return;
 input_due=now+20;
 int button=r2_display_button();
 if(button<0){atomic_set(&input_error,button);atomic_set(&input_ready,0);return;}
 unsigned key=ui_take();
 uint64_t gap=now-input_last_poll;input_last_poll=now;
 if(gap>status.poll_max_ms)status.poll_max_ms=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;
 unsigned before=status.page|(status.help<<8)|(status.menu.open<<9)|
  ((unsigned)status.menu.item<<10)|(status.screensaver<<16)|(status.poweroff<<17);
 int prior_stable=input_stable;
  unsigned inject=(unsigned)atomic_get(&press_request);
  if(inject&&atomic_cas(&press_request,(int)inject,0))input_virtual_until=now+(inject==1?120:2200);
  if(now<input_virtual_until)button=1;
  if(button!=input_last){input_last=button;input_changed=now;}
#if defined(R2_DC_PROFILE) || defined(R2_PC_PROFILE)
  if(now-input_changed>=50&&input_stable!=input_last){
   input_stable=input_last;input_idle_since=now;
   if(input_stable){input_pressed=now;input_wake_press=status.screensaver;status.screensaver=false;if(input_wake_press)add(&status.buttons);input_held=input_wake_press;}
   else {if(!input_held&&!input_wake_press){if(status.help)status.help=false;else status.page=(status.page+1)%R2_UI_PAGES;add(&status.buttons);}input_wake_press=false;}
  }
  if(input_stable)input_idle_since=now;
  if(input_stable&&!input_held&&now-input_pressed>=1500){input_held=true;
   if(status.page==0&&!status.help&&model.profile_supported){atomic_set(&profile_toggle,1);add(&status.profile_toggles);status.profile_target_rc=!model.profile_rc;status.profile_flash_until=now+4000;}
   else {status.help=true;}
   }

  if(input_stable&&!input_wake_press&&now-input_pressed>=4000&&!atomic_get(&profile_toggle)&&!(status.page==0&&model.profile_supported)){status.poweroff=true;atomic_set(&poweroff_request,1);}
#else
  if(now-input_changed>=50)input_stable=input_last;

  enum r2_ui_menu_action action=r2_ui_menu_sample_help(&status.menu,&input_button,button!=0,input_stable!=0,status.screensaver,r2_ui_menu_enabled(&model),status.page==1&&!status.help,input_changed,now);
  status.button_down=input_stable!=0;
  if(input_stable){status.screensaver=false;input_idle_since=now;}
  if(action!=R2_MENU_NONE){
   add(&status.buttons);input_idle_since=now;
   if(action==R2_MENU_HELP)status.help=true;
   else if(action==R2_MENU_PAGE){if(status.help)status.help=false;else status.page=(status.page+1)%R2_UI_PAGES;}
   else {status.help=false;}
   if(action==R2_MENU_OFF){status.poweroff=true;atomic_set(&poweroff_request,1);}
   else if(action==R2_MENU_PROFILE||action==R2_MENU_BLE_TOGGLE||action==R2_MENU_LORA_TOGGLE){
    status.menu_result_item=status.menu.item;status.menu_result_until=now+1500;
    status.menu_result=(!model.platform_ready||model.profile_switching||((unsigned)atomic_get(&context_flags)&(R2_UI_CONTEXT_SWITCH|R2_UI_CONTEXT_STOP)))?-EBUSY:0;
    if(!status.menu_result){
     if(action==R2_MENU_PROFILE){atomic_set(&profile_toggle,1);add(&status.profile_toggles);}
     else if(action==R2_MENU_BLE_TOGGLE)atomic_set(&ble_toggle,1);
#ifdef R2_LORA
     else atomic_set(&lora_toggle,1);
#endif
    }
   }
  }

#endif

 unsigned after=status.page|(status.help<<8)|(status.menu.open<<9)|
  ((unsigned)status.menu.item<<10)|(status.screensaver<<16)|(status.poweroff<<17);
 if(before!=after||prior_stable!=input_stable){
  control_change();atomic_set(&model_requested,1);
 }
 ui_drop(key);
}
#ifdef R2_GM7_NATIVE
static bool detail_scope(const struct r2_service_status *s,uint32_t revision){
 unsigned key=ui_take();struct r2_ui_context c=published_context;ui_drop(key);
 return (uint32_t)atomic_get(&context_revision)==revision&&revision!=UINT32_MAX&&
  s->profile_incarnation==c.incarnation&&
  (s->root<4?s->identities[s->root]:0)==c.root&&s->root_boot==c.boot&&
  (s->serving?s->local_source_epoch:s->source_epoch)==c.source_epoch&&
  s->precision.train==c.train&&s->profile_rc==!!(c.flags&R2_UI_CONTEXT_RC)&&
  s->serving==!!(c.flags&R2_UI_CONTEXT_SERVING)&&
  s->profile_pending==!!(c.flags&R2_UI_CONTEXT_SWITCH);
}
#if defined(R2_GM7_NATIVE) && (defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST))
/* Two independent clock owners, one capture. No clock/selection feedback. */
#ifdef R2_UI_CRITICAL
static struct {struct r2_ui_rf_comparison value;uint32_t context,hint;} ui_rf_optional_cache;
#endif
static void __attribute__((noinline)) optional_rf_view(struct r2_ui_model *m){
#ifdef R2_UI_CRITICAL
 uint32_t revision=(uint32_t)atomic_get(&context_revision),hint=(uint32_t)atomic_get(&source_hint);
 uint64_t kernel_now=(uint64_t)k_uptime_get()*1000;
 m->rf_comparison=ui_rf_optional_cache.context==revision&&ui_rf_optional_cache.hint==hint&&
  kernel_now>=ui_rf_optional_cache.value.sampled_us&&kernel_now<ui_rf_optional_cache.value.until_us?
  ui_rf_optional_cache.value:(struct r2_ui_rf_comparison){0};
#else
 m->rf_comparison=(struct r2_ui_rf_comparison){0};
#endif
 bool requested=r2_ui_rf_requested();
#ifdef R2_UI_CRITICAL
 if(!optional_enter())return;
#endif
 if(!requested||m->lora.transmitter)return;
 struct r2_capture at;uint64_t now;
 if(!r2_capture_now_at(&at,&now))return;
 struct r2_rf_clock_sample ble={0},lora={0};
#ifdef R2_UI_CRITICAL
 if(!optional_owner_enter()||!r2_service_rf_clock_try(&ble,at,now,read_boundary,NULL)||!optional_enter())return;
 /* This reader's existing IRQ-masked spin copy has no mutex PI restore or
  * explicit scheduler call. Keep it unchanged; guard immediately on return. */
 r2_lora_rf_clock(&lora,at,now);if(!optional_enter())return;
#else
 r2_service_rf_clock(&ble,at,now);r2_lora_rf_clock(&lora,at,now);
#endif
 if(!ble.available||!lora.available||!ble.fresh||!lora.fresh||!ble.utc||!lora.utc||
    !ble.root||!ble.boot||!lora.root||!lora.boot||ble.time_ns>INT64_MAX||lora.time_ns>INT64_MAX)return;
 uint64_t until=ble.fresh_until_us<lora.fresh_until_us?ble.fresh_until_us:lora.fresh_until_us;
 if(now>=until)return;
 m->rf_comparison=(struct r2_ui_rf_comparison){.sampled_us=now,.until_us=until,
  .delta_ns=(int64_t)ble.time_ns-(int64_t)lora.time_ns,
  .drift_ppb=(int64_t)ble.rate_ppb-lora.rate_ppb,
  .flags=R2_UI_RF_DELTA|(ble.rate_valid&&lora.rate_valid?R2_UI_RF_RATE:0)|
   (ble.provisional||lora.provisional?R2_UI_RF_ESTIMATE:0)};
#ifdef R2_UI_CRITICAL
 if(revision==(uint32_t)atomic_get(&context_revision)&&hint==(uint32_t)atomic_get(&source_hint)){
  ui_rf_optional_cache.value=m->rf_comparison;ui_rf_optional_cache.context=revision;ui_rf_optional_cache.hint=hint;
 }else m->rf_comparison=(struct r2_ui_rf_comparison){0};
#endif
}
#endif

/* Optional details live only in the render scratch. They cannot publish a
 * logical stamp, deadline, source, hold or history back to main's model. */
static bool supplied_name(const char name[25],uint64_t id,const struct r2_service_status *s){
 if(!id||!name_known(name))return false;
 if(s->self<4&&s->identities[s->self]==id&&s->name[0]&&strncmp(s->name,"Clock-",6)&&!strcmp(name,s->name))return true;
 for(unsigned i=0;i<4;i++)if(s->identities[i]==id&&s->peer_names[i][0]&&
    strncmp(s->peer_names[i],"Clock-",6)&&!strcmp(name,s->peer_names[i]))return true;
 return false;
}
static void cache_details(struct r2_ui_model *m,const struct r2_service_status *service,
 uint32_t revision,unsigned hint,struct r2_ui_label_ids ids){
 unsigned supplied=(supplied_name(m->root_name,ids.root,service)?1u:0)|
  (supplied_name(m->upstream_name,ids.upstream,service)?2u:0)|
  (supplied_name(m->coordinator_name,ids.coordinator,service)?4u:0);
 unsigned key=ui_take();
 bool current=model_context==revision&&(uint32_t)atomic_get(&context_revision)==revision&&
  revision!=UINT32_MAX&&model_hint==hint&&(unsigned)atomic_get(&source_hint)==hint&&
  labels_equal(ids,model_labels)&&m->upstream_is_lora==model.upstream_is_lora;
 if(current){
  if(supplied&1u)memcpy(model.root_name,m->root_name,25);
  if((supplied&2u)&&!m->upstream_is_lora)memcpy(model.upstream_name,m->upstream_name,25);
  if(supplied&4u)memcpy(model.coordinator_name,m->coordinator_name,25);
  memcpy(m->root_name,model.root_name,25);memcpy(m->upstream_name,model.upstream_name,25);
  memcpy(m->coordinator_name,model.coordinator_name,25);
 }
 ui_drop(key);
}
static bool __attribute__((noinline)) optional_details(struct r2_ui_model *m,
 uint32_t revision,unsigned hint,uint64_t emitter){
 unsigned key=ui_take();struct r2_ui_label_ids ids=model_labels;ui_drop(key);
#ifdef R2_UI_CRITICAL
 if(!optional_enter())return false;
#endif
 struct r2_service_status service;
#ifdef R2_UI_CRITICAL
 if(!optional_owner_enter()||!r2_service_snapshot_try(&service,read_boundary,NULL))return false;
#else
 r2_service_snapshot(&service);
#endif
 if(!detail_scope(&service,revision)||(unsigned)atomic_get(&source_hint)!=hint||
    !r2_ui_work_allowed(true,service.profile_pending,service.closing,service.supervision.state,
      service.backlog,service.relay_pending,service.takeover_pending))return false;
#ifdef R2_UI_CRITICAL
 if(!optional_enter())return false;
#endif
 (void)emitter;
#ifdef R2_UI_CRITICAL
 if(!optional_owner_enter()||!r2_service_ui_details_try(m,&service,read_boundary,NULL))return false;
 if(!optional_enter())return false;
#else
 r2_service_ui_details(m,&service);
#endif
 cache_details(m,&service,revision,hint,ids);
#ifdef R2_UI_CRITICAL
 return optional_enter();
#else
 return true;
#endif
}
#endif
#ifdef R2_UI_CRITICAL
#include "ui_critical.inc"
#else
static void worker(void *a,void *b,void *c)
{
 k_sleep(K_MSEC(20));int rc=atomic_get(&stop_requested)?0:r2_display_init();
 bool initialized=!rc;int terminal_error=rc;bool draw_failed=false,frame_returned=false;
 unsigned observed_page;
 unsigned key=ui_take();status.error=rc;observed_page=status.page;ui_drop(key);
 uint64_t due=k_uptime_get(),saver_since=0,saver_period=0;
 key=ui_take();input_changed=input_last_poll=input_idle_since=due;
 uint32_t observed_control=control_serial;ui_drop(key);
 /* Match original GPIO sampling lifetime even if LCD init failed. */
 atomic_set(&input_ready,atomic_get(&stop_requested)?0:1);
 while(!atomic_get(&stop_requested)){
  uint64_t now=k_uptime_get();
  int input_rc=atomic_get(&input_error);if(input_rc){terminal_error=rc=input_rc;break;}
  /* Share priority9 with the GM owner at explicit work boundaries. Main,
   * precision, supervisor and urgent LoRa drain still preempt this worker. */
  k_yield();
  /* BLE may delay this low-priority worker beyond a frame deadline. Preserve
   * the failed result and buffer until SPI reports completion; then drop that
   * frame and retry at the ordinary cadence, without extending its deadline. */
  if(rc&&initialized&&!r2_display_recover()){rc=0;draw_failed=false;}
  struct r2_ui_model m;struct r2_ui_status s;
  key=ui_take();m=model;uint32_t shadow_revision=model_context,shadow_published_hint=model_hint;uint32_t shadow_control=control_serial;s=status;
  uint64_t idle_since=input_idle_since;int stable=input_stable;bool wake_press=input_wake_press;
  ui_drop(key);
  if(shadow_control!=observed_control){observed_control=shadow_control;due=now;}
   uint32_t shadow_live_revision=(uint32_t)atomic_get(&context_revision);
   model_fence(&m,shadow_revision,shadow_published_hint);
   if(shadow_live_revision!=shadow_revision){
    s.sync_display=(struct r2_ui_sync_display){0};
    s.holds.hot_since=s.holds.active_since=0;
   }
  /* A publisher can preempt button/recovery work after the loop timestamp.
   * Evaluate this immutable snapshot against time sampled after its copy. */
  now=k_uptime_get();
  unsigned interval=s.page==1&&!s.screensaver?125:250;
  if((unsigned)atomic_get(&model_interval)!=interval)atomic_set(&model_interval,(int)interval);
   bool page_changed=s.page!=observed_page;
  if(page_changed){observed_page=s.page;atomic_set(&model_requested,1);}
  /* Every page may idle into the saver. Its overlay does not end the
   * selected measurement page's independent witness session. */
  bool saver=!s.menu.open&&!stable&&!wake_press&&now>=idle_since&&now-idle_since>=R2_UI_IDLE_MS&&r2_ui_saver_allowed(&m,now);
  uint64_t period=saver&&now>=saver_since?(now-saver_since)/R2_UI_SAVER_MOVE_MS:0;
  bool move=saver&&s.screensaver&&period!=saver_period;
  /* Existing public PRNG, outside the UI lock; no draw-time RNG or RF work. */
  uint32_t random=move?sys_rand32_get():0;
  key=ui_take();
  if(control_serial==shadow_control){
   if(status.screensaver&&!saver&&!stable&&!wake_press&&!page_changed){status.page=0;status.help=false;idle_since=now;}
   if(saver!=status.screensaver){status.screensaver=saver;saver_since=now;saver_period=0;status.screensaver_position=0;due=now;}
   if(move){status.screensaver_position=(uint8_t)r2_ui_saver_next(status.screensaver_position,random);saver_period=period;due=now;}
   if(!saver)status.screensaver_position=0;
   input_idle_since=idle_since;
  }
  s=status;ui_drop(key);
   if(shadow_live_revision!=shadow_revision){
    s.sync_display=(struct r2_ui_sync_display){0};
    s.holds.hot_since=s.holds.active_since=0;
   }
  /* Histories are maintained by main. Pixels still wake at the original
   * grace deadline even when ordinary frame cadence would be later. */
  if(s.sync_display.loss_ms){
   uint64_t deadline=s.sync_display.loss_ms+500;
   if(now>=deadline)due=now;else if(deadline<due)due=deadline;
  }
  shadow_tick(&m,&s,now,!rc,shadow_revision,shadow_published_hint,shadow_control);
  if(!rc&&now>=due){
   /* Shadow consumption is synchronous and retained no pointers. Reuse its
    * scratch pair, then take the same fresh draw snapshot and generation tags. */
   key=ui_take();m=model;uint32_t policy=logic_revision;uint64_t emitter=model_lora_emitter;uint32_t published_revision=model_context,published_hint=model_hint,control=control_serial;s=status;ui_drop(key);
   uint32_t revision=(uint32_t)atomic_get(&context_revision);unsigned hint=(unsigned)atomic_get(&source_hint);model_fence(&m,published_revision,published_hint);
   now=k_uptime_get();
   uint64_t grace_deadline=s.sync_display.loss_ms?s.sync_display.loss_ms+500:0;
#ifdef R2_LORA
   transmitter_age(&m,now,s.page==1&&!s.screensaver?125:250);
#endif
#ifdef R2_GM7_NATIVE
   bool details=optional_details(&m,revision,hint,emitter);
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
   if(details)optional_rf_view(&m);
#else
   (void)details;
#endif
#else
   (void)emitter;
#endif
   s.screensaver=s.screensaver&&r2_ui_saver_allowed(&m,now);
   m.temperature_valid=!r2_display_temperature(&m.temperature_qc);
   r2_ui_render(framebuffer,&m,&s,now);
   /* Let a queued peer owner finish before taking a fresh validity decision.
    * This yield is outside SPI/DMA ownership and starts no new deadline. */
   k_yield();
   /* Never submit obsolete pixels. No DMA storage is touched while owned by
    * SPI; an already-submitted frame still retires under its original deadline. */
   if(!frame_current(policy,revision,s.page,control,hint)){
    key=ui_take();add(&status.missed);ui_drop(key);
    unsigned cadence=s.page==1&&!s.screensaver?125:250;
    due=k_uptime_get()+cadence;
    if(grace_deadline&&grace_deadline<due)due=grace_deadline;
    goto ui_sleep;
   }
   rc=r2_display_frame(framebuffer);frame_returned=true;
   if(!rc)rc=r2_display_brightness(s.screensaver?50:100);
   draw_failed=rc!=0;
#ifdef R2_TZ_WIRE
   /* Calendar lookup stays outside the publication IRQ fence. */
   int64_t zone_utc=0;bool zone_coarse;
   bool zone_valid=r2_ui_display_time(&m,now,&zone_utc,&zone_coarse);
   if(r2_ui_sync_display_time(&s.sync_display,&m,now,&zone_utc))zone_valid=true;
   uint8_t zone_flags;struct r2_tz zone=r2_ui_display_zone(&m,now,zone_utc,&zone_flags);
   if(!zone_valid)zone_flags&=(uint8_t)~R2_UI_TZ_LOCAL_VALID;
#endif
   uint64_t end=k_uptime_get();
   if(!rc){
    /* Everything potentially expensive is evaluated before the short commit. */
    s.drawn_menu=s.menu.open;s.drawn_screensaver=s.screensaver;s.drawn_screensaver_position=s.screensaver_position;
    memset(s.drawn_lamps,0,sizeof s.drawn_lamps);
    if(s.page==1&&!s.screensaver&&!s.menu.open)r2_ui_rf_lamps(s.drawn_lamps,&m,now);
    s.analog=!s.menu.open&&r2_ui_face_locked(&m,now);
    bool fresh=now>=m.sampled_ms&&now-m.sampled_ms<=2500&&(!m.ble_time_selected||r2_ui_ble_current(&m,now));
    s.drawn_scope=fresh&&m.scope_active;s.drawn_role=s.drawn_scope?m.role:R2_UI_NO_ROLE;
    s.drawn_ble=fresh&&r2_ui_ble_current(&m,now);s.drawn_pps=fresh&&m.follower_pps_valid;s.drawn_map=fresh&&m.precision.map_valid;
    s.drawn_time=!s.menu.open&&r2_ui_display_time(&m,now,&s.drawn_utc_us,&s.drawn_coarse);
    s.drawn_sync_grace=!s.menu.open&&r2_ui_sync_display_time(&s.sync_display,&m,now,&s.drawn_utc_us);
    if(s.drawn_sync_grace)s.drawn_time=s.drawn_coarse=true;
   }
   key=ui_take();
   bool current=policy!=UINT32_MAX&&logic_revision==policy&&model_context==published_revision&&control_serial==control&&status.page==s.page&&
    (uint32_t)atomic_get(&context_revision)==revision&&(unsigned)atomic_get(&source_hint)==hint;
   if(!rc){
    add(&status.frames);status.ready=true;status.drawn_page=s.page;status.last_frame_ms=(uint32_t)end;status.drawn_model_ms=m.sampled_ms;
    status.drawn_menu=s.drawn_menu;status.drawn_screensaver=s.drawn_screensaver;status.drawn_screensaver_position=s.drawn_screensaver_position;
    memcpy(status.drawn_lamps,s.drawn_lamps,sizeof status.drawn_lamps);
    status.analog=current&&s.analog;status.drawn_scope=current&&s.drawn_scope;
    status.drawn_role=status.drawn_scope?s.drawn_role:R2_UI_NO_ROLE;
    status.drawn_ble=current&&s.drawn_ble;status.drawn_pps=current&&s.drawn_pps;status.drawn_map=current&&s.drawn_map;
    /* Frame pixels describe the captured instant. A renewed same-source
     * owner lease can keep logical time valid without extending the old copy.
     * Grace seeding below uses the current owned clock, never the old lease. */
    uint64_t logical_now=(uint64_t)k_uptime_get();
    bool live_time=!!(logical_flags(&model,&status.sync_display,logical_now)&R2_UI_LOGIC_VALID);
    status.drawn_time=current&&live_time&&s.drawn_time;status.drawn_coarse=s.drawn_coarse;status.drawn_utc_us=s.drawn_utc_us;status.drawn_sync_grace=current&&s.drawn_sync_grace;
    if(current&&s.analog&&s.page==0&&!s.help&&!s.menu.open&&r2_ui_analog(&model,logical_now)){
     int64_t utc;bool coarse;
     if(r2_ui_display_time(&model,logical_now,&utc,&coarse)){
      analog_ack.utc_us=utc;analog_ack.at_ms=logical_now;analog_ack.completed_ms=end;
      analog_ack.context=revision;analog_ack.hint=hint;analog_ack.policy=policy;analog_ack.pending=true;
     }
    }
    drawn_context=revision;drawn_hint=hint;
#ifdef R2_TZ_WIRE
    status.drawn_tz=zone;status.drawn_tz_flags=zone_flags;
#endif
   }
   uint32_t elapsed=(uint32_t)(end-now);if(elapsed>status.max_frame_ms)status.max_frame_ms=elapsed;
   unsigned cadence=s.page==1&&!s.screensaver?125:250;
   if(end>=due+cadence)add(&status.missed);
   if(!rc)status.brightness_percent=s.screensaver?50:100;
   status.temperature_qc=m.temperature_qc;status.temperature_valid=m.temperature_valid;
   status.error=rc;if(rc)status.ready=false;ui_drop(key);
   /* Drop missed refreshes; never queue catch-up frames. */
   due=end>=due+cadence?end+cadence:due+cadence;
   if(grace_deadline&&grace_deadline<due)due=grace_deadline;
  }
ui_sleep:
  ;uint64_t sleep_now=k_uptime_get();
  k_sleep(K_MSEC(due>sleep_now&&due-sleep_now<20?due-sleep_now:20));
 }
 atomic_set(&input_ready,0);
 /* Public SPI completion/abort precedes buffer reuse and display shutdown. */
 r2_shadow_session_stop(&shadow_session,k_uptime_get());
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
 r2_shadow_enable(false);
#endif
 struct r2_shadow_session_view final_shadow;shadow_view(&final_shadow);
 key=ui_take();status.shadow=final_shadow;ui_drop(key);
 /* A returned public synchronous frame owns no live DMA. A prior optional
  * draw failure is observational, not proof that closure failed. Discard it
  * from close_result only after affirmative CS recovery and PWM-off. */
 int released=0;
 if(draw_failed){
  released=(!initialized||!frame_returned)?(rc?rc:-EIO):r2_display_recover();
 }
 int closed=r2_display_off();int result=terminal_error?terminal_error:released?released:closed;
 key=ui_take();
 /* Preserve the last draw error separately from actual release proof. */
 if(terminal_error)status.error=terminal_error;else if(!status.error&&result)status.error=result;
 status.stopped=true;status.ready=false;ui_drop(key);atomic_set(&close_state,result);
}
#endif
void r2_ui_start(void){if(started)return;started=true;atomic_set(&model_requested,1);atomic_set(&close_state,-EAGAIN);if(atomic_get(&stop_requested)){unsigned k=ui_take();status.stopped=true;ui_drop(k);atomic_set(&close_state,0);return;}k_thread_create(&thread,ui_stack,K_THREAD_STACK_SIZEOF(ui_stack),worker,NULL,NULL,NULL,9,0,K_NO_WAIT);k_thread_name_set(&thread,"r2_ui");}
void r2_ui_stop(void){
#ifdef R2_UI_CRITICAL
 (void)atomic_cas(&stop_requested,0,1);
#else
 atomic_set(&stop_requested,1);
#endif
}
int r2_ui_close_result(void){
#ifdef R2_UI_CRITICAL
 critical_priority_tick();
#endif
 return atomic_get(&close_state);}

bool r2_ui_poweroff_requested(void){return atomic_get(&poweroff_request)!=0;}

uint32_t r2_ui_stack_unused(void)
{
#ifdef CONFIG_INIT_STACKS
 size_t unused;if(started&&!k_thread_stack_space_get(&thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}
