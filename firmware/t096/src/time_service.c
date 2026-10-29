/* SPDX-License-Identifier: Apache-2.0 */
#include "time_service.h"
#include "rc_shadow.h"
#ifdef R2_TZ_AUTO
#include "time_zone_config.h"
static struct r2_tz_selector tz_selector;
#endif
#if defined(R2_SWITCHABLE_FIELD)
#include "ble_service.h"
#endif
static struct r2_capture shadow_edge;
#include <string.h>
#include <zephyr/kernel.h>
#define R2_LOCK_UNIT 11u
#include "health_lock.h"
struct input { int64_t at; struct r2_capture capture; uint8_t size; bool pps; uint8_t bytes[R2_TIME_CHUNK]; };
static struct input queue[R2_TIME_QUEUE];
static unsigned head,tail,count;
static uint32_t drops;
static enum r2_clock_fault pending;
static bool accepting,started;
static struct k_spinlock lock;
static struct r2_clock clock_state;
static struct r2_pps_precision precision;
static struct r2_source_clock source;
static int64_t last_capture;
static struct r2_source_pps_view source_pps;
int64_t r2_time_now_us(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }
static void put(const struct input *event)
{
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(accepting) {
    if(count==R2_TIME_QUEUE) {pending=R2_INPUT_OVERFLOW;if(drops<UINT32_MAX)++drops;}
    else {queue[head]=*event;head=(head+1)%R2_TIME_QUEUE;++count;}
  }
  k_spin_unlock(&lock,key);
}
void r2_time_input_fault(enum r2_clock_fault reason)
{
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(accepting)pending=reason;
  k_spin_unlock(&lock,key);
}
void r2_time_input_bytes(const uint8_t *data,size_t size,int64_t received)
{
  if(size>R2_TIME_CHUNK){r2_time_input_fault(R2_INPUT_OVERFLOW);return;}
  if(!size)return;
  struct input event={.at=received,.size=(uint8_t)size};memcpy(event.bytes,data,size);put(&event);
}
void r2_time_input_pps(int64_t edge) {struct input event={.at=edge,.pps=true};put(&event);}
void r2_time_input_precise_pps(struct r2_capture capture,int64_t arrival)
{struct input event={.at=arrival,.capture=capture,.pps=true};put(&event);}
void r2_time_init(void)
{
#ifdef R2_TZ_AUTO
  tz_selector=(struct r2_tz_selector){0};
#endif
  head=tail=count=drops=0;pending=R2_INPUT_OK;accepting=started=false;
  r2_clock_init(&clock_state,r2_time_now_us(),r2_gnss_association());
  r2_pps_precision_init(&precision);last_capture=0;
  r2_source_clock_init(&source);source_pps=(struct r2_source_pps_view){0};
}
void r2_time_start(void)
{
  if(started)return;
  started=true;k_spinlock_key_t key=k_spin_lock(&lock);accepting=true;k_spin_unlock(&lock,key);
  if(r2_gnss_start())r2_time_input_fault(R2_INPUT_DRIVER);
}
void r2_time_service(void)
{
  int64_t now=r2_time_now_us();
  if(started)r2_gnss_poll(now);
  for(unsigned i=0;i<8;i++) {
    struct input event;bool have=false;
    k_spinlock_key_t key=k_spin_lock(&lock);enum r2_clock_fault fault=pending;
    if(fault) {pending=R2_INPUT_OK;head=tail=count=0;}
    else if(count){event=queue[tail];tail=(tail+1)%R2_TIME_QUEUE;--count;have=true;}
    k_spin_unlock(&lock,key);
    /* UART/PPS can arrive during poll/dequeue. A loop-start sample can be
     * older than that event and falsely report future/stale input. */
    now=r2_time_now_us();
    if(fault){r2_clock_fault(&clock_state,fault,now);r2_pps_precision_init(&precision);r2_source_clock_invalidate(&source);last_capture=0;break;}
    if(!have)break;
    if(event.pps){
      r2_clock_pps(&clock_state,event.at,now);
      shadow_edge=event.capture;
      if(event.capture.epoch&&now>=event.at&&now-event.at<R2_INPUT_MAX_AGE_US){
        /* Evaluate the immutable prior source map before ingesting this PPS.
         * Healthy source publication follows its own preceding PPS by <=250ms,
         * so this one-second edge cannot use the newly fitted map. */
        uint64_t ns=0,generation=0;bool valid=event.capture.kind==R2_CAPTURE_PPS&&r2_source_clock_read(&source,event.capture,&ns,&generation);
        uint32_t observations=source_pps.observations;if(observations<UINT32_MAX)++observations;
        source_pps=(struct r2_source_pps_view){.tick=event.capture.tick,.epoch=event.capture.epoch,.ns=ns,.generation=generation,.observations=observations,.valid=valid};
        if(valid){uint64_t fraction=ns%1000000000;source_pps.phase_ns=fraction>=500000000?(int32_t)((int64_t)fraction-1000000000):(int32_t)fraction;
          for(unsigned j=0;j<source.count;j++)if(source.maps[j].generation==generation)source_pps.map_from=source.maps[j].from;}
        struct r2_capture available;
        if(r2_pps_precision_add(&precision,event.capture))last_capture=event.at;
        if(r2_gnss_capture_now(&available))
          (void)r2_source_clock_publish(&source,&precision,available);
        else r2_source_clock_invalidate(&source);
      }
    }
    else {r2_gnss_observe(event.bytes,event.size,event.at);r2_clock_bytes(&clock_state,event.bytes,event.size,event.at,now);}
  }
  r2_clock_advance(&clock_state,r2_time_now_us());
#ifdef R2_TZ_AUTO
  {
    k_spinlock_key_t key=k_spin_lock(&lock);
    r2_tz_selector_step(&tz_selector,&clock_state.position,
      (uint64_t)clock_state.view.mono_us,(uint64_t)clock_state.view.utc_us,
      !clock_state.stopped&&!clock_state.fatal&&clock_state.view.valid);
    k_spin_unlock(&lock,key);
  }
#endif
  if(clock_state.translated_epoch!=clock_state.view.source_epoch||!source.utc_bound)clock_state.translated_epoch=0;
#if defined(R2_SWITCHABLE_FIELD)
  if(!r2_switch_rc_active())
#endif
  {
    /* Bound uptime/capture sampling skew. Coarse time selects only the
     * whole-second UTC basis; its UART/servo fraction cannot set PPS phase.
     * Association remains unqualified. Advance raw time to this midpoint. */
    int64_t before=r2_time_now_us();struct r2_capture at;
    bool captured=r2_gnss_capture_now(&at);int64_t after=r2_time_now_us();
    if(captured&&after>=before&&after-before<=100){
      int64_t sampled=before+(after-before)/2;
      r2_clock_advance(&clock_state,sampled);
      if(!clock_state.stopped&&!clock_state.fatal&&clock_state.view.valid&&
         clock_state.view.fix_active&&clock_state.view.pps_present&&
         clock_state.view.utc_us>=0&&clock_state.view.utc_us<=INT64_MAX/1000){
        int bound=r2_source_utc_bind(&source,at,clock_state.view.utc_us*1000,
          clock_state.view.source_epoch,clock_state.view.association);
        if(bound<0){
          r2_clock_source_fence(&clock_state);
          if(!clock_state.fatal)bound=r2_source_utc_bind(&source,at,clock_state.view.utc_us*1000,
            clock_state.view.source_epoch,clock_state.view.association);
        }
        int64_t utc;
        if(bound>0&&!clock_state.fatal&&r2_source_utc_read(&source,at,clock_state.view.source_epoch,&utc)){
          clock_state.translated_utc_us=utc/1000;clock_state.translated_at_us=sampled;
          clock_state.translated_epoch=clock_state.view.source_epoch;
        }
      }
    }
  }

  if(r2_shadow_enabled()){
  if(shadow_edge.epoch)r2_shadow_reference(shadow_edge,&clock_state);
#ifndef R2_BOUNDARY_FIELD
#if defined(R2_SWITCHABLE_FIELD)
  if(!r2_switch_rc_active())
#endif
  r2_shadow_poll_normal();
#endif
  }
}
void r2_time_stop(void)
{
#ifdef R2_TZ_AUTO
  k_spinlock_key_t tz_key=k_spin_lock(&lock);
  tz_selector=(struct r2_tz_selector){0};clock_state.position.valid=false;
  k_spin_unlock(&lock,tz_key);
#endif
  k_spinlock_key_t key=k_spin_lock(&lock);accepting=false;head=tail=count=0;pending=R2_INPUT_OK;k_spin_unlock(&lock,key);
  r2_clock_fault(&clock_state,R2_INPUT_STOP,r2_time_now_us());
  r2_pps_precision_init(&precision);last_capture=0;
  r2_source_clock_invalidate(&source);
  r2_gnss_stop();
}
void r2_time_precision_snapshot(struct r2_pps_precision_view *out)
{
 int64_t now=r2_time_now_us(),age=last_capture?now-last_capture:INT64_MAX;
 bool fresh=last_capture&&age>=0&&age<R2_PPS_FRESH_US;
 *out=(struct r2_pps_precision_view){.epoch=precision.epoch,.age_us=age,
  .drift_ppb=precision.drift_ppb,.raw_rate_ppb=precision.raw_rate_ppb,.trim_ppb=r2_pps_mapping_trim_ppb(&precision),
  .fit_rms_ns=precision.fit_rms_ns,.prediction_rms_ns=precision.prediction_rms_ns,
  .samples=precision.count,.rejected=precision.rejected,
  .rate_valid=fresh&&precision.rate_valid,.fit_valid=fresh&&precision.fit_valid};
}
bool r2_time_source_read(struct r2_capture event,uint64_t *ns,uint64_t *generation)
{ return r2_source_clock_read(&source,event,ns,generation); }
int r2_time_close_result(void) { return r2_gnss_close_result(); }
void r2_time_snapshot(struct r2_clock_view *out,uint32_t *lost)
{
  r2_time_service();
  k_spinlock_key_t key=k_spin_lock(&lock);*out=clock_state.view;*lost=drops;out->input_error=r2_gnss_input_error();
  if(pending){out->source_available=out->fix_active=false;out->reason=pending;out->state=R2_TIME_HOLDOVER;}
  k_spin_unlock(&lock,key);
}

