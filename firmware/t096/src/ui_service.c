/* SPDX-License-Identifier: Apache-2.0 */
#include "ui.h"
#include "rc_shadow.h"
#include "shadow_settle.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/random/random.h>
#include <errno.h>
#define R2_LOCK_UNIT 12u
#include "health_lock.h"
static struct r2_ui_model model;
static struct r2_ui_status status;
static struct k_spinlock lock;
static atomic_t stop_requested,profile_toggle;
static bool started;
static uint8_t framebuffer[R2_UI_FRAME_BYTES];
static struct r2_shadow_session shadow_session;
static struct {uint64_t root,root_boot,parent,parent_boot;uint32_t root_epoch;bool valid;} shadow_scope;
static uint64_t shadow_due;
static void shadow_cache_startup(void){
 const struct r2_shadow_settle *s=&shadow_session.startup;
 k_spinlock_key_t key=k_spin_lock(&lock);
 status.shadow.warmup=s->seen;status.shadow.warmup_skipped=s->skipped;status.shadow.warmup_missing=s->missing;
 status.shadow.warmup_resets=s->resets;status.shadow.settling_ms=r2_shadow_settle_ms(s);
 k_spin_unlock(&lock,key);
}
static void shadow_view(struct r2_shadow_session_view *v){
 if(!shadow_session.settled)shadow_cache_startup();
 uint32_t startup[5];k_spinlock_key_t key=k_spin_lock(&lock);
 startup[0]=status.shadow.warmup;startup[1]=status.shadow.warmup_skipped;startup[2]=status.shadow.warmup_missing;
 startup[3]=status.shadow.warmup_resets;startup[4]=status.shadow.settling_ms;k_spin_unlock(&lock,key);
 r2_shadow_session_view(&shadow_session,v);
 v->warmup=startup[0];v->warmup_skipped=startup[1];v->warmup_missing=startup[2];v->warmup_resets=startup[3];v->settling_ms=startup[4];
}
K_THREAD_STACK_DEFINE(ui_stack,4096);
static struct k_thread thread;
static void add(uint32_t *v){if(*v<UINT32_MAX)++*v;}
void r2_ui_publish(const struct r2_ui_model *m){k_spinlock_key_t k=k_spin_lock(&lock);r2_ui_holds_update(&status.holds,&model,m->sampled_ms);model=*m;r2_ui_holds_update(&status.holds,m,m->sampled_ms);k_spin_unlock(&lock,k);}
void r2_ui_snapshot(struct r2_ui_status *out){k_spinlock_key_t k=k_spin_lock(&lock);*out=status;k_spin_unlock(&lock,k);}
int r2_ui_page(unsigned page){if(page>=R2_UI_PAGES)return -EINVAL;k_spinlock_key_t k=k_spin_lock(&lock);status.page=page;status.help=false;if(r2_ui_witness_page(&model,page)){status.screensaver=false;status.screensaver_position=0;}else{status.shadow.active=false;status.shadow.state=R2_SHADOW_OFF;}k_spin_unlock(&lock,k);return 0;}
bool r2_ui_shadow_requested(void){k_spinlock_key_t key=k_spin_lock(&lock);bool requested=r2_ui_witness_page(&model,status.page)&&!status.help&&!status.stopped&&!(model.scope_active&&model.role==R2_UI_GM);k_spin_unlock(&lock,key);return requested;}
bool r2_ui_take_profile_toggle(void){return atomic_cas(&profile_toggle,1,0);}
void r2_ui_profile_flash(bool rc){k_spinlock_key_t key=k_spin_lock(&lock);status.profile_target_rc=rc;status.profile_flash_until=k_uptime_get()+4000;k_spin_unlock(&lock,key);}
static void shadow_tick(const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now,bool display_ok){
 bool fresh=now>=m->sampled_ms&&now-m->sampled_ms<=2500;
 bool visible=r2_ui_witness_page(m,s->page)&&!s->help&&s->ready&&r2_ui_witness_page(m,s->drawn_page)&&display_ok&&fresh&&!m->profile_switching;
 bool gm=m->scope_active&&m->role==R2_UI_GM;
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
 if(warming&&(!pps||!m->precision.map_valid))r2_shadow_settle_reset(settle);
 bool refresh=visible&&now>=shadow_due;
 if(refresh)shadow_due=now+250;
 if(warming&&refresh){
  if(!pps||!m->precision.map_valid){
   struct rc_shadow_record unused;struct rc_shadow_status st;
   (void)r2_shadow_read(UINT32_MAX,&unused,&st);
   /* Do not restart a ready streak using queued edges from before a loss. */
   if(st.epoch==settle->epoch)settle->boundary=st.now_tick;
  }
  for(unsigned i=0;i<RC_SHADOW_RECORDS;i++){
   struct rc_shadow_record r;struct rc_shadow_status st;
   if(!r2_shadow_read(settle->cursor,&r,&st))break;
   if(r.edge.epoch!=settle->epoch){r2_shadow_session_stop(&shadow_session,now);settle->collecting=false;collecting=false;break;}
   bool usable=pps&&m->precision.map_valid&&r.valid&&r.reason==RC_SHADOW_OK&&r.reference_seen&&r.reference_fix&&r.reference_pps;
   if(!r2_shadow_settle_observe(settle,&r,usable))continue;
   shadow_cache_startup();
   if(r2_shadow_session_begin(&shadow_session,(uint64_t)r.arrival_us/1000,r.edge.tick,r.edge.epoch,r.sequence)){
    /* The last warmup PPS is the exact hardware start boundary. Every later
     * eligible edge is measured, irrespective of its error magnitude. */
    shadow_scope.root=r.map.root;shadow_scope.root_boot=r.map.root_boot;shadow_scope.root_epoch=r.map.root_epoch;
    shadow_scope.parent=r.map.parent;shadow_scope.parent_boot=r.map.parent_boot;shadow_scope.valid=true;break;
   }
  }
 }
 if(collecting&&!shadow_session.active)shadow_session.state=!pps?R2_SHADOW_WAIT_PPS:!m->precision.map_valid?R2_SHADOW_WAIT_MAP:R2_SHADOW_SETTLING;
 if(shadow_session.active&&refresh){
  for(unsigned i=0;i<RC_SHADOW_RECORDS;i++){
   struct rc_shadow_record r;struct rc_shadow_status st;
   if(!r2_shadow_read(shadow_session.cursor,&r,&st))break;
   if(r.edge.epoch!=shadow_session.epoch){r2_shadow_session_stop(&shadow_session,now);break;}
   bool usable=r.valid&&r.reason==RC_SHADOW_OK&&r.reference_seen&&r.reference_fix&&r.reference_pps;
   if(usable&&shadow_scope.valid&&(shadow_scope.root!=r.map.root||shadow_scope.root_boot!=r.map.root_boot||shadow_scope.root_epoch!=r.map.root_epoch||shadow_scope.parent!=r.map.parent||shadow_scope.parent_boot!=r.map.parent_boot)){
    r2_shadow_session_stop(&shadow_session,now);break;
   }
   if(usable&&!shadow_scope.valid){shadow_scope.root=r.map.root;shadow_scope.root_boot=r.map.root_boot;shadow_scope.root_epoch=r.map.root_epoch;shadow_scope.parent=r.map.parent;shadow_scope.parent_boot=r.map.parent_boot;shadow_scope.valid=true;}
   r2_shadow_session_sample(&shadow_session,r.sequence,r.edge.tick,r.edge.epoch,usable,r.phase_ns);
  }
  if(pps&&!m->precision.map_valid)shadow_session.state=R2_SHADOW_WAIT_MAP;
 }
 if(refresh||previous!=shadow_session.state||arm){
  struct r2_shadow_session_view v;shadow_view(&v);
  k_spinlock_key_t key=k_spin_lock(&lock);status.shadow=v;k_spin_unlock(&lock,key);
 }
}
static void worker(void *a,void *b,void *c)
{
 k_sleep(K_MSEC(20));int rc=atomic_get(&stop_requested)?0:r2_display_init();
 bool initialized=!rc;
 unsigned observed_page;
 k_spinlock_key_t key=k_spin_lock(&lock);status.error=rc;observed_page=status.page;k_spin_unlock(&lock,key);
 uint64_t due=k_uptime_get(),changed=due,pressed=0,last_poll=due,idle_since=due,saver_since=0,saver_period=0;int last=0,stable=0;bool held=false,wake_press=false;
 while(!atomic_get(&stop_requested)){
  uint64_t now=k_uptime_get();uint64_t gap=now-last_poll;last_poll=now;
  key=k_spin_lock(&lock);if(gap>status.poll_max_ms)status.poll_max_ms=gap>UINT32_MAX?UINT32_MAX:(uint32_t)gap;k_spin_unlock(&lock,key);
  int button=r2_display_button();
  if(button<0){rc=button;break;}
  if(button!=last){last=button;changed=now;}
  if(now-changed>=50&&stable!=last){
   stable=last;idle_since=now;
   if(stable){pressed=now;key=k_spin_lock(&lock);wake_press=status.screensaver;status.screensaver=false;if(wake_press)add(&status.buttons);k_spin_unlock(&lock,key);held=wake_press;due=now;}
   else {if(!held&&!wake_press){key=k_spin_lock(&lock);if(status.help)status.help=false;else status.page=(status.page+1)%R2_UI_PAGES;add(&status.buttons);k_spin_unlock(&lock,key);}wake_press=false;}
  }
  if(stable)idle_since=now;
  if(stable&&!held&&now-pressed>=1500){held=true;key=k_spin_lock(&lock);
   if(status.page==0&&!status.help&&model.profile_supported){atomic_set(&profile_toggle,1);add(&status.profile_toggles);status.profile_target_rc=!model.profile_rc;status.profile_flash_until=now+4000;}
   else {status.help=true;}
   k_spin_unlock(&lock,key);}

  if(stable&&!wake_press&&now-pressed>=4000&&!atomic_get(&profile_toggle)&&!(status.page==0&&model.profile_supported)){key=k_spin_lock(&lock);status.poweroff=true;k_spin_unlock(&lock,key);}
  /* BLE may delay this low-priority worker beyond a frame deadline. Preserve
   * the failed result and buffer until SPI reports completion; then drop that
   * frame and retry at the ordinary cadence, without extending its deadline. */
  if(rc&&initialized&&!r2_display_recover())rc=0;
  struct r2_ui_model shadow_model;struct r2_ui_status shadow_status;
  key=k_spin_lock(&lock);shadow_model=model;shadow_status=status;k_spin_unlock(&lock,key);
  bool page_changed=shadow_status.page!=observed_page;
  if(page_changed){observed_page=shadow_status.page;idle_since=now;}
  /* Every page may idle into the saver. Its overlay does not end the
   * selected measurement page's independent witness session. */
  bool saver=!stable&&!wake_press&&now>=idle_since&&now-idle_since>=R2_UI_IDLE_MS&&r2_ui_saver_allowed(&shadow_model,now);
  uint64_t period=saver&&now>=saver_since?(now-saver_since)/R2_UI_SAVER_MOVE_MS:0;
  bool move=saver&&shadow_status.screensaver&&period!=saver_period;
  /* Existing public PRNG, outside the UI lock; no draw-time RNG or RF work. */
  uint32_t random=move?sys_rand32_get():0;
  key=k_spin_lock(&lock);
  if(status.screensaver&&!saver&&!stable&&!wake_press&&!page_changed){status.page=0;status.help=false;idle_since=now;}
  if(saver!=status.screensaver){status.screensaver=saver;saver_since=now;saver_period=0;status.screensaver_position=0;due=now;}
  if(move){status.screensaver_position=(uint8_t)r2_ui_saver_next(status.screensaver_position,random);saver_period=period;due=now;}
  if(!saver)status.screensaver_position=0;
  uint64_t sync_due=status.sync_display.loss_ms?status.sync_display.loss_ms+500:0;
  r2_ui_sync_display_update(&status.sync_display,&shadow_model,now,false);
  r2_ui_holds_update(&status.holds,&shadow_model,now);
  if(status.sync_display.loss_ms&&status.sync_display.loss_ms+500<due)due=status.sync_display.loss_ms+500;
  if(sync_due&&now>=sync_due)due=now;
  shadow_status=status;k_spin_unlock(&lock,key);
  shadow_tick(&shadow_model,&shadow_status,now,!rc);
  if(!rc&&now>=due){
   struct r2_ui_model m;struct r2_ui_status s;key=k_spin_lock(&lock);m=model;r2_ui_sync_display_update(&status.sync_display,&m,now,false);s=status;k_spin_unlock(&lock,key);
   s.screensaver=s.screensaver&&r2_ui_saver_allowed(&m,now);
   m.temperature_valid=!r2_display_temperature(&m.temperature_qc);
   r2_ui_render(framebuffer,&m,&s,now);rc=r2_display_frame(framebuffer);
   if(!rc)rc=r2_display_brightness(s.screensaver?50:100);
   uint64_t end=k_uptime_get();key=k_spin_lock(&lock);
   if(!rc){
    r2_ui_sync_display_update(&status.sync_display,&m,now,s.page==0&&!s.help&&r2_ui_analog(&m,now));
    status.drawn_screensaver=s.screensaver;status.drawn_screensaver_position=s.screensaver_position;
    memset(status.drawn_lamps,0,sizeof status.drawn_lamps);
    if(s.page==1&&!s.screensaver)r2_ui_rf_lamps(status.drawn_lamps,&m,now);
    add(&status.frames);status.ready=true;status.drawn_page=s.page;status.analog=r2_ui_face_locked(&m,now);status.last_frame_ms=(uint32_t)end;status.drawn_model_ms=m.sampled_ms;bool fresh=now>=m.sampled_ms&&now-m.sampled_ms<=2500&&(!m.ble_time_selected||r2_ui_ble_current(&m,now));status.drawn_scope=fresh&&m.scope_active;status.drawn_role=status.drawn_scope?m.role:R2_UI_NO_ROLE;status.drawn_ble=fresh&&r2_ui_ble_current(&m,now);status.drawn_pps=fresh&&m.follower_pps_valid;status.drawn_map=fresh&&m.precision.map_valid;status.drawn_time=r2_ui_display_time(&m,now,&status.drawn_utc_us,&status.drawn_coarse);}
   if(!rc){
    status.drawn_sync_grace=r2_ui_sync_display_time(&s.sync_display,&m,now,&status.drawn_utc_us);
    if(status.drawn_sync_grace)status.drawn_time=status.drawn_coarse=true;
   }
   uint32_t elapsed=(uint32_t)(end-now);if(elapsed>status.max_frame_ms)status.max_frame_ms=elapsed;
   unsigned cadence=s.page==1&&!s.screensaver?125:250;
   if(end>=due+cadence)add(&status.missed);
   if(!rc)status.brightness_percent=s.screensaver?50:100;
   status.temperature_qc=m.temperature_qc;status.temperature_valid=m.temperature_valid;
   status.error=rc;if(rc)status.ready=false;k_spin_unlock(&lock,key);
   /* Drop missed refreshes; never queue catch-up frames. */
   due=end>=due+cadence?end+cadence:due+cadence;
  }
  uint64_t sleep_now=k_uptime_get();
  k_sleep(K_MSEC(due>sleep_now&&due-sleep_now<20?due-sleep_now:20));
 }
 /* Public SPI completion/abort precedes buffer reuse and display shutdown. */
 r2_shadow_session_stop(&shadow_session,k_uptime_get());
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
 r2_shadow_enable(false);
#endif
 struct r2_shadow_session_view final_shadow;shadow_view(&final_shadow);
 key=k_spin_lock(&lock);status.shadow=final_shadow;k_spin_unlock(&lock,key);
 int closed=r2_display_off();key=k_spin_lock(&lock);status.error=rc?rc:closed;status.stopped=true;status.ready=false;k_spin_unlock(&lock,key);
}
void r2_ui_start(void){if(started)return;started=true;if(atomic_get(&stop_requested)){k_spinlock_key_t k=k_spin_lock(&lock);status.stopped=true;k_spin_unlock(&lock,k);return;}k_thread_create(&thread,ui_stack,K_THREAD_STACK_SIZEOF(ui_stack),worker,NULL,NULL,NULL,6,0,K_NO_WAIT);k_thread_name_set(&thread,"r2_ui");}
void r2_ui_stop(void){atomic_set(&stop_requested,1);}
int r2_ui_close_result(void){struct r2_ui_status s;r2_ui_snapshot(&s);return !started?0:!s.stopped?-EAGAIN:s.error;}

bool r2_ui_poweroff_requested(void){struct r2_ui_status s;r2_ui_snapshot(&s);return s.poweroff;}

uint32_t r2_ui_stack_unused(void)
{
#ifdef CONFIG_INIT_STACKS
 size_t unused;if(started&&!k_thread_stack_space_get(&thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}
