/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Included once by the RC owner. Fitting and witnesses are single-owner work;
 * main reads only the bounded published snapshot under the existing mutex.
 */
#ifdef R2_PC_PROFILE
static void hold_reset(unsigned r){(void)r;}
static void hold_tick(uint64_t t){(void)t;}
static void hold_witness(const struct rc5_reference *r){(void)r;}
static void hold_prepare(const struct rc5_reference *r){(void)r;}
static bool hold_shadow_publish(struct r2_capture a){(void)a;return false;}
static bool hold_read(uint64_t *u,uint32_t *a){(void)u;(void)a;return false;}
int r2_dynamic_service_holdover_json(char *out,size_t size,bool data_only){(void)data_only;return snprintf(out,size,"{\"available\":false}");}
int r2_dynamic_service_holdover_test(uint32_t ms){(void)ms;return -ENOTSUP;}
#else
static struct r2_hold_rate hold_rate;
static struct r2_hold_trial hold_trial[2];
static struct {
 struct rc5_map map;uint64_t pair_tick,pair_ns,available;int64_t utc_offset;
 uint32_t epoch,resets;int32_t fast,chosen,short_rate,long_rate;
 uint32_t short_guard,long_guard;uint64_t fit_at,test_last;
 uint64_t raw_pair[3],trial_start[2],trial_anchor[2];int32_t trial_model[2][3];
 uint32_t trial[2][10],test[7],raw_sequence,channel;bool valid,selected;
} hold_view;
static struct rc5_map hold_estimate;
static uint32_t hold_serial,hold_resets;
static uint32_t hold_pair_serial;
static uint32_t hold_shadow_probe;
static uint64_t hold_parent,hold_root;
static atomic_t hold_test_request;
static uint64_t hold_test_until,hold_test_last;
static uint32_t hold_test_count,hold_test_max,hold_test_cross,hold_test_gaps;
static int32_t hold_test_result;
static void hold_test_publish(void){
 hold_view.test[0]=hold_test_until!=0;hold_view.test[1]=hold_test_count;hold_view.test[2]=hold_test_max;
 hold_view.test[3]=hold_test_cross;hold_view.test[4]=hold_test_gaps;hold_view.test[5]=(uint32_t)hold_test_result;
 hold_view.test[6]=(uint32_t)atomic_get(&hold_test_request);
 hold_view.test_last=hold_test_last;
}
static void hold_test_end(int result){
 hold_test_result=result;
 r2_timing_event(R2_TIMING_HOLD_TEST,result,hold_test_last,hold_test_count,hold_test_max,hold_test_cross);
 hold_test_until=0;
}
static void hold_trial_publish(unsigned i){
 const struct r2_hold_trial *t=&hold_trial[i];uint32_t *v=hold_view.trial[i];
 hold_view.trial_start[i]=t->start;hold_view.trial_anchor[i]=t->baseline.anchor_ns;
 hold_view.trial_model[i][0]=t->baseline.ppb;hold_view.trial_model[i][1]=t->selected;hold_view.trial_model[i][2]=t->baseline.phase_ns;
 v[0]=t->id;v[1]=t->probe_ms;v[2]=t->active;v[3]=t->witnesses;v[4]=t->max_ns[0];v[5]=t->max_ns[1];
 v[6]=t->cross_ms[0];v[7]=t->cross_ms[1];v[8]=t->gaps;v[9]=t->last>=t->start?(uint32_t)((t->last-t->start)/16000):0;
}
static void hold_tick(uint64_t tick){
 int32_t request=atomic_set(&hold_test_request,0);
 bool changed=request!=0;
 if(request==-1&&hold_test_until)hold_test_end(-ECANCELED);
 if(request< -1){
  hold_shadow_probe=request==-2?0:(uint32_t)(-(int64_t)request-1);
  for(unsigned i=0;i<2;i++)if(hold_trial[i].active)
   r2_timing_event(R2_TIMING_HOLD_END,2,hold_trial[i].last,hold_trial[i].id,hold_trial[i].max_ns[1],hold_trial[i].max_ns[0]);
  memset(hold_trial,0,sizeof hold_trial);++hold_resets;
  k_mutex_lock(&state_lock,K_FOREVER);hold_view.resets=hold_resets;hold_trial_publish(0);hold_trial_publish(1);k_mutex_unlock(&state_lock);
  r2_timing_event(R2_TIMING_HOLD_RESET,4,tick,hold_resets,hold_shadow_probe,0);
 }
 if(request>0){
  hold_test_count=hold_test_max=hold_test_cross=hold_test_gaps=0;hold_test_last=0;
  struct rc5_map frozen;
  if(hold_estimate.generation&&tick>=hold_view.map.effective&&tick-hold_view.map.effective<R2_CAPTURE_HZ/4&&
     r2_hold_abs(hold_estimate.phase_ns)<=250&&r2_hold_freeze(&hold_estimate,tick,hold_estimate.ppb,&frozen)){
   hold_estimate=frozen;hold_test_until=tick+(uint64_t)(uint32_t)request*16000;hold_test_result=0;
   r2_timing_event(R2_TIMING_HOLD_TEST,1,tick,(uint32_t)request,(uint32_t)frozen.ppb,frozen.generation);
  }else hold_test_result=-EAGAIN;
 }
 if(hold_test_until&&tick>hold_test_until+R2_CAPTURE_HZ){hold_test_end(-ETIMEDOUT);changed=true;}
 for(unsigned i=0;i<2;i++){
  struct r2_hold_trial *t=&hold_trial[i];
  if(t->active&&tick>=t->start&&tick-t->start>(uint64_t)(t->probe_ms+1000)*16000){
   t->active=false;r2_timing_event(R2_TIMING_HOLD_END,1,t->last,t->id,t->max_ns[1],t->max_ns[0]);
   k_mutex_lock(&state_lock,K_FOREVER);hold_trial_publish(i);k_mutex_unlock(&state_lock);
  }
 }
 if(changed){k_mutex_lock(&state_lock,K_FOREVER);hold_view.map=hold_estimate;hold_test_publish();k_mutex_unlock(&state_lock);}
}
_Static_assert(sizeof hold_rate+sizeof hold_estimate<=1024,"HOLD selector and output RAM");
_Static_assert(sizeof hold_view<=336,"HOLD published diagnostics RAM");
_Static_assert(sizeof hold_rate+sizeof hold_estimate+sizeof hold_trial+sizeof hold_view+
 sizeof hold_serial+sizeof hold_resets+sizeof hold_test_request+sizeof hold_test_until+
 sizeof hold_test_last+sizeof hold_test_count+sizeof hold_test_max+sizeof hold_test_cross+
 sizeof hold_test_gaps+sizeof hold_test_result+sizeof hold_parent+sizeof hold_root+sizeof hold_shadow_probe+sizeof hold_pair_serial<=1280,"Complete HOLD mutable RAM budget");
