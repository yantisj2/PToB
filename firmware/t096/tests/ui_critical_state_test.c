/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual production controller/worker helpers, ordinary API interleavings. */
static void base9_check(void);
static void recover_check(void);
#define R2_UI_TEST_BASE9() base9_check()
#define R2_UI_TEST_RECOVER() recover_check()
#define k_mutex_lock fixture_legacy_lock
#define k_mutex_unlock fixture_legacy_unlock
#define k_sleep fixture_legacy_sleep
#include UI_FAKE_HEADER
#undef k_mutex_lock
#undef k_mutex_unlock
#undef k_sleep
#include <stdlib.h>
#include <stdio.h>
typedef int atomic_val_t;
static int atomic_or(atomic_t *v,int n){int old=*v;*v|=n;return old;}
static int atomic_and(atomic_t *v,int n){int old=*v;*v&=n;return old;}
static struct k_thread *current_thread;
static struct k_thread *k_current_get(void){return current_thread;}
static unsigned setters,probes,wakes;
static unsigned recovery_checks;
static unsigned physical_recovery_checks;
static void base9_check(void){assert(priority==9);++recovery_checks;}
static void recover_check(void){assert(priority==9);++physical_recovery_checks;}
static void partial_main(void);
static void (*release_hook)(bool);
static struct {struct k_mutex *lock;struct k_thread *owner;int original;} loans[8];
static unsigned slot(struct k_mutex *lock){for(unsigned i=0;i<8;i++)if(!loans[i].lock||loans[i].lock==lock){loans[i].lock=lock;return i;}abort();}
static int k_mutex_lock(struct k_mutex *lock,int wait){
 if(!wait)++probes;
 int rc=wait==K_NO_WAIT?pthread_mutex_trylock(&lock->native):pthread_mutex_lock(&lock->native);
 if(!rc){unsigned i=slot(lock);loans[i].owner=current_thread;loans[i].original=priority;}
 return rc;
}
static int k_mutex_unlock(struct k_mutex *lock){
 unsigned i=slot(lock);
 if(loans[i].owner)priority=loans[i].original; /* pinned owner_orig_prio restoration */
 if(release_hook)release_hook(false);
 int rc=pthread_mutex_unlock(&lock->native);
 if(release_hook)release_hook(true);
 return rc;
}
static int k_thread_priority_get(struct k_thread *t){(void)t;return priority;}
static void k_thread_priority_set(struct k_thread *t,int p){(void)t;assert(!current_thread);priority=p;++setters;}
static void k_wakeup(struct k_thread *t){(void)t;++wakes;}
static void (*clock_hook)(unsigned);
static void k_sleep(unsigned ms){if(clock_hook)clock_hook(ms);else {fake_now+=ms;partial_main();}}
static int (*region_hook)(const uint8_t *,unsigned,unsigned,unsigned,unsigned,uint64_t,r2_display_checkpoint_fn,void *);
int r2_display_region(const uint8_t *fb,unsigned x,unsigned y,unsigned w,unsigned h,
 uint64_t deadline,r2_display_checkpoint_fn callback,void *context){
 if(region_hook)return region_hook(fb,x,y,w,h,deadline,callback,context);
 (void)fb;(void)x;(void)y;(void)w;(void)h;(void)deadline;(void)callback;(void)context;return -EIO;
}
#include UI_SERVICE_SOURCE
static struct k_mutex owner_lock={PTHREAD_MUTEX_INITIALIZER};
static bool owner_busy;static unsigned owner_probes;
bool r2_service_ui_lock_probe(unsigned owner){assert(owner==R2_UI_READ_GM||owner==R2_UI_READ_RC);++owner_probes;return !owner_busy;}
/* Public provider API boundaries. Native linkage and backend source checks
 * independently bind their real owner implementations. */
