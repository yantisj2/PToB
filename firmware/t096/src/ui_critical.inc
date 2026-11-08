/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Included by the sole UI owner; static/nonreentrant, no IRQ/timer owner. */
enum {UI_TIME=1,UI_PULSE=2,UI_LOSS=4,UI_CLEAR=8};
static struct {
 uint64_t edge_at,second_at;
 int64_t second;
 uint32_t context,hint,control,policy,edges;
 uint32_t last_hint,digital_hint;
 uint8_t page,population,position;
 bool seen,visible,time_visible,pulse,analog,valid,on_pending,off_pending,second_pending,edge_eligible;
 struct r2_ui_zone_cache zone;
 struct r2_ui_critical_status metrics;
 struct {
  uint64_t render_ms,model_ms;int64_t utc_us;
  uint32_t tags[4];uint16_t remaining,face;
  int16_t temperature_qc;uint8_t page,position,flags,control_flags;
#ifdef R2_TZ_WIRE
  struct r2_tz zone;uint8_t zone_flags;
#endif
 } background;
 struct {uint32_t tags[4];uint16_t remaining;} loss;
 uint8_t lamp_y;bool lamp_lit;
} ui_partial_state;
static void add_n(uint32_t *v,uint32_t n){*v=UINT32_MAX-*v<n?UINT32_MAX:*v+n;}
static void __attribute__((noinline)) max_ms(uint32_t *v,uint64_t age){if(age>*v)*v=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
static bool urgent(void){return atomic_get(&ui_critical_dirty)!=0;}
static void request_tile(unsigned bit){
 atomic_or(&ui_critical_dirty,(atomic_val_t)bit);
 add(&ui_partial_state.metrics.generation);
 atomic_set(&ui_urgent_revision,(atomic_val_t)ui_partial_state.metrics.generation);
}
/* Caller owns ui_lock. Event cancellation is separate from a GUI miss. */
static void cancel_events(void){
 struct r2_ui_critical_status *v=&ui_partial_state.metrics;unsigned pop=ui_partial_state.population;
 if(ui_partial_state.on_pending)add(&v->pps[pop][7]);
 if(ui_partial_state.off_pending)add(&v->pps[pop][8]);
 if(ui_partial_state.second_pending)add(&v->seconds[3]);
 add(&v->resets);ui_partial_state.on_pending=ui_partial_state.off_pending=ui_partial_state.second_pending=false;
 ui_partial_state.edge_eligible=false;atomic_set(&ui_critical_dirty,0);
}
/* Main owns priority changes. WANT/LOCK cannot be promoted. A successful
 * public try-lock in RELEASING witnesses the pinned mutex's completed PI
 * restoration. UI cannot run/relock between this probe and priority_set. */
static void critical_priority_tick(void){
 if(!started)return;
 if(atomic_get(&stop_requested)==1&&!k_mutex_lock(&ui_lock,K_NO_WAIT)){
  cancel_events();atomic_set(&stop_requested,2);(void)k_mutex_unlock(&ui_lock);
 }
 unsigned phase=(unsigned)atomic_get(&ui_worker_phase);
 if(phase==UI_LOCK||phase==UI_UNSAFE||phase==UI_OWNER_LOCK)return;
 bool boost=phase==UI_WAIT8||phase==UI_SPI||
  ((phase==UI_READY||phase==UI_RASTER||phase==UI_RELEASING||phase==UI_OWNER_RELEASING)&&urgent()&&!atomic_get(&stop_requested));
 int desired=boost?8:9;
 if(k_thread_priority_get(&thread)!=desired){
  if(phase==UI_RELEASING){
   if(k_mutex_lock(&ui_lock,K_NO_WAIT))return;
   (void)k_mutex_unlock(&ui_lock);
  }
  if(phase==UI_OWNER_RELEASING&&!r2_service_ui_lock_probe((unsigned)atomic_get(&ui_read_owner)))return;
  k_thread_priority_set(&thread,desired);
 }
 if(phase==UI_WAIT8||phase==UI_WAIT9||(boost&&phase==UI_READY))k_wakeup(&thread);
}
static bool worker_priority(int priority){
 atomic_set(&ui_worker_phase,priority==8?UI_WAIT8:UI_WAIT9);
 while((priority==9||!atomic_get(&stop_requested))&&k_thread_priority_get(&thread)!=priority)k_sleep(K_MSEC(1));
 return !atomic_get(&stop_requested);
}
/* Publish WANT before the actual-priority/pending checks. A main promotion
 * immediately before this store is observed; none can begin after it. */
static bool optional_enter(void){
 atomic_set(&ui_worker_phase,UI_UNSAFE);
 return !urgent()&&k_thread_priority_get(&thread)==9&&!atomic_get(&stop_requested);
}
static bool optional_owner_enter(void){
 if(!optional_enter())return false;
 atomic_set(&ui_worker_phase,UI_OWNER_LOCK);return true;
}
static bool read_boundary(void *unused,unsigned owner,bool after){
 (void)unused;
 if(after)return optional_enter();
 atomic_set(&ui_read_owner,(atomic_val_t)owner);
 atomic_set(&ui_worker_phase,UI_OWNER_RELEASING);return true;
}
static void critical_observe(uint64_t now){
 struct r2_ui_critical_status *v=&ui_partial_state.metrics;
 bool visible=started&&!atomic_get(&stop_requested)&&!status.help&&!status.menu.open&&!status.poweroff&&
  (status.screensaver||status.page==0);
 bool time_visible=visible&&!status.screensaver;
 uint8_t pop=status.screensaver?1:0;
 bool changed=ui_partial_state.seen&&(ui_partial_state.context!=model_context||ui_partial_state.hint!=model_hint||
  ui_partial_state.control!=control_serial||ui_partial_state.page!=status.page||
  ui_partial_state.population!=pop||ui_partial_state.position!=status.screensaver_position||ui_partial_state.visible!=visible);
 if(changed)cancel_events();
 uint32_t delta=model.clock.pps_edges-ui_partial_state.edges;
 if(ui_partial_state.seen&&delta){
  if(delta<=INT32_MAX)add_n(&v->observed_edges,delta);
  else {add(&v->unknown_edges);delta=0;}
 }
 ui_partial_state.edges=model.clock.pps_edges;
 bool pulse=r2_ui_local_pulse(&model,now);
 bool pulse_changed=!ui_partial_state.seen||changed||pulse!=ui_partial_state.pulse;
 if(visible&&ui_partial_state.seen&&!changed&&delta){
  if(ui_partial_state.on_pending){add(&v->pps[pop][4]);ui_partial_state.on_pending=false;}
  if(ui_partial_state.off_pending){add(&v->pps[pop][5]);ui_partial_state.off_pending=false;}
  ui_partial_state.edge_eligible=false;
  if(delta>1){add_n(&v->unknown_edges,delta-1);add_n(&v->pps[pop][6],delta-1);}
  if(pulse){
   uint64_t age=(uint64_t)model.clock.pps_age_us+(now-model.sampled_ms)*1000;
   if(age/1000<=now){
    ui_partial_state.edge_at=now-age/1000;ui_partial_state.edge_eligible=ui_partial_state.on_pending=true;
    add(&v->pps[pop][0]);pulse_changed=true;
   }else {add(&v->unknown_edges);add(&v->pps[pop][6]);}
  }else {add(&v->unknown_edges);add(&v->pps[pop][6]);}
 }
 if(visible&&!changed&&ui_partial_state.seen&&ui_partial_state.pulse&&!pulse){
  if(ui_partial_state.on_pending){add(&v->pps[pop][4]);ui_partial_state.on_pending=false;}
  /* Only a previously eligible observed edge creates an off population. */
  if(ui_partial_state.edge_eligible){ui_partial_state.off_pending=true;add(&v->pps[pop][1]);}
 }
 int64_t utc=0;bool coarse=false;
 bool valid=r2_ui_display_time(&model,now,&utc,&coarse);
 if(r2_ui_sync_display_time(&status.sync_display,&model,now,&utc))valid=true;
 int64_t second=valid?utc/1000000:0;
 bool analog=r2_ui_face_locked(&model,now)||
  r2_ui_sync_display_time(&status.sync_display,&model,now,&utc);
 bool second_changed=!ui_partial_state.seen||changed||valid!=ui_partial_state.valid||second!=ui_partial_state.second;
 if(!valid&&ui_partial_state.second_pending){add(&v->seconds[3]);ui_partial_state.second_pending=false;}
 if(time_visible&&valid&&ui_partial_state.seen&&!changed&&ui_partial_state.valid&&second!=ui_partial_state.second){
  if(ui_partial_state.second_pending)add(&v->seconds[2]);
  int64_t advance=second-ui_partial_state.second;
  if(advance>1&&advance<=INT32_MAX)add_n(&v->unknown_seconds,(uint32_t)advance-1);
  if(advance<=0)add(&v->unknown_seconds);
  add(&v->seconds[0]);ui_partial_state.second_pending=true;
   int64_t fraction=utc%1000000;if(fraction<0)fraction+=1000000;
   if((uint64_t)fraction/1000<=now)ui_partial_state.second_at=now-(uint64_t)fraction/1000;
   else {add(&v->unknown_seconds);ui_partial_state.second_pending=false;}
 }
 if(visible){
  if(time_visible&&second_changed)request_tile(UI_TIME);
  if(pulse_changed||delta)request_tile(UI_PULSE);
  if(time_visible&&ui_partial_state.seen&&analog!=ui_partial_state.analog)request_tile(UI_TIME);
  if(!analog&&(!ui_partial_state.seen||changed||analog!=ui_partial_state.analog||
      logic_revision!=ui_partial_state.policy))request_tile(UI_LOSS);
  if(analog)atomic_and(&ui_critical_dirty,~(atomic_val_t)UI_LOSS);
 }else atomic_set(&ui_critical_dirty,0);
 /* Erase a physically lit old lamp before moving or hiding its region. The
  * erased old population is a cancellation, never a new eligible off event. */
 unsigned lamp_y=status.screensaver?r2_ui_saver_top(status.screensaver_position)+2:19;
 if(ui_partial_state.lamp_lit&&(!visible||ui_partial_state.lamp_y!=lamp_y)&&
    !(atomic_get(&ui_critical_dirty)&UI_CLEAR))request_tile(UI_CLEAR);
 ui_partial_state.context=model_context;ui_partial_state.hint=model_hint;ui_partial_state.control=control_serial;
 ui_partial_state.policy=logic_revision;ui_partial_state.page=status.page;ui_partial_state.population=pop;
 ui_partial_state.position=status.screensaver_position;ui_partial_state.visible=visible;
 ui_partial_state.time_visible=time_visible;ui_partial_state.pulse=pulse;ui_partial_state.analog=analog;
 ui_partial_state.valid=valid;ui_partial_state.second=second;ui_partial_state.seen=true;
 v->context=model_context;v->page=status.page;v->population=pop;
}
/* Pure projection shared by submission, retirement and on-demand evidence.
 * A returned successful SPI call is not an ACK for a crossed second/edge. */
static bool __attribute__((noinline)) time_matches(const struct r2_ui_model *m,
 const struct r2_ui_status *s,uint64_t now,int64_t rendered,bool valid){
 int64_t utc=0;bool coarse;bool live=r2_ui_display_time(m,now,&utc,&coarse);
 if(r2_ui_sync_display_time(&s->sync_display,m,now,&utc))live=true;
 return live==valid&&(!live||utc/1000000==rendered/1000000);
}
void r2_ui_critical_snapshot(struct r2_ui_critical_status *out){
 unsigned key=ui_take();*out=ui_partial_state.metrics;
 out->pending=(ui_partial_state.on_pending?1u:0u)|(ui_partial_state.off_pending?2u:0u)|(ui_partial_state.second_pending?4u:0u);
 uint32_t context=(uint32_t)atomic_get(&context_revision),hint=(uint32_t)atomic_get(&source_hint);
 uint64_t now=k_uptime_get();
 bool permission=status.ready&&!status.stopped&&!atomic_get(&stop_requested)&&
  !!(logical_flags(&model,&status.sync_display,now)&R2_UI_LOGIC_VALID);
 out->valid=out->valid&&permission&&out->fence[0]==context&&ui_partial_state.last_hint==hint&&
  out->fence[1]==control_serial&&out->fence[2]==logic_revision;
 out->digital_valid=out->digital_valid&&permission&&time_matches(&model,&status,now,out->digital_utc_us,true)&&!status.screensaver&&status.page==0&&!status.help&&!status.menu.open&&
  out->fence[3]==context&&ui_partial_state.digital_hint==hint&&
  out->fence[4]==control_serial&&out->fence[5]==logic_revision;
 ui_drop(key);
}
/* Existing clock query only. Fixed field order is documented/tested; no
 * periodic serialization or continuous log stream. */
static bool ui_json_add(struct r2_json *j,const char *text){
 r2_json_text(j,text);return !j->error&&j->used<j->capacity;
}
static bool ui_json_array(struct r2_json *j,const char *key,const uint32_t *a,unsigned count){
 if(!ui_json_add(j,key))return false;
 /* Existing callers include the opening bracket in their trusted key. */
 --j->used;r2_json_u32_array(j,a,count);
 return !j->error&&j->used<j->capacity;
}
static bool shadow_append(struct r2_json *j){
 unsigned key=ui_take();
 ui_query_scratch.shadow.view=status.shadow;
 ui_query_scratch.shadow.window=ui_shadow_observer.published;
 bool current=ui_shadow_observer.published.context==(uint32_t)atomic_get(&context_revision)&&
  ui_shadow_observer.published.hint==(uint32_t)atomic_get(&source_hint)&&
  ui_shadow_observer.published.control==control_serial&&shadow_page(&model,status.page)&&
  !status.help&&!status.stopped&&status.ready;
 ui_drop(key);
 const struct r2_shadow_session_view *v=&ui_query_scratch.shadow.view;
 const struct r2_ui_shadow_window *w=&ui_query_scratch.shadow.window;
 if(!ui_json_add(j,",\"shadow_pps\":{\"v\":1")||
    !ui_json_array(j,",\"window\":[",&w->session,7)||
    !ui_json_array(j,",\"counts\":[",&v->session,13))return false;
 int n=snprintf(j->out+j->used,j->capacity-j->used,
  ",\"ticks\":[%llu,%llu],\"view\":[%u,%u,%u,%u,%llu,%lld,%u]}}",
  (unsigned long long)w->start_tick,(unsigned long long)w->epoch,
  v->state,v->active,v->valid,v->approximate,(unsigned long long)v->elapsed_ms,
  (long long)v->mean_tenth_ns,current);
 return n>=0&&(size_t)n<j->capacity-j->used;
}
bool r2_ui_critical_append(char *json,size_t capacity){
 struct r2_ui_critical_status *v=&ui_query_scratch.critical;r2_ui_critical_snapshot(v);
 size_t n=0;while(n<capacity&&json[n])++n;
 if(!n||n==capacity||json[n-1]!='}')return false;
 struct r2_json j={.out=json,.capacity=capacity,.used=n-1};json[j.used]=0;
 if(!ui_json_add(&j,",\"ui_critical\":{\"v\":1"))return false;
 uint32_t a[8]={v->generation,v->context,v->page,v->population,v->pending};
 if(!ui_json_array(&j,",\"scope\":[",a,5))return false;
 a[0]=v->observed_edges;a[1]=v->unknown_edges;a[2]=v->unknown_seconds;a[3]=v->resets;
 if(!ui_json_array(&j,",\"observed\":[",a,4)||
     !ui_json_array(&j,",\"normal\":[",v->pps[0],9)||
     !ui_json_array(&j,",\"saver\":[",v->pps[1],9))return false;
 memcpy(a,v->pps_max_ms,sizeof a);
 if(!ui_json_array(&j,",\"pps_max_ms\":[",a,8))return false;
 memcpy(a,v->seconds,sizeof v->seconds);memcpy(a+4,v->seconds_max_ms,sizeof v->seconds_max_ms);
 if(!ui_json_array(&j,",\"seconds\":[",a,6)||
     !ui_json_array(&j,",\"late_gt100_ms\":[",v->late_gt100_ms,5)||
     !ui_json_array(&j,",\"jobs\":[",v->jobs,8)||
     !ui_json_array(&j,",\"fence\":[",v->fence,6))return false;
 int r=snprintf(j.out+j.used,j.capacity-j.used,
  ",\"attempt\":%u,\"last\":[%u,%u,%u,%u,%lld,%llu,%llu,%llu],\"digital\":[%lld,%llu,%llu,%llu,%u]}}",
   v->attempt,v->tile,v->edge,v->pulse,v->valid,(long long)v->rendered_utc_us,(unsigned long long)v->render_ms,
   (unsigned long long)v->submit_ms,(unsigned long long)v->retire_ms,(long long)v->digital_utc_us,
   (unsigned long long)v->digital_render_ms,(unsigned long long)v->digital_submit_ms,
   (unsigned long long)v->digital_retire_ms,v->digital_valid);
 if(r<0||(size_t)r>=j.capacity-j.used)return false;
 j.used+=(size_t)r-1;json[j.used]=0;return shadow_append(&j);
}

struct ui_job {
 struct r2_ui_model *m;struct r2_ui_status *s;
 struct r2_ui_tile_view view;
 uint64_t render_ms,submit_ms,retire_ms,deadline,idle_since;
 bool stable,wake_press;
 uint32_t context,hint,control,policy,generation;
 unsigned bit;enum r2_ui_tile tile;bool background;uint8_t stage;
 struct r2_ui_rect rect;
};
static bool job_current(const struct ui_job *j){
 return !atomic_get(&stop_requested)&&j->context==(uint32_t)atomic_get(&context_revision)&&j->context!=UINT32_MAX&&
  j->hint==(uint32_t)atomic_get(&source_hint)&&j->control==(uint32_t)atomic_get(&ui_control_revision)&&
  j->policy==(uint32_t)atomic_get(&ui_policy_revision)&&
  (j->background?!urgent():(j->generation!=UINT32_MAX&&j->generation==(uint32_t)atomic_get(&ui_urgent_revision)));
}
static bool job_copy(struct ui_job *j,bool initial){
 atomic_set(&ui_worker_phase,UI_LOCK);
 if(k_mutex_lock(&ui_lock,K_NO_WAIT)){atomic_set(&ui_worker_phase,UI_SPI);return false;}
 *j->m=model;*j->s=status;
 j->idle_since=input_idle_since;j->stable=input_stable;j->wake_press=input_wake_press;
 if(initial){
  j->context=model_context;j->hint=model_hint;j->control=control_serial;
  j->policy=logic_revision;j->generation=ui_partial_state.metrics.generation;
 }
 bool same=j->context==model_context&&j->hint==model_hint&&
  j->control==control_serial&&j->policy==logic_revision&&
   (j->background||j->generation==ui_partial_state.metrics.generation);
 if(initial&&ui_partial_state.seen) same=same&&
  ui_partial_state.context==model_context&&ui_partial_state.hint==model_hint&&
  ui_partial_state.control==control_serial&&ui_partial_state.policy==logic_revision&&
  ui_partial_state.page==status.page&&ui_partial_state.population==(status.screensaver?1u:0u)&&
  ui_partial_state.position==status.screensaver_position;
 atomic_set(&ui_worker_phase,UI_RELEASING);(void)k_mutex_unlock(&ui_lock);
 model_fence(j->m,j->context,j->hint);
 return same&&job_current(j);
}
static bool raster_check(void *context,enum r2_ui_render_stage stage){
 struct ui_job *j=context;
 j->stage=8;
 if(stage==R2_UI_RENDER_OPTIONAL)return j->background&&optional_enter();
 atomic_set(&ui_worker_phase,j->background?UI_RASTER:UI_SPI);
 if(atomic_get(&stop_requested)||!job_current(j))return false;
 if(j->background)return k_thread_priority_get(&thread)==9;
 k_yield();return job_current(j)&&k_uptime_get()<(int64_t)j->deadline;
}
static bool display_check(void *context,enum r2_display_stage stage){
 struct ui_job *j=context;
 if(stage==R2_DISPLAY_RETIRED){j->retire_ms=k_uptime_get();return true;}
 j->stage=(uint8_t)stage+2;
 atomic_set(&ui_worker_phase,UI_SPI);
 if(!job_current(j)||k_uptime_get()>=(int64_t)j->deadline)return false;
 if(stage==R2_DISPLAY_PREPARE&&!j->background){
  if(!job_copy(j,false))return false;
  j->render_ms=k_uptime_get();
   bool rendered=r2_ui_tile_render_rect(framebuffer,j->m,j->s,j->render_ms,j->tile,
    &ui_partial_state.zone,&j->view,raster_check,j,j->rect);
  return rendered&&(j->tile!=R2_UI_TILE_LOSS||!j->view.analog);
 }
 if(stage==R2_DISPLAY_SUBMIT){
  uint64_t now=k_uptime_get();
   if(!j->background&&(j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS)){
   if(!time_matches(j->m,j->s,now,j->view.utc_us,j->view.valid)||
     r2_ui_local_pulse(j->m,now)!=j->view.pulse){
    unsigned key=ui_take();add(&ui_partial_state.metrics.jobs[6]);ui_drop(key);return false;
   }
  }
  j->submit_ms=now;return job_current(j);
 }
 k_yield();return job_current(j);
}
static bool draw_error(int rc){return rc<0;}
static void job_attempt(const struct ui_job *j){
 unsigned key=ui_take();add(&ui_partial_state.metrics.jobs[j->background?1:0]);ui_drop(key);
}
static void job_result(struct ui_job *j,int rc,bool final){
 unsigned key=ui_take();struct r2_ui_critical_status *v=&ui_partial_state.metrics;
 bool current=job_current(j);
 if(current&&!j->background&&j->view.valid&&
    (j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS))
  current=!!(logical_flags(&model,&status.sync_display,k_uptime_get())&R2_UI_LOGIC_VALID);
 if(current&&!j->background&&(j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS)){
  uint64_t now=k_uptime_get();
  current=time_matches(&model,&status,now,j->view.utc_us,j->view.valid)&&
   r2_ui_local_pulse(&model,now)==j->view.pulse;
 }
 uint64_t elapsed=j->retire_ms-(j->deadline-100);
 unsigned result=rc<0?4:rc?(unsigned)rc:current?0:5;
 v->attempt=((uint32_t)(elapsed>65535?65535:elapsed)<<16)|
  ((unsigned)j->stage<<8)|(j->background?128u:0u)|result;
 /* Submitted pixels can have reached the panel even when retirement is late,
  * a source fence changed, or a real transfer error made completion unknown.
  * Retain a conservative old-lamp erase obligation without fabricating ACK. */
 if(!j->background&&j->submit_ms&&(j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS)&&
    (j->view.pulse||(!rc&&current&&final))){
  ui_partial_state.lamp_y=j->s->screensaver?r2_ui_saver_top(j->s->screensaver_position)+2:19;
  ui_partial_state.lamp_lit=j->view.pulse;
 }
 if(rc){
  if(draw_error(rc))add(&v->jobs[4]);else add(&v->jobs[7]);
  if(rc==-ETIMEDOUT||rc==R2_DISPLAY_BUDGET_DECLINED||rc==R2_DISPLAY_EXPIRED)add(&v->jobs[5]);
 }else{
  status.ready=true;status.error=0;
  if(!current)add(&v->jobs[7]);
 }
 if(!rc&&current){
  add(&v->jobs[j->background?3:2]);
  if(!j->background&&final){
   if(j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS){
    v->tile=j->tile;v->edge=j->view.edge;v->pulse=j->view.pulse;v->valid=j->view.valid;
    v->rendered_utc_us=j->view.utc_us;v->render_ms=j->render_ms;
    v->submit_ms=j->submit_ms;v->retire_ms=j->retire_ms;
    v->fence[0]=j->context;v->fence[1]=j->control;v->fence[2]=j->policy;ui_partial_state.last_hint=j->hint;
    unsigned pop=j->s->screensaver?1:0;
    if(j->view.pulse&&ui_partial_state.on_pending&&j->view.edge==ui_partial_state.edges){
     add(&v->pps[pop][2]);ui_partial_state.on_pending=false;
     max_ms(&v->pps_max_ms[pop][0],j->submit_ms-ui_partial_state.edge_at);
     uint64_t late=j->retire_ms-ui_partial_state.edge_at;
     max_ms(&v->pps_max_ms[pop][1],late);if(late>100)add(&v->late_gt100_ms[pop*2]);
    }else if(!j->view.pulse&&ui_partial_state.off_pending&&j->view.edge==ui_partial_state.edges){
     add(&v->pps[pop][3]);ui_partial_state.off_pending=false;
     uint64_t at=ui_partial_state.edge_at+250,late=j->retire_ms>at?j->retire_ms-at:0;
     max_ms(&v->pps_max_ms[pop][2],j->submit_ms>at?j->submit_ms-at:0);
     max_ms(&v->pps_max_ms[pop][3],late);if(late>100)add(&v->late_gt100_ms[pop*2+1]);
    }
   }
    if(j->tile==R2_UI_TILE_CLEAR)ui_partial_state.lamp_lit=false;
    if(j->tile==R2_UI_TILE_LOSS)status.analog=false;
   if(j->tile==R2_UI_TILE_TIME){
    v->digital_utc_us=j->view.utc_us;v->digital_render_ms=j->render_ms;
    v->digital_submit_ms=j->submit_ms;v->digital_retire_ms=j->retire_ms;v->digital_valid=j->view.valid;
    v->fence[3]=j->context;v->fence[4]=j->control;v->fence[5]=j->policy;ui_partial_state.digital_hint=j->hint;
    if(ui_partial_state.second_pending&&j->view.valid&&j->view.utc_us/1000000==ui_partial_state.second){
     add(&v->seconds[1]);ui_partial_state.second_pending=false;
     max_ms(&v->seconds_max_ms[0],j->submit_ms-ui_partial_state.second_at);
     uint64_t late=j->retire_ms-ui_partial_state.second_at;
     max_ms(&v->seconds_max_ms[1],late);if(late>100)add(&v->late_gt100_ms[4]);
    }
   }
   /* A current normal TIME band includes its PPS lamp. */
   unsigned satisfied=j->bit|(j->tile==R2_UI_TILE_TIME?UI_PULSE:0);
   atomic_and(&ui_critical_dirty,~(atomic_val_t)satisfied);
  }
 }
 if(draw_error(rc)){status.error=rc;status.ready=false;}
 ui_drop(key);
}
static int region_piece(struct ui_job *j,unsigned x,unsigned y,unsigned w,unsigned h,bool final){
 if(!w||!h)return 0;
 j->rect=(struct r2_ui_rect){x,y,w,h};
 j->stage=1;
 job_attempt(j);if(!worker_priority(8))return R2_DISPLAY_OBSOLETE;
 atomic_set(&ui_worker_phase,UI_SPI);
 int rc=r2_display_region(framebuffer,x,y,w,h,j->deadline,display_check,j);
 job_result(j,rc,final);return rc;
}
/* Window setup is bounded per independent strip. Critical rectangles are
 * excluded from cached background/loss strips, including their setup wakes. */
static int draw_band(struct ui_job *j,unsigned y,bool final){
 if(!j->s->help&&!j->s->menu.open&&!j->s->screensaver&&j->s->page==0&&y>=16&&y<48)return 0;
 if(j->s->screensaver){
  unsigned lamp=r2_ui_saver_top(j->s->screensaver_position)+2;
  if(y<lamp+9&&y+16>lamp){
   unsigned lo=y>lamp?y:lamp,hi=y+16<lamp+9?y+16:lamp+9;
   int rc=region_piece(j,0,y,80,lo-y,false);
   if(!rc)rc=region_piece(j,0,lo,66,hi-lo,false);
   if(!rc)rc=region_piece(j,75,lo,5,hi-lo,final&&hi==y+16);
   if(!rc)rc=region_piece(j,0,hi,80,y+16-hi,final);
   return rc;
  }
 }
 return region_piece(j,0,y,80,16,final);
}
static void save_tags(uint32_t tags[4],const struct ui_job *j){
 tags[0]=j->context;tags[1]=j->hint;tags[2]=j->control;tags[3]=j->policy;
}
static bool same_tags(const uint32_t tags[4],const struct ui_job *j){
 return tags[0]==j->context&&tags[1]==j->hint&&tags[2]==j->control&&tags[3]==j->policy;
}
static unsigned next_band(unsigned remaining,unsigned preferred){
 unsigned mask=remaining&preferred;if(!mask)mask=remaining;
 unsigned band=0;while(band<10&&!(mask&(1u<<band)))++band;return band;
}
enum {BG_ANALOG=1,BG_SCOPE=2,BG_BLE=4,BG_PPS=8,BG_MAP=16,BG_TIME=32,BG_COARSE=64,BG_GRACE=128};
/* A completed face can seed the original actual-analog ACK independently of
 * slower optional bands. Its original raster instant has a strict250ms cap. */
static void background_face(const struct ui_job *j,uint64_t end){
 if(ui_partial_state.background.face)return;
 ui_partial_state.background.face=0x8000;
 if(!(ui_partial_state.background.flags&BG_ANALOG)||end<ui_partial_state.background.render_ms||
    end-ui_partial_state.background.render_ms>250)return;
 unsigned key=ui_take();uint64_t now=k_uptime_get();
 if(now>=ui_partial_state.background.render_ms&&now-ui_partial_state.background.render_ms<=250&&
    job_current(j)&&r2_ui_analog(&model,now)){
  int64_t utc;bool coarse;
  if(r2_ui_display_time(&model,now,&utc,&coarse)){
   analog_ack.utc_us=utc;analog_ack.at_ms=now;analog_ack.completed_ms=end;
   analog_ack.context=j->context;analog_ack.hint=j->hint;analog_ack.policy=j->policy;analog_ack.pending=true;
   status.analog=true;drawn_context=j->context;drawn_hint=j->hint;
  }
 }
 ui_drop(key);
}
static void background_complete(const struct ui_job *j,uint64_t end){
 unsigned key=ui_take();
 if(job_current(j)){
  unsigned flags=ui_partial_state.background.flags,controls=ui_partial_state.background.control_flags;
  uint64_t elapsed=end-ui_partial_state.background.render_ms;
  bool whole=!(controls&1u)&&(ui_partial_state.background.page!=0||(controls&10u));
  bool coherent=whole&&elapsed<=250;
  /* frames remains a coherent whole-page counter. Independent strip/critical
   * completions have their own counters and cannot masquerade as a frame. */
  if(coherent){add(&status.frames);max_ms(&status.max_frame_ms,elapsed);}
  else if(whole)add(&status.missed);
  status.drawn_page=ui_partial_state.background.page;status.drawn_model_ms=ui_partial_state.background.model_ms;
  status.drawn_menu=!!(controls&2u);status.drawn_screensaver=!!(controls&1u);
  status.drawn_screensaver_position=ui_partial_state.background.position;status.last_frame_ms=(uint32_t)end;
  status.drawn_scope=!!(flags&BG_SCOPE);status.drawn_role=status.drawn_scope?j->m->role:R2_UI_NO_ROLE;
  status.drawn_ble=!!(flags&BG_BLE);status.drawn_pps=!!(flags&BG_PPS);status.drawn_map=!!(flags&BG_MAP);
  status.drawn_time=coherent&&!!(flags&BG_TIME);status.drawn_coarse=!!(flags&BG_COARSE);
  status.drawn_sync_grace=status.drawn_time&&!!(flags&BG_GRACE);status.drawn_utc_us=ui_partial_state.background.utc_us;
  status.temperature_qc=ui_partial_state.background.temperature_qc;status.temperature_valid=!!(controls&4u);
  memset(status.drawn_lamps,0,sizeof status.drawn_lamps);
  if(status.drawn_page==1&&!(controls&3u))r2_ui_rf_lamps(status.drawn_lamps,j->m,ui_partial_state.background.render_ms);
  drawn_context=j->context;drawn_hint=j->hint;
#ifdef R2_TZ_WIRE
  status.drawn_tz=ui_partial_state.background.zone;status.drawn_tz_flags=ui_partial_state.background.zone_flags;
#endif
 }
 ui_drop(key);
}

static void worker(void *a,void *b,void *c){
 (void)a;(void)b;(void)c;
 atomic_set(&ui_worker_phase,UI_UNSAFE);k_sleep(K_MSEC(20));
 int rc=atomic_get(&stop_requested)?0:r2_display_init(),terminal_error=rc;
 bool initialized=!rc,draw_failed=false,frame_returned=false;
 unsigned brightness=0;
 if(initialized&&!atomic_get(&stop_requested)){
  /* Base9/UNSAFE: a missing backlight cannot be a visible-output success. */
  rc=r2_display_brightness(100);frame_returned=true;draw_failed=rc<0;
  if(!rc)brightness=100;
 }
 unsigned key=ui_take();status.error=rc;if(brightness)status.brightness_percent=brightness;ui_drop(key);
 uint64_t due=k_uptime_get(),saver_since=0,saver_period=0;
 key=ui_take();input_changed=input_last_poll=input_idle_since=due;
 uint32_t observed_control=control_serial;ui_drop(key);
 atomic_set(&input_ready,atomic_get(&stop_requested)?0:1);
 while(!atomic_get(&stop_requested)){
  /* Urgent arbitration contains only the already bounded try-copy/scalars.
   * A forced9 interval here can donate a whole GM item before WAIT8. Actual
   * recovery and every optional operation still require base9 below. */
  if(!worker_priority(9-(int)(!rc&&urgent())))break;
  atomic_set(&ui_worker_phase,UI_READY);
  int input_rc=atomic_get(&input_error);if(input_rc){terminal_error=rc=input_rc;break;}
  if(rc){
   atomic_set(&ui_worker_phase,UI_UNSAFE);
   if(initialized&&k_thread_priority_get(&thread)==9&&!r2_display_recover()){rc=0;draw_failed=false;}
   if(rc){atomic_set(&ui_worker_phase,UI_READY);k_sleep(K_MSEC(20));continue;}
  }
  struct r2_ui_model m;struct r2_ui_status s;
  struct ui_job job={.m=&m,.s=&s,.background=false};
   if(!job_copy(&job,true)){atomic_set(&ui_worker_phase,UI_READY);k_sleep(K_MSEC(1));continue;}
  uint64_t now=k_uptime_get();
  if(job.control!=observed_control){observed_control=job.control;due=now;}
  unsigned bits=(unsigned)atomic_get(&ui_critical_dirty);
  if(bits&&!rc){
    job.bit=bits&UI_CLEAR?UI_CLEAR:bits&UI_TIME?UI_TIME:bits&UI_PULSE?UI_PULSE:UI_LOSS;
    job.tile=job.bit==UI_CLEAR?R2_UI_TILE_CLEAR:job.bit==UI_LOSS?R2_UI_TILE_LOSS:
     job.bit==UI_TIME?R2_UI_TILE_TIME:R2_UI_TILE_PPS;
   /* Each new attempt starts only after the prior synchronous job returned.
    * Event timestamps stay original for honest lateness/miss accounting. */
   job.deadline=now+100;
   if(!worker_priority(8))break;
   /* A policy-only probe cancels a regained/valid face before any window
    * command. The post-window probe/raster still uses the original deadline. */
    if(job.tile==R2_UI_TILE_LOSS&&r2_ui_tile_render_rect(NULL,&m,&s,now,job.tile,
        &ui_partial_state.zone,&job.view,raster_check,&job,(struct r2_ui_rect){0,0,1,1})&&job.view.analog){
    key=ui_take();if(job_current(&job))atomic_and(&ui_critical_dirty,~(atomic_val_t)UI_LOSS);ui_drop(key);continue;
   }
   int result=0;
    if(job.tile==R2_UI_TILE_LOSS){
     if(!ui_partial_state.loss.remaining||!same_tags(ui_partial_state.loss.tags,&job)){
      save_tags(ui_partial_state.loss.tags,&job);ui_partial_state.loss.remaining=1023;
      if(!s.screensaver)ui_partial_state.loss.remaining&=~6u;
     }
     unsigned heading=(s.screensaver?r2_ui_saver_top(s.screensaver_position)+23:62)/16;
     unsigned band=next_band(ui_partial_state.loss.remaining,1u<<heading);
     bool final=(ui_partial_state.loss.remaining&~(1u<<band))==0;
     result=draw_band(&job,band*16,final);
     if(!result&&job_current(&job))ui_partial_state.loss.remaining&=~(1u<<band);
    }else{
     unsigned y=job.tile==R2_UI_TILE_CLEAR?ui_partial_state.lamp_y:
      job.tile==R2_UI_TILE_TIME?16:s.screensaver?r2_ui_saver_top(s.screensaver_position)+2:19;
     result=region_piece(&job,job.tile==R2_UI_TILE_TIME?0:66,y,
      job.tile==R2_UI_TILE_TIME?80:9,job.tile==R2_UI_TILE_TIME?32:9,true);
   }
   frame_returned=true;draw_failed=draw_error(result);
   if(draw_error(result))rc=result;
   continue;
  }
  if(!optional_enter())continue;
   unsigned wanted=s.screensaver?50:100;
   if(brightness!=wanted){
    rc=r2_display_brightness(wanted);draw_failed=rc<0;
    key=ui_take();status.error=rc;status.ready=!rc;if(!rc){brightness=wanted;status.brightness_percent=wanted;}ui_drop(key);
    if(rc||!optional_enter())continue;
   }
  /* Optional model/provider/temperature/calendar work remains base9. */
  now=k_uptime_get();
   shadow_tick(&m,&s,now,!rc,job.context,job.hint,job.control);if(!optional_enter())continue;
   if(ui_partial_state.background.remaining&&!same_tags(ui_partial_state.background.tags,&job))ui_partial_state.background.remaining=0;
   if(!ui_partial_state.background.remaining&&now>=due&&!rc){
   uint64_t idle=job.idle_since;
   unsigned interval=s.page==1&&!s.screensaver?125:250;
   if((unsigned)atomic_get(&model_interval)!=interval)atomic_set(&model_interval,(int)interval);
   bool allowed=r2_ui_saver_allowed(&m,now);
   /* Eligibility can expire between publications. Leave the transition to
    * main and never cache a normal page under a saver layout. */
   if(s.screensaver&&!allowed){
    atomic_set(&ui_worker_phase,UI_READY);k_sleep(K_MSEC(1));continue;
   }
   bool enter=!s.screensaver&&allowed&&!s.menu.open&&!job.stable&&!job.wake_press&&
    now>=idle&&now-idle>=R2_UI_IDLE_MS;
   uint64_t period=s.screensaver?(now-saver_since)/R2_UI_SAVER_MOVE_MS:0;
   bool move=s.screensaver&&period!=saver_period;
   if(enter||move){
    uint32_t random=move?sys_rand32_get():0;
    key=ui_take();
    if(job_current(&job)&&input_idle_since==idle){
     control_change();status.screensaver=true;
     if(enter){saver_since=now;saver_period=0;status.screensaver_position=0;}
     else {saver_period=period;status.screensaver_position=r2_ui_saver_next(status.screensaver_position,random);}
     atomic_set(&model_requested,1);
    }
    ui_drop(key);continue;
   }
   if(!job_current(&job))continue;
   transmitter_age(&m,now,s.page==1&&!s.screensaver?125:250);
   bool details=optional_details(&m,job.context,job.hint,0);
   if(!optional_enter())continue;
#if defined(R2_LORA_PASSIVE_SLOW)||defined(R2_LORA_PASSIVE_FAST)
   if(details)optional_rf_view(&m);
#else
   (void)details;
#endif
   if(!optional_enter())continue;
   /* Human names already cached in the owned model are retained. Provider
    * diagnostics never gate a ui_partial_state job or create a new display lease. */
   m.temperature_valid=!r2_display_temperature(&m.temperature_qc);
   if(!optional_enter())continue;
   r2_ui_zone_cache_update(&ui_partial_state.zone,&m,now);
    job.background=true;job.render_ms=now;
   if(!r2_ui_render_try(framebuffer,&m,&s,now,raster_check,&job))continue;
    if(!optional_enter())continue;
    save_tags(ui_partial_state.background.tags,&job);
    ui_partial_state.background.render_ms=now;ui_partial_state.background.model_ms=m.sampled_ms;
    ui_partial_state.background.page=s.page;ui_partial_state.background.position=s.screensaver_position;
    ui_partial_state.background.temperature_qc=m.temperature_qc;
    ui_partial_state.background.control_flags=s.screensaver|(s.menu.open?2u:0u)|(m.temperature_valid?4u:0u)|(s.help?8u:0u);
    bool fresh=now>=m.sampled_ms&&now-m.sampled_ms<=2500&&(!m.ble_time_selected||r2_ui_ble_current(&m,now));
    int64_t utc=0;bool coarse=false,time=r2_ui_display_time(&m,now,&utc,&coarse);
    bool grace=r2_ui_sync_display_time(&s.sync_display,&m,now,&utc);if(grace){time=coarse=true;}
    ui_partial_state.background.utc_us=utc;
    ui_partial_state.background.flags=(!s.menu.open&&r2_ui_face_locked(&m,now)?BG_ANALOG:0)|
     (fresh&&m.scope_active?BG_SCOPE:0)|(fresh&&r2_ui_ble_current(&m,now)?BG_BLE:0)|
     (fresh&&m.follower_pps_valid?BG_PPS:0)|(fresh&&m.precision.map_valid?BG_MAP:0)|
     (!s.menu.open&&time?BG_TIME:0)|(coarse?BG_COARSE:0)|(grace?BG_GRACE:0);
#ifdef R2_TZ_WIRE
    ui_partial_state.background.zone=ui_partial_state.zone.zone;
    ui_partial_state.background.zone_flags=ui_partial_state.zone.flags;
    if(!time)ui_partial_state.background.zone_flags&=~R2_UI_TZ_LOCAL_VALID;
#endif
    ui_partial_state.background.remaining=1023;ui_partial_state.background.face=0;
    if(!s.help&&!s.menu.open&&!s.screensaver&&s.page==0)ui_partial_state.background.remaining&=~6u;
    unsigned top=s.screensaver?r2_ui_saver_top(s.screensaver_position)+15:54;
    if(!s.help&&!s.menu.open&&(s.page==0||s.screensaver))
     for(unsigned y=top;y<top+62;y++)ui_partial_state.background.face|=1u<<(y/16);
   }
   if(ui_partial_state.background.remaining){
    job.background=true;job.render_ms=ui_partial_state.background.render_ms;
    job.deadline=k_uptime_get()+100;
    unsigned band=next_band(ui_partial_state.background.remaining,ui_partial_state.background.face&1023u);
    int result=draw_band(&job,band*16,true);frame_returned=true;
    draw_failed=draw_error(result);if(draw_failed)rc=result;
    uint64_t end=k_uptime_get();
    /* No optional ACK can be current while background work is obsolete.
     * Preserve its conservative progress and arbitrate the urgent job now. */
    if(!urgent()&&worker_priority(9)&&!result&&job_current(&job)){
     ui_partial_state.background.remaining&=~(1u<<band);ui_partial_state.background.face&=~(1u<<band);
     background_face(&job,end);
     if(!ui_partial_state.background.remaining){background_complete(&job,end);due=end+(s.page==1&&!s.screensaver?125:250);}
    }
    continue;
  }
  atomic_set(&ui_worker_phase,UI_READY);k_sleep(K_MSEC(20));
 }
 atomic_set(&input_ready,0);
 (void)worker_priority(9);atomic_set(&ui_worker_phase,UI_UNSAFE);
 r2_shadow_session_stop(&shadow_session,k_uptime_get());
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
 r2_shadow_enable(false);
#endif
 struct r2_shadow_session_view final_shadow;r2_shadow_session_view(&shadow_session,&final_shadow);
 key=ui_take();
 final_shadow.warmup=status.shadow.warmup;final_shadow.warmup_skipped=status.shadow.warmup_skipped;
 final_shadow.warmup_missing=status.shadow.warmup_missing;final_shadow.warmup_resets=status.shadow.warmup_resets;
  final_shadow.settling_ms=status.shadow.settling_ms;shadow_publish_locked(&final_shadow);ui_drop(key);
 atomic_set(&ui_worker_phase,UI_UNSAFE);
 int released=draw_failed?(!initialized||!frame_returned?rc:r2_display_recover()):0;
 int closed=r2_display_off(),result=terminal_error?terminal_error:released?released:closed;
 key=ui_take();if(terminal_error)status.error=terminal_error;else if(!status.error&&result)status.error=result;
 status.stopped=true;status.ready=false;ui_drop(key);atomic_set(&close_state,result);
 atomic_set(&ui_worker_phase,UI_UNSAFE);
}