static void hold_reset(unsigned reason){
 if(hold_test_until)hold_test_end(-ECANCELED);
 atomic_set(&hold_test_request,0);
 hold_parent=hold_root=0;
 hold_pair_serial=0;
 hold_shadow_probe=0;
 hold_test_count=hold_test_max=hold_test_cross=hold_test_gaps=0;hold_test_last=0;hold_test_result=-ECANCELED;
 for(unsigned i=0;i<2;i++)if(hold_trial[i].active)
  r2_timing_event(R2_TIMING_HOLD_END,2,hold_trial[i].last,hold_trial[i].id,hold_trial[i].max_ns[1],hold_trial[i].max_ns[0]);
 memset(&hold_rate,0,sizeof hold_rate);memset(hold_trial,0,sizeof hold_trial);memset(&hold_estimate,0,sizeof hold_estimate);
 ++hold_resets;
 k_mutex_lock(&state_lock,K_FOREVER);memset(&hold_view,0,sizeof hold_view);hold_view.resets=hold_resets;k_mutex_unlock(&state_lock);
 r2_timing_event(R2_TIMING_HOLD_RESET,(int)reason,0,hold_resets,0,0);
}
static void hold_witness(const struct rc5_reference *r){
 if(!r->valid||!r->rx||r->parent_tx_ns>INT64_MAX)return;
 uint64_t parent=routes.node[parent_index].id;
 if((hold_parent&&hold_parent!=parent)||(hold_root&&hold_root!=r->origin.root)||
    (hold_view.valid&&(hold_view.epoch!=capture_epoch||hold_view.utc_offset!=current_origin.utc_offset)))hold_reset(3);
 hold_parent=parent;hold_root=r->origin.root;
 /* Score BEFORE this pair enters either frequency history or delivered map.
  * Compare the same calibrated phase coordinate used by the RC clock. */
 uint64_t remote=r->parent_tx_ns;
 if(direct_phase_bias<0){if(remote<(uint64_t)(-(int64_t)direct_phase_bias))return;remote-=(uint64_t)(-(int64_t)direct_phase_bias);}
 else {if(remote>UINT64_MAX-(uint32_t)direct_phase_bias)return;remote+=(uint32_t)direct_phase_bias;}
 if(hold_test_until&&r->rx>=hold_estimate.effective){
  uint64_t value,elapsed;
  if(rc5_map_project(&hold_estimate,r->rx,&value,&elapsed)){
   uint64_t error=value>remote?value-remote:remote-value;
   uint32_t age=(uint32_t)((r->rx-hold_estimate.effective)/16000);
   if(error>hold_test_max)hold_test_max=error<UINT32_MAX?(uint32_t)error:UINT32_MAX;
   if(error>=1000&&!hold_test_cross)hold_test_cross=age+1;
   if(r->rx-(hold_test_last?hold_test_last:hold_estimate.effective)>R2_CAPTURE_HZ/4)++hold_test_gaps;
   hold_test_last=r->rx;++hold_test_count;
  }else hold_test_result=-ERANGE;
  if(r->rx>=hold_test_until)hold_test_end(hold_test_result);
 }
 for(unsigned i=0;i<2;i++){
  struct r2_hold_trial *t=&hold_trial[i];if(!t->active)continue;
  if(r->rx>=t->start&&r->rx-t->start>(uint64_t)(t->probe_ms+1000)*16000){
   t->active=false;r2_timing_event(R2_TIMING_HOLD_END,1,t->last,t->id,t->max_ns[1],t->max_ns[0]);continue;
  }
  if(!r2_hold_trial_score(t,r->rx,remote))continue;
  if(r->rx-t->start>=(uint64_t)t->probe_ms*16000){
   t->active=false;r2_timing_event(R2_TIMING_HOLD_END,0,t->last,t->id,t->max_ns[1],t->max_ns[0]);
  }
 }
 k_mutex_lock(&state_lock,K_FOREVER);
 hold_view.raw_pair[0]=r->rx;hold_view.raw_pair[1]=r->parent_tx_ns;hold_view.raw_pair[2]=r->completed_at;
 if(hold_pair_serial<UINT32_MAX)++hold_pair_serial;
 hold_view.raw_sequence=hold_pair_serial;k_mutex_unlock(&state_lock);
 bool sampled=r2_hold_observe(&hold_rate,r->rx,r->parent_tx_ns,r->completed_at,r->channel);
 r2_hold_refresh(&hold_rate,r->completed_at);
 if(sampled){
  k_mutex_lock(&state_lock,K_FOREVER);hold_view.pair_tick=r->rx;hold_view.pair_ns=r->parent_tx_ns;
  hold_view.available=r->completed_at;hold_view.channel=r->channel;k_mutex_unlock(&state_lock);
 }
 if(r2_radio_audit_capturing()){
  struct r2_radio_audit_record q={.kind=R2_AUDIT_HOLD,.tick=r->rx,.ns=r->parent_tx_ns,
   .available=r->completed_at,.generation=hold_pair_serial,.channel=r->channel,.raw=sampled,
   .epoch=capture_epoch,.root_epoch=hold_resets};
  r2_radio_audit_put(&q);
 }
}
static void hold_prepare(const struct rc5_reference *r){
 if(!local_clock.count||!fit.valid||!fit_rms_valid)return;
 struct r2_capture entry;if(!r2_capture_now(&entry)||entry.epoch!=capture_epoch||entry.tick<r->completed_at)return;
 const struct rc5_map *fast=&local_clock.map[(local_clock.head+3)%4];
 uint64_t span=fit.count>1?fit.rx[(fit.head+15)%16]-fit.rx[(fit.head+16-fit.count)%16]:0;
 uint64_t guard=span?(uint64_t)fit_rms*3*R2_CAPTURE_HZ/span:UINT32_MAX;
 bool selected;int32_t chosen=r2_hold_choose(&hold_rate,entry.tick,fast->ppb,guard<UINT32_MAX?(uint32_t)guard:UINT32_MAX,&selected);
 struct rc5_map next;
 if(!r2_hold_freeze(fast,entry.tick,chosen,&next))return;
 uint64_t previous,elapsed;
 if(hold_estimate.generation&&rc5_map_project(&hold_estimate,entry.tick,&previous,&elapsed)){
  int64_t delta=next.anchor_ns>=previous?(int64_t)(next.anchor_ns-previous):-(int64_t)(previous-next.anchor_ns);
  if(r2_hold_abs(delta)<=1000000){
   int64_t phase=(int64_t)next.phase_ns+delta;
   if(phase>=INT32_MIN&&phase<=INT32_MAX){next.anchor_ns=previous;next.phase_ns=(int32_t)phase;}
  }
 }
 if(!hold_test_until)hold_estimate=next;
 /* Avoid a warm-up bias: starts require an actually measured sub-us fast
  * fit and a settled delivered phase; later bad witnesses are never filtered. */
 if(!hold_test_until&&fit.count==16&&fit_rms<1000&&r2_hold_abs(next.phase_ns)<=250){
  for(unsigned i=0;i<2;i++)if(!hold_trial[i].active){
   struct r2_hold_trial *t=&hold_trial[i];
   /* Keep a completed endpoint observable for the host without another ring. */
   if(t->id&&t->last&&entry.tick>=t->last&&entry.tick-t->last<R2_CAPTURE_HZ/2)continue;
   uint32_t probe=hold_shadow_probe?hold_shadow_probe:i?(t->probe_ms==12000?32000:t->probe_ms==32000?64000:t->probe_ms==64000?120000:12000):2000;
   if(hold_shadow_probe&&i==1&&!t->id&&entry.tick-hold_trial[0].start<(uint64_t)probe*8000)continue;
   if(hold_serial==UINT32_MAX)break;
   *t=(struct r2_hold_trial){.baseline={next.anchor_ns,fast->ppb,next.phase_ns},.start=entry.tick,.id=++hold_serial,.probe_ms=probe,.selected=chosen,.active=true};
   r2_timing_event(R2_TIMING_HOLD_START,chosen,t->start,t->id,(uint32_t)fast->ppb,probe);
  }
 }
 k_mutex_lock(&state_lock,K_FOREVER);
 hold_view.map=hold_estimate;
 hold_view.utc_offset=current_origin.utc_offset;hold_view.epoch=capture_epoch;
 hold_view.fast=fast->ppb;hold_view.chosen=chosen;hold_view.selected=selected;hold_view.valid=true;
 hold_view.short_rate=hold_rate.short_fit.rate;hold_view.short_guard=hold_rate.short_fit.guard;
 hold_view.long_rate=hold_rate.long_fit.rate;hold_view.long_guard=hold_rate.long_fit.guard;hold_view.fit_at=hold_rate.refreshed;
 for(unsigned i=0;i<2;i++)hold_trial_publish(i);
 hold_test_publish();
 k_mutex_unlock(&state_lock);
}
static bool __attribute__((noinline)) hold_shadow_publish(struct r2_capture activation){
 if(!hold_estimate.generation||!r2_shadow_enabled()||activation.epoch!=capture_epoch||
    hold_estimate.effective>UINT64_MAX-R2_HOLD_MAX_TICKS)return false;
 struct rc_shadow_map m={.normal=true,.local_hold=true,.normal_map={.valid=true},.map=hold_estimate,
  .normal_until=hold_estimate.effective+R2_HOLD_MAX_TICKS,.lo=activation.tick,.epoch=capture_epoch,
  .parent=routes.node[parent_index].id,.parent_boot=timing.parent_boot,
  .root=current_origin.root,.root_boot=current_origin.boot,.root_epoch=current_origin.epoch,
  .parent_epoch=timing.parent_epoch,.train=timing.parent_train,.accepted=timing.accepted,
  .issued=current_origin.issued,.source_expires=current_origin.expires,.utc_offset=current_origin.utc_offset};
 r2_shadow_normal_map(&m);return true;
}
static bool hold_read(uint64_t *utc,uint32_t *age){
 struct r2_capture at;if(!r2_capture_now(&at))return false;
 bool ok=false;uint64_t ns,elapsed;
 k_mutex_lock(&state_lock,K_FOREVER);
 if(hold_view.valid&&at.epoch==hold_view.epoch&&at.tick>=hold_view.available&&
    at.tick>=hold_view.map.effective&&at.tick-hold_view.map.effective<=R2_HOLD_MAX_TICKS&&
    rc5_map_project(&hold_view.map,at.tick,&ns,&elapsed)&&ns<=INT64_MAX){
  int64_t offset=hold_view.utc_offset;
  if(!((offset>0&&(int64_t)ns>INT64_MAX-offset)||(offset<0&&(int64_t)ns<INT64_MIN-offset))){
   int64_t value=(int64_t)ns+offset;
   if(value>0){*utc=(uint64_t)value/1000;*age=(uint32_t)((at.tick-hold_view.map.effective+15999)/16000);ok=true;}
  }
 }
 k_mutex_unlock(&state_lock);return ok;
}
int r2_dynamic_service_holdover_json(char *out,size_t size,bool data_only){
 k_mutex_lock(&state_lock,K_FOREVER);
 struct r2_capture now={0};bool captured=r2_capture_now(&now);
 if(data_only){
  int n=snprintf(out,size,"{\"schema\":2,\"now_tick\":%llu,\"epoch\":%u,\"resets\":%u,\"raw_pair\":[%llu,%llu,%llu,%u],\"pair\":[%llu,%llu,%llu],\"channel\":%u,\"fit_at\":%llu}",
   (unsigned long long)now.tick,hold_view.epoch,hold_view.resets,
   (unsigned long long)hold_view.raw_pair[0],(unsigned long long)hold_view.raw_pair[1],(unsigned long long)hold_view.raw_pair[2],hold_view.raw_sequence,
   (unsigned long long)hold_view.pair_tick,(unsigned long long)hold_view.pair_ns,(unsigned long long)hold_view.available,hold_view.channel,(unsigned long long)hold_view.fit_at);
  k_mutex_unlock(&state_lock);return n<0||(size_t)n>=size?-ENOSPC:n;
 }
 bool available=captured&&hold_view.valid&&now.epoch==hold_view.epoch&&now.tick>=hold_view.map.effective&&now.tick-hold_view.map.effective<=R2_HOLD_MAX_TICKS;
 int n=snprintf(out,size,"{\"schema\":2,\"policy\":\"BLE_RATE_ONLY_T096_V2\",\"now_tick\":%llu,\"available\":%s,\"selected\":%s,\"resets\":%u,\"epoch\":%u,\"pair\":[%llu,%llu,%llu],\"raw_pair\":[%llu,%llu,%llu,%u],\"channel\":%u,\"fit_at\":%llu,\"test_last\":%llu,\"rates\":[%d,%d,%d,%u,%d,%u],\"map\":[%llu,%llu,%d,%d,%u],\"trials\":[",
  (unsigned long long)now.tick,available?"true":"false",hold_view.selected?"true":"false",hold_view.resets,hold_view.epoch,
  (unsigned long long)hold_view.pair_tick,(unsigned long long)hold_view.pair_ns,(unsigned long long)hold_view.available,
  (unsigned long long)hold_view.raw_pair[0],(unsigned long long)hold_view.raw_pair[1],(unsigned long long)hold_view.raw_pair[2],hold_view.raw_sequence,
  hold_view.channel,(unsigned long long)hold_view.fit_at,(unsigned long long)hold_view.test_last,
  hold_view.fast,hold_view.chosen,hold_view.short_rate,hold_view.short_guard,hold_view.long_rate,hold_view.long_guard,
  (unsigned long long)hold_view.map.anchor_tick,(unsigned long long)hold_view.map.anchor_ns,hold_view.map.ppb,hold_view.map.phase_ns,hold_view.map.generation);
 for(unsigned i=0;i<2&&n>0&&(size_t)n<size;i++){
  const uint32_t *v=hold_view.trial[i];int k=snprintf(out+n,size-(size_t)n,"%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]",i?",":"",v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9]);
  if(k<0){n=-1;break;}n+=k;
 }
 if(n>0&&(size_t)n<size){
  const uint32_t *v=hold_view.test;
  int k=snprintf(out+n,size-(size_t)n,"],\"trial_models\":[[%llu,%llu,%d,%d,%d],[%llu,%llu,%d,%d,%d]],\"test\":[%u,%u,%u,%u,%u,%d,%d],\"bound_known\":false,\"temperature_steering\":false}",
   (unsigned long long)hold_view.trial_start[0],(unsigned long long)hold_view.trial_anchor[0],hold_view.trial_model[0][0],hold_view.trial_model[0][1],hold_view.trial_model[0][2],
   (unsigned long long)hold_view.trial_start[1],(unsigned long long)hold_view.trial_anchor[1],hold_view.trial_model[1][0],hold_view.trial_model[1][1],hold_view.trial_model[1][2],
   v[0],v[1],v[2],v[3],v[4],(int32_t)v[5],(int32_t)v[6]);
  if(k<0)n=-1;else n+=k;
 }
 k_mutex_unlock(&state_lock);return n<0||(size_t)n>=size?-ENOSPC:n;
}
int r2_dynamic_service_holdover_test(uint32_t ms){
 bool shadow=(ms&UINT32_C(0x80000000))!=0;uint32_t duration=ms&UINT32_C(0x7fffffff);
 if(duration&&(duration<1000||duration>120000))return -EINVAL;
 struct r2_capture at;if(!r2_capture_now(&at))return -EAGAIN;
 k_mutex_lock(&state_lock,K_FOREVER);
 bool ready=!ms||shadow||(hold_view.valid&&!hold_view.test[0]&&at.epoch==hold_view.epoch&&at.tick>=hold_view.map.effective&&at.tick-hold_view.map.effective<R2_CAPTURE_HZ/4&&r2_hold_abs(hold_view.map.phase_ns)<=250);
 k_mutex_unlock(&state_lock);
 if(!ready)return -EAGAIN;
 int32_t request=shadow?(duration?-(int32_t)duration-1:-2):ms?(int32_t)ms:-1;
 return atomic_cas(&hold_test_request,0,request)?0:-EBUSY;
}
#endif