bool r2_service_snapshot_try(struct r2_service_status *s,r2_ui_read_boundary_fn f,void *c){base9_check();(void)s;(void)f;(void)c;return false;}
bool r2_service_ui_details_try(struct r2_ui_model *m,const struct r2_service_status *s,r2_ui_read_boundary_fn f,void *c){base9_check();(void)m;(void)s;(void)f;(void)c;return false;}
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
bool r2_capture_now_at(struct r2_capture *at,uint64_t *now){*at=(struct r2_capture){fake_now*16000,1,R2_CAPTURE_NOW};*now=fake_now*1000;return true;}
bool r2_service_rf_clock_try(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now,r2_ui_read_boundary_fn f,void *c){
 (void)at;*out=(struct r2_rf_clock_sample){.time_ns=UINT64_C(1700000000000000000),.root=1,.boot=2,.fresh_until_us=now+1000000,.available=true,.fresh=true,.utc=true,.rate_valid=true,.rate_ppb=3};
 return f(c,R2_UI_READ_GM,false)&&f(c,R2_UI_READ_GM,true);
}
void r2_lora_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now){
 (void)at;*out=(struct r2_rf_clock_sample){.time_ns=UINT64_C(1700000000000000123),.root=3,.boot=4,.fresh_until_us=now+500000,.available=true,.fresh=true,.utc=true,.rate_valid=true,.rate_ppb=1};
}
#endif
static void partial_main(void){struct k_thread *saved=current_thread;current_thread=NULL;critical_priority_tick();current_thread=saved;}
static bool in_hook;
static unsigned before_release,after_release;
static void probe_release(bool after){
 if(in_hook)return;in_hook=true;
 int before=priority;partial_main();
 if(!after){++before_release;assert(priority==before);}
 else {++after_release;assert(priority==8);}
 in_hook=false;
}
static void reset(void){
 assert(!pthread_mutex_trylock(&ui_lock.native));pthread_mutex_unlock(&ui_lock.native);
 memset(&ui_partial_state,0,sizeof ui_partial_state);memset(&status,0,sizeof status);
 started=true;priority=9;current_thread=&thread;fake_now=1000;release_hook=NULL;
 setters=probes=wakes=owner_probes=before_release=after_release=recovery_checks=physical_recovery_checks=0;owner_busy=false;
 atomic_set(&stop_requested,0);atomic_set(&context_revision,1);model_context=1;
 atomic_set(&source_hint,0);model_hint=0;control_serial=7;logic_revision=1;
 atomic_set(&ui_control_revision,7);atomic_set(&ui_policy_revision,1);
 ui_partial_state.metrics.generation=5;atomic_set(&ui_urgent_revision,5);atomic_set(&ui_critical_dirty,UI_TIME);
 model=(struct r2_ui_model){.sampled_ms=1000,.role=R2_UI_FOLLOWER,.scope_active=true,.ble_active=true,
  .authenticated_gm=true,.follower_pps_valid=true,.ble_time_selected=true,.ble_valid_until_us=5000000,
  .precision={.authorized=true,.map_valid=true,.locked=true},.platform_ready=true,.clock={.valid=true,.source_available=true,.pps_present=true,.pps_age_us=0,.utc_us=1792340000000000LL}};
 status.ready=true;
}
int main(void){
 /* Compare this digest with the separately compiled frozen325 controller:
  * named phases, urgency, stopping, actual priority and owner-probe refusal.
  * Internal enum numbers are intentionally excluded from the comparison. */
 unsigned phases[]={UI_UNSAFE,UI_LOCK,UI_OWNER_LOCK,UI_WAIT9,UI_WAIT8,UI_SPI,
  UI_READY,UI_RASTER,UI_RELEASING,UI_OWNER_RELEASING};
 uint32_t digest=2166136261u;unsigned cases=0;
 for(unsigned p=0;p<10;p++)for(unsigned u=0;u<2;u++)for(unsigned stop=0;stop<3;stop++)
 for(unsigned prio=7;prio<=9;prio++)for(unsigned busy=0;busy<2;busy++){
  reset();atomic_set(&ui_worker_phase,phases[p]);atomic_set(&ui_read_owner,R2_UI_READ_GM);
  atomic_set(&ui_critical_dirty,u?UI_TIME|UI_PULSE:0);atomic_set(&stop_requested,stop);
  priority=(int)prio;owner_busy=busy;partial_main();
  unsigned values[]={(unsigned)priority,setters,probes,owner_probes,wakes,
   (unsigned)atomic_get(&stop_requested),(unsigned)atomic_get(&ui_critical_dirty)};
  for(unsigned i=0;i<sizeof values/sizeof *values;i++)digest=(digest^values[i])*16777619u;
  ++cases;
 }
 printf("SCHED_EQ %08x cases%u\n",digest,cases);
 /* Every typed unsafe phase refuses promotion, even while urgent. */
 const unsigned unsafe[]={UI_UNSAFE,UI_LOCK,UI_OWNER_LOCK};
 for(unsigned i=0;i<sizeof unsafe/sizeof *unsafe;i++){reset();atomic_set(&ui_worker_phase,unsafe[i]);partial_main();assert(priority==9&&!setters&&!probes);}
 reset();atomic_set(&ui_worker_phase,UI_READY);partial_main();assert(priority==8&&setters==1);
 assert(!optional_enter()&&atomic_get(&ui_worker_phase)==UI_UNSAFE);
 /* RELEASING before unlock must not promote a held UI mutex. Restoration
  * then real release permits the actual nonblocking witness and promotion. */
 reset();unsigned key=ui_take();assert(priority==9);release_hook=probe_release;ui_drop(key);
 assert(before_release==1&&after_release==1&&priority==8&&setters==1);
 release_hook=NULL;assert(!optional_enter());
 /* No unchanged-priority probe traffic; no desired-priority cache may hide
  * a later restoration to9 after a previously successful8 transition. */
 reset();priority=8;atomic_set(&ui_worker_phase,UI_RELEASING);partial_main();assert(!probes&&!setters);
 priority=9;partial_main();assert(priority==8&&probes==1&&setters==1);
 /* Public owner witness, including an outer GM loan over nested PT reads. */
 for(unsigned owner=R2_UI_READ_GM;owner<=R2_UI_READ_RC;owner++){
  reset();atomic_set(&ui_critical_dirty,0);assert(optional_owner_enter());atomic_set(&ui_critical_dirty,UI_TIME);assert(read_boundary(NULL,owner,false));owner_busy=true;
  partial_main();assert(priority==9&&owner_probes==1&&!setters);
  owner_busy=false;partial_main();assert(priority==8&&owner_probes==2&&setters==1);assert(!read_boundary(NULL,owner,true));
 }
 /* WAIT9 must complete the actual demotion even after stop. Stop cancels
  * pending events once, without forced READY or a recurring normal probe. */
 reset();priority=8;ui_partial_state.on_pending=ui_partial_state.off_pending=ui_partial_state.second_pending=true;
 atomic_set(&stop_requested,1);assert(!worker_priority(9));assert(priority==9&&wakes&&atomic_get(&stop_requested)==2);
 assert(ui_partial_state.metrics.pps[0][7]==1&&ui_partial_state.metrics.pps[0][8]==1&&ui_partial_state.metrics.seconds[3]==1&&!urgent());
 unsigned prior=probes;partial_main();assert(probes==prior);
 /* Actual locked copy preserves the original tuple; every changed fence
  * or exhausted generation rejects the selected job. */
 reset();struct r2_ui_model m;struct r2_ui_status s;struct ui_job j={.m=&m,.s=&s};
 assert(job_copy(&j,true));assert(job_current(&j));
 atomic_set(&ui_control_revision,8);assert(!job_current(&j));atomic_set(&ui_control_revision,7);
 atomic_set(&ui_policy_revision,2);assert(!job_current(&j));atomic_set(&ui_policy_revision,1);
 atomic_set(&ui_urgent_revision,6);assert(!job_current(&j));atomic_set(&ui_urgent_revision,5);
 atomic_set(&source_hint,1);assert(!job_current(&j));atomic_set(&source_hint,0);
 atomic_set(&context_revision,2);assert(!job_copy(&j,false));atomic_set(&context_revision,1);
 j.generation=UINT32_MAX;atomic_set(&ui_urgent_revision,UINT32_MAX);assert(!job_current(&j));
 /* Submission and retirement reject a crossed second or actual250ms lamp,
  * even if main has not yet published another generation. */
 reset();struct r2_ui_model model_copy=model;struct r2_ui_status status_copy=status;
 assert(time_matches(&model_copy,&status_copy,1000,model.clock.utc_us,true));
 assert(!time_matches(&model_copy,&status_copy,2000,model.clock.utc_us,true));
 assert(r2_ui_local_pulse(&model_copy,1249)&&!r2_ui_local_pulse(&model_copy,1250));
 for(unsigned late=0;late<3;late++){
  reset();struct r2_ui_model mm;struct r2_ui_status ss;struct ui_job q={.m=&mm,.s=&ss};
  assert(job_copy(&q,true));q.tile=R2_UI_TILE_TIME;q.bit=UI_TIME;
  q.view=(struct r2_ui_tile_view){.utc_us=model.clock.utc_us,.valid=true,.pulse=true,.edge=1};
  q.submit_ms=1000;q.retire_ms=fake_now=late==0?1001:late==1?1250:2000;
  job_result(&q,0,true);
  if(!late)assert(ui_partial_state.metrics.digital_valid&&!urgent());
  else assert(!ui_partial_state.metrics.digital_valid&&urgent()&&ui_partial_state.metrics.jobs[7]==1);
 }
 /* A late/obsolete/failed on write may still be physically visible. Moving
  * the region must erase it, while no commit or eligible ACK is invented. */
 for(unsigned failure=0;failure<3;failure++){
  reset();struct r2_ui_model mm;struct r2_ui_status ss;struct ui_job q={.m=&mm,.s=&ss};
  assert(job_copy(&q,true));q.tile=R2_UI_TILE_PPS;q.bit=UI_PULSE;
  q.view=(struct r2_ui_tile_view){.utc_us=model.clock.utc_us,.valid=true,.pulse=true,.edge=1};
  q.submit_ms=1000;q.retire_ms=2000;
  if(failure==1)atomic_set(&ui_urgent_revision,6);
  job_result(&q,failure==0?R2_DISPLAY_EXPIRED:failure==1?0:-EIO,true);
  assert(ui_partial_state.lamp_lit&&!ui_partial_state.metrics.jobs[2]);
  unsigned key=ui_take();control_change();status.screensaver=true;status.screensaver_position=2;
  critical_observe(fake_now);ui_drop(key);assert(atomic_get(&ui_critical_dirty)&UI_CLEAR);
 }
 /* A9 demotion can delay post-SPI publication. Fresh logical time may not
  * turn a now-old physical face into a new actual-analog500ms grace seed. */
 for(unsigned late=0;late<2;late++){
  reset();atomic_set(&ui_critical_dirty,0);model.face_lock_until_us=5000000;
  model.precision.rms_valid=true;model.precision.rms_ns=0;
  struct r2_ui_model mm;struct r2_ui_status ss;struct ui_job q={.m=&mm,.s=&ss,.background=true};
  assert(job_copy(&q,true));ui_partial_state.background.render_ms=1000;
  ui_partial_state.background.flags=BG_ANALOG;ui_partial_state.background.face=0;
  analog_ack.pending=false;fake_now=late?1600:1200;background_face(&q,1200);
  assert(analog_ack.pending==!late);
 }
 /* Separate front/Saver jobs never fabricate a whole-frame ACK or miss. */
 for(unsigned page=0;page<2;page++){
  reset();atomic_set(&ui_critical_dirty,0);struct r2_ui_model mm;struct r2_ui_status ss;
  struct ui_job q={.m=&mm,.s=&ss,.background=true};assert(job_copy(&q,true));
  ui_partial_state.background.page=page;ui_partial_state.background.render_ms=1000;
  background_complete(&q,1100);
  assert(status.frames==(page?1u:0u)&&!status.missed&&!status.drawn_time);
 }
 /* Returned-attempt visibility does not fabricate a tile ACK. Keep its
  * original100ms start, stage/result domain and saturated elapsed width. */
 for(unsigned outcome=0;outcome<6;outcome++){
  reset();struct r2_ui_model mm;struct r2_ui_status ss;
  struct ui_job q={.m=&mm,.s=&ss};assert(job_copy(&q,true));
  q.deadline=1100;q.stage=8;q.background=outcome==5;
  q.retire_ms=fake_now=outcome==5?100000:1100;
  int rc=outcome==0?R2_DISPLAY_OBSOLETE:outcome==1?R2_DISPLAY_BUDGET_DECLINED:
   outcome==2?R2_DISPLAY_EXPIRED:outcome==3?-EIO:0;
  if(outcome>=4)atomic_set(&ui_urgent_revision,6);
  if(outcome==5)atomic_set(&ui_critical_dirty,UI_TIME);
  job_result(&q,rc,true);
  unsigned packed=ui_partial_state.metrics.attempt;
  assert((packed>>16)==(outcome==5?65535u:100u));
  assert((packed>>8&255u)==8&&!!(packed&128u)==(outcome==5));
  const unsigned results[]={2,1,3,4,5,5};assert((packed&127u)==results[outcome]);
  assert(!ui_partial_state.metrics.jobs[2]&&!ui_partial_state.metrics.digital_retire_ms);
 }
 (void)owner_lock;
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
 reset();atomic_set(&ui_critical_dirty,0);priority=9;status.page=1;
 struct r2_ui_model rf=model;optional_rf_view(&rf);
 assert(rf.rf_comparison.delta_ns==-123&&rf.rf_comparison.drift_ppb==2&&
  rf.rf_comparison.until_us==fake_now*1000+500000);
#endif
 puts("PASS actual production unsafe/release/probe/restore/stop/generation helpers and fresh-second/PPS boundaries");
 return 0;
}