void r2_time_copy(struct r2_clock *out) { *out=clock_state; }

void r2_time_source_snapshot(struct r2_source_clock *out){*out=source;}

void r2_time_source_pps_snapshot(struct r2_source_pps_view *out){
 *out=source_pps;int64_t now=r2_time_now_us();if(!last_capture||now<last_capture||now-last_capture>=R2_PPS_FRESH_US)out->valid=false;
}

bool r2_time_source_utc_now(uint32_t source_epoch,int64_t *utc_us){
 struct r2_capture at;int64_t ns;
 if(!r2_gnss_capture_now(&at)||!r2_source_utc_read(&source,at,source_epoch,&ns))return false;
 *utc_us=ns/1000;return true;
}

#ifdef R2_TZ_AUTO
void r2_time_position_snapshot(struct r2_position *out){
 k_spinlock_key_t key=k_spin_lock(&lock);*out=clock_state.position;k_spin_unlock(&lock,key);
}
struct r2_tz r2_time_tz_at(uint64_t now,uint64_t utc,bool valid){
 k_spinlock_key_t key=k_spin_lock(&lock);
 struct r2_tz_selector selected=tz_selector;
 k_spin_unlock(&lock,key);
 return r2_tz_selector_value(&selected,R2_TZ_MODE,
  (struct r2_tz){R2_TZ_MANUAL_ID,R2_TZ_MANUAL_MIN},now,utc,valid);
}
void r2_time_tz_status(struct r2_tz_local_status *out,uint64_t now){
 k_spinlock_key_t key=k_spin_lock(&lock);
 struct r2_tz_selector s=tz_selector;struct r2_position p=clock_state.position;
 bool utc_live=clock_state.view.valid&&!clock_state.stopped&&!clock_state.fatal;
 bool window=clock_state.view.utc_us>=0&&r2_tz_db_window((uint64_t)clock_state.view.utc_us);
 k_spin_unlock(&lock,key);
 uint64_t age=p.valid&&p.received_us>=0&&now>=(uint64_t)p.received_us?
  now-(uint64_t)p.received_us:UINT64_MAX;
 uint64_t confirmed=s.selected&&now>=s.confirmed_at?now-s.confirmed_at:UINT64_MAX;
 *out=(struct r2_tz_local_status){.position_age_us=age,.confirmed_age_us=confirmed,
  .selected=R2_TZ_MODE==R2_TZ_MANUAL_ZONE?R2_TZ_MANUAL_ID:s.selected,
  .mode=R2_TZ_MODE,.reason=R2_TZ_MODE==R2_TZ_MANUAL_OFFSET?R2_TZ_LIVE:
    R2_TZ_MODE==R2_TZ_MANUAL_ZONE?(utc_live?(window?R2_TZ_LIVE:R2_TZ_DB_EXPIRED):R2_TZ_NO_UTC):s.reason,
  .hold=R2_TZ_MODE==R2_TZ_AUTOMATIC&&utc_live&&window&&s.selected&&confirmed<R2_TZ_HOLD_US&&
        (age>R2_TZ_POSITION_US||s.reason!=R2_TZ_LIVE)};
}
#endif
