/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
/* Actual worker and raster; synthetic ordinary-thread/SPI boundaries only.
 * No simulated latency below is hardware timing or CPU-cost evidence. */
#define main state_main
static void pipeline_clock(unsigned);
#define R2_UI_INIT_ADVANCE pipeline_clock
#include "ui_critical_state_test.c"
#undef main
static uint64_t begin_at,stop_at,loss_at,loss_until;
static unsigned regions,pixels,base9_delay,clear_jobs,loss_jobs,bottom_jobs;
static bool moving;
static unsigned saver_event;
static bool saver_drawn,awake_drawn;
static uint64_t saver_event_at;
static unsigned handoff_case;static bool handoff_injected,handoff_seen;
static uint64_t handoff_at;
static uint8_t panel[R2_UI_FRAME_BYTES];
static void pipeline_clock(unsigned ms){
 bool wait9=atomic_get(&ui_worker_phase)==UI_WAIT9;
 int before=priority;
 while(ms--){
  ++fake_now;struct k_thread *saved=current_thread;current_thread=NULL;
  if(handoff_case==3&&!handoff_injected&&atomic_get(&ui_worker_phase)==UI_WAIT8){
   atomic_set(&ui_critical_dirty,0);handoff_injected=true;
  }
  if(moving&&(fake_now==begin_at+1040||fake_now==begin_at+2060||(!saver_event&&fake_now==begin_at+4060))){
   fprintf(stderr,"move at%llu old lamp%u/%u saver%u PPS%u/%u\n",(unsigned long long)fake_now,
    ui_partial_state.lamp_lit,ui_partial_state.lamp_y,status.screensaver,
    ui_partial_state.metrics.pps[1][2],ui_partial_state.metrics.pps[1][3]);
   unsigned key=ui_take();control_change();
   if(fake_now==begin_at+1040){
    if(saver_event)status.page=1;
    input_idle_since=fake_now-R2_UI_IDLE_MS; /* real worker enters */
   }
   else if(fake_now==begin_at+2060)status.screensaver_position=2;
   else {status.screensaver=false;status.screensaver_position=0;input_idle_since=fake_now;}
   ui_drop(key);
  }
  if(fake_now%20==0){
   bool lost=fake_now>=loss_at&&fake_now<loss_until;
   if(saver_event&&fake_now==saver_event_at){
    assert(status.screensaver&&status.drawn_screensaver&&status.brightness_percent==50);
    saver_drawn=true;
    if(saver_event==2){
     const struct r2_ui_context replacement={.root=2,.boot=3,.train=4,.flags=R2_UI_CONTEXT_LIVE};
     r2_ui_context_update(&replacement);
    }
   }
   struct r2_ui_model m={.sampled_ms=fake_now,.unit="TEST",.platform_ready=true,.profile_supported=true,
    .ble_active=true,.role=lost?R2_UI_NO_ROLE:R2_UI_FOLLOWER,.scope_active=!lost,.authenticated_gm=!lost,
    .ble_time_selected=true,.ble_valid_until_us=lost?0:(fake_now+1000)*1000,
    .face_lock_until_us=lost?0:(fake_now+1000)*1000,.sync_attempt=lost,
    .precision={.authorized=!lost,.map_valid=!lost,.locked=!lost},.follower_pps_valid=!lost,
    .clock={.valid=!lost,.source_available=!lost,.fix_active=true,.pps_present=true,
     .pps_edges=(uint32_t)(fake_now/1000),.pps_age_us=(int64_t)(fake_now%1000)*1000,
     .utc_us=INT64_C(1792340000000000)+(int64_t)fake_now*1000}};
   if(saver_event>=3){
    bool dropped=fake_now>=saver_event_at&&fake_now<begin_at+4200;
    m.lora=(struct r2_lora_view){.transmitter=true,.requested=true,.active=true,
     .time_valid=!(dropped&&saver_event==3),.state=RC8_AVAILABLE,
     .age_ms=dropped&&saver_event==4?UINT32_MAX:1000};
    assert(r2_ui_saver_allowed(&m,fake_now));
   }
   r2_ui_publish(&m);
   if(saver_event&&fake_now>=saver_event_at){
    assert(!status.screensaver&&status.page==0&&!status.screensaver_position);
    assert(input_idle_since==saver_event_at);
    if(!status.drawn_screensaver&&status.brightness_percent==100)awake_drawn=true;
   }
  }
  if(fake_now>=stop_at)atomic_set(&stop_requested,1);
  partial_main();current_thread=saved;
  if(wait9&&before==8&&priority==9&&base9_delay){ms+=base9_delay;wait9=false;}
 }
}
static int pipeline_region(const uint8_t *fb,unsigned x,unsigned y,unsigned w,unsigned h,
 uint64_t deadline,r2_display_checkpoint_fn check,void *context){
 ++regions;assert(priority==8&&x+w<=80&&y+h<=160&&w*h*2<=5120);
 struct ui_job *j=context;
 if(handoff_injected&&!handoff_seen&&handoff_case<3&&!j->background){
  fprintf(stderr,"handoff case%u delay%llu\n",handoff_case,(unsigned long long)(fake_now-handoff_at));
  if(handoff_case==1)assert(fake_now-handoff_at<base9_delay/2);
  /* Physical error recovery deliberately retains its mandatory9 interval,
   * even when urgency arrives simultaneously. It must not be optimized out. */
  else assert(physical_recovery_checks==1);
  handoff_seen=true;
 }
 if(j->tile==R2_UI_TILE_CLEAR&&!j->background)++clear_jobs;
 if(j->tile==R2_UI_TILE_LOSS&&!j->background)++loss_jobs;
 if(j->background&&y>=144)++bottom_jobs;
 /* 2a command+arguments,2b command+arguments,2c command; each is a
  * synchronous sleep. Arbitration only at a completed command boundary. */
 for(unsigned command=0;command<3;command++){
  pipeline_clock(command==2?1:2);
  if(fake_now>=deadline)return R2_DISPLAY_EXPIRED;
  if(!check(context,R2_DISPLAY_WINDOW))return R2_DISPLAY_OBSOLETE;
 }
 if(!check(context,R2_DISPLAY_PREPARE))return R2_DISPLAY_OBSOLETE;
 for(unsigned row=0;row<h;row++)if(!check(context,R2_DISPLAY_EXPAND))return R2_DISPLAY_OBSOLETE;
 if(!check(context,R2_DISPLAY_SUBMIT))return R2_DISPLAY_OBSOLETE;
 assert(job_current(j));
 if(saver_event&&saver_drawn)assert(!j->s->screensaver&&j->s->page==0);
 if(j->background&&(ui_partial_state.background.flags&BG_ANALOG))assert(r2_ui_analog(&model,fake_now));
 if(!j->background&&(j->tile==R2_UI_TILE_TIME||j->tile==R2_UI_TILE_PPS))
  assert(!ui_partial_state.lamp_lit||ui_partial_state.lamp_y==(j->s->screensaver?r2_ui_saver_top(j->s->screensaver_position)+2:19));
 ++pixels;
 for(unsigned row=0;row<h;row++)memcpy(panel+(y+row)*80+x,fb+(y+row)*80+x,w);
 pipeline_clock(1);assert(check(context,R2_DISPLAY_RETIRED));
 if(handoff_case&&handoff_case<3&&!handoff_injected&&j->background){
  unsigned key=ui_take();request_tile(UI_TIME);ui_drop(key);
  handoff_injected=true;handoff_at=fake_now;
  if(handoff_case==2)return -EIO;
 }
 return fake_now>=deadline?R2_DISPLAY_EXPIRED:0;
}
static void run(unsigned delay,bool loss,bool move,unsigned handoff,unsigned event){
 reset();memset(panel,0xcc,sizeof panel);memset(&shadow_session,0,sizeof shadow_session);
 memset(&ui_shadow_observer,0,sizeof ui_shadow_observer);input_due=0;input_stable=input_last=0;
 mode=0;regions=pixels=clear_jobs=loss_jobs=bottom_jobs=0;base9_delay=delay;
 moving=move;
 saver_event=event;saver_drawn=awake_drawn=false;
 context_seen=false;
 handoff_case=handoff;handoff_injected=handoff_seen=false;
 begin_at=move?R2_UI_IDLE_MS+10000:1000;
 saver_event_at=begin_at+3200;
 loss_at=loss?(event?saver_event_at:begin_at+2800):UINT64_MAX;loss_until=loss?begin_at+4200:UINT64_MAX;
 fake_now=begin_at;stop_at=begin_at+7500;clock_hook=pipeline_clock;region_hook=pipeline_region;
 atomic_set(&ui_worker_phase,UI_UNSAFE);worker(NULL,NULL,NULL);
 clock_hook=NULL;region_hook=NULL;
 assert(status.stopped&&!status.ready&&priority==9&&atomic_get(&close_state)==0);
 assert(regions>=pixels&&pixels>30&&bottom_jobs>0);
 assert(ui_partial_state.metrics.jobs[0]&&ui_partial_state.metrics.jobs[1]);
 assert(ui_partial_state.metrics.seconds[1]>=3);
 assert(ui_partial_state.metrics.pps[0][2]>=3&&ui_partial_state.metrics.pps[0][3]>=3);
 assert(ui_partial_state.metrics.jobs[4]==(handoff==2?1u:0u));
 if(handoff){assert(handoff_injected);if(handoff<3)assert(handoff_seen);}
 if(handoff==2)assert(physical_recovery_checks==1);
 if(loss)assert(loss_jobs>=8);
 if(event)assert(saver_drawn&&awake_drawn&&status.brightness_percent==100);
 if(move&&!event){fprintf(stderr,"move final clear%u saverPPS%u/%u\n",clear_jobs,ui_partial_state.metrics.pps[1][2],ui_partial_state.metrics.pps[1][3]);
  assert(clear_jobs>=3&&ui_partial_state.metrics.pps[1][2]&&ui_partial_state.metrics.pps[1][3]);}
 printf("pipeline delay%u loss%u: %u windows,%u pixels,%u bottom,%u loss; seconds%u PPS%u/%u missed%u/%u\n",
  delay,loss,regions,pixels,bottom_jobs,loss_jobs,ui_partial_state.metrics.seconds[1],
  ui_partial_state.metrics.pps[0][2],ui_partial_state.metrics.pps[0][3],
  ui_partial_state.metrics.pps[0][4],ui_partial_state.metrics.pps[0][5]);
}
int main(void){
 state_main();
 run(0,false,false,0,0);run(30,false,false,0,0);run(0,true,false,0,0);run(30,true,false,0,0);run(30,false,true,0,0);
 run(200,false,false,1,0);run(200,false,false,2,0);run(200,false,false,3,0);
 run(30,true,true,0,1);run(30,false,true,0,2);
 run(30,false,true,0,3);run(30,false,true,0,4);
 /* Complete-scope check refuses the control->next20ms observation gap. */
 reset();struct r2_ui_model m;struct r2_ui_status s;struct ui_job j={.m=&m,.s=&s};
 unsigned key=ui_take();critical_observe(fake_now);ui_drop(key);assert(job_copy(&j,true));
 key=ui_take();control_change();status.page=1;ui_drop(key);assert(!job_copy(&j,true));
 puts("PASS actual full worker synthetic6-sleep windows, recurring real-input PPS, retained background progress, invalid loss strips and closure");
}
