#include "controller_diag.h"
/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "gm_diagnostics.h"
#ifdef R2_GM7_NATIVE
#include "gm7_service.h"
#endif
#include "health.h"
#include "timing_events.h"
#include "initiator_diag.h"
#include "watchdog.h"
#include "time_service.h"
#include "capture.h"
#include "gnss_aid.h"
#ifdef R2_PC_PROFILE
#include "pc_experiment.h"
#endif
#ifdef R2_DC_PROFILE
#include "direct_connect.h"
#include "dc_gnss.h"
#endif
#ifdef R2_USB_SOF_PROBE
#include "usb_sof_probe.h"
#endif
#ifdef R2_USB_SOURCE_STREAM
#include "usb_source.h"
#endif
#include "ui.h"
#ifdef R2_LORA
#include "lora_service.h"
#include "lora_watchdog.h"
#endif
#ifdef R2_NATIVE_BLE
#include "ble_service.h"
#include "gnss_aid.h"
#include "radio_service.h"
#include "radio_audit.h"
#include "radio_capture.h"
#include "diagnostic.h"
#ifdef R2_RC_PROFILE
#include "rc_unicast.h"
#include "rc_dynamic_service.h"
#endif
#include "r2_enrollment.h"
#endif
#include "rc_shadow.h"
#include "rc_direct_calibration.h"
#include "build_identity.h"
#ifdef R2_RC_PROFILE
#include <hal/nrf_clock.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct trace_record { uint64_t ms; uint32_t kind; int result; };
static struct trace_record trace[R2_TRACE_COUNT];
static unsigned trace_count, trace_lost;
static struct r2_identity identity;
static struct r2_maint maintenance;
static uint32_t reply_sequence, last_request, rejected, heartbeat;
static bool startup_decided, platform_ready, boot_timeout;
/* The main owner finishes formatting before console_reply copies the frame
 * into its owned queue. Share that lifetime instead of duplicating 2976B RAM;
 * r2_frame explicitly supports this overlap. Capacities are unchanged. */
static union {char json[R2_REPLY_BYTES-96];char frame[R2_REPLY_BYTES];} reply_buffers;
#define json reply_buffers.json
#define frame reply_buffers.frame
static bool diagnostic_busy(void)
{
#ifdef R2_NATIVE_BLE
  return r2_diag_busy();
#else
  return false;
#endif
}
static void event(uint32_t kind,int result)
{
  if(kind==5&&result>=0)return; /* No routine command trace after boot. */
  if(trace_count==R2_TRACE_COUNT) { if(trace_lost<UINT32_MAX) ++trace_lost; return; }
  trace[trace_count++]=(struct trace_record){.ms=k_uptime_get(),.kind=kind,.result=result};
}
#ifdef R2_NATIVE_BLE
static const char *error_name(int code){
 return !code?"NONE":code==-ETIMEDOUT?"ETIMEDOUT":code==-ESTALE?"ESTALE":
        code==-ERANGE?"ERANGE":code==-EFAULT?"EFAULT":code==-ENOBUFS?"ENOBUFS":"OTHER";
}
#endif
static void __attribute__((noinline)) clock_reply(const struct r2_request *r,uint64_t arrival)
{
#ifdef R2_DC_PROFILE
  uint32_t gnss_bridge[5];r2_dc_gnss_status(gnss_bridge);
#endif
  struct r2_clock_view t;uint32_t drops;r2_time_snapshot(&t,&drops);
  struct r2_pps_precision_view q;r2_time_precision_snapshot(&q);
  struct r2_gnss_aid_status aid;r2_gnss_aid_status(&aid);
  struct r2_source_pps_view source_pps;r2_time_source_pps_snapshot(&source_pps);
  int n=snprintf(json,sizeof(json),
    "{\"source\":\"%s\",\"boot\":\"%016llx\",\"state\":%u,\"valid\":%s,"
    "\"utc_us\":%lld,\"mono_us\":%lld,\"gps_age_us\":%lld,\"pps_age_us\":%lld,\"rx_age_us\":%lld,\"gga_age_us\":%lld,\"gga_hdop_centi\":%u,"
    "\"input_error\":%d,\"rate_ppb\":%d,\"phase_us\":%lld,\"rate_valid\":%s,\"source_available\":%s,"
    "\"generation\":%u,\"source_epoch\":%u,\"pairs\":%u,\"accepted\":%u,\"rejected\":%u,"
    "\"faults\":%u,\"reason\":%u,\"association\":%u,\"pps_edges\":%u,\"pps_rejected\":%u,"
    "\"pps_present\":%s,\"fix_active\":%s,\"sats_used\":%u,\"gga_count\":%u,\"queue_drops\":%u,"
    "\"pps_capture_epoch\":%llu,\"pps_capture_age_us\":%lld,\"pps_fit_samples\":%u,"
    "\"pps_rate_valid\":%s,\"pps_fit_valid\":%s,\"pps_drift_ppb\":%d,\"pps_trim_ppb\":%d,"
    "\"pps_fit_rms_ns\":%u,\"pps_prediction_rms_ns\":%u,\"native_capture_error\":%d,"
    "\"source_pps\":[%llu,%llu,%llu,%llu,%llu,%d,%u,%s],\"rmc\":[%u,%u],\"aid\":[%u,%u,%u,%u,%d,%s,%llu],"
#ifdef R2_DC_PROFILE
    "\"dc_gnss_bridge\":[%u,%u,%u,%u,%u],"
#endif
    "\"precision\":\"UNKNOWN\",\"timescale\":\"POSIX_UTC_ESTIMATE\",\"uncertainty_us\":null}",
    R2_SOURCE_SHA,(unsigned long long)identity.boot,t.state,t.valid?"true":"false",
    (long long)t.utc_us,(long long)t.mono_us,(long long)t.gps_age_us,(long long)t.pps_age_us,(long long)t.rx_age_us,(long long)t.gga_age_us,(unsigned)t.gga_hdop_centi,
    t.input_error,t.rate_ppb,(long long)t.phase_us,t.rate_valid?"true":"false",t.source_available?"true":"false",
    t.generation,t.source_epoch,t.pairs,t.accepted,t.rejected,t.faults,t.reason,t.association,
    t.pps_edges,t.pps_rejected,t.pps_present?"true":"false",t.fix_active?"true":"false",
    t.sats_used,t.gga_count,drops,(unsigned long long)q.epoch,(long long)q.age_us,q.samples,
    q.rate_valid?"true":"false",q.fit_valid?"true":"false",q.drift_ppb,q.trim_ppb,q.fit_rms_ns,q.prediction_rms_ns,r2_capture_error(),(unsigned long long)source_pps.tick,(unsigned long long)source_pps.epoch,(unsigned long long)source_pps.ns,(unsigned long long)source_pps.generation,(unsigned long long)source_pps.map_from,source_pps.phase_ns,source_pps.observations,source_pps.valid?"true":"false",t.rmc_status,t.rmc_mode,aid.attempts,aid.completed,aid.aborted,aid.ack,aid.result,aid.busy?"true":"false",(unsigned long long)aid.utc_ms
#ifdef R2_DC_PROFILE
    ,gnss_bridge[0],gnss_bridge[1],gnss_bridge[2],gnss_bridge[3],gnss_bridge[4]
#endif
    );
  if(n<0||(size_t)n>=sizeof(json)||reply_sequence==UINT32_MAX)return;
  n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);
  if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);
}
#ifdef R2_NATIVE_BLE
/* Explicit read-only query, never ISR or periodic logging. UINT32_MAX means
 * unavailable. Stack painting tracks historical high-water use, not just the
 * shallow query frame. Native main and shared BLE workqueue have static lives. */
/* Same linker-owned stack bounds and painting convention as pinned Zephyr's
 * thread_analyzer ISR scan. Main-only explicit query: no ISR scan/format/write,
 * allocation, guessed RAM or reset persistence claim. Historical minimum free
 * prefix; sampling perturbs main only and is not a proof against every nesting. */
#if defined(CONFIG_ARM) && defined(CONFIG_INIT_STACKS)
K_KERNEL_STACK_ARRAY_DECLARE(z_interrupt_stacks, CONFIG_MP_MAX_NUM_CPUS,
                             CONFIG_ISR_STACK_SIZE);
#endif
static uint32_t isr_stack_unused(void)
{
#if defined(CONFIG_ARM) && defined(CONFIG_INIT_STACKS)
  const volatile uint8_t *p=(const volatile uint8_t *)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]);
  size_t n=K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]),i=0;
  while(i<n&&p[i]==0xaa)++i;
  return (uint32_t)i;
#else
  return UINT32_MAX;
#endif
}
static uint32_t stack_unused(bool system_workqueue)
{
#ifdef CONFIG_INIT_STACKS
  size_t unused;
  struct k_thread *thread=system_workqueue?&k_sys_work_q.thread:k_current_get();
  if(!k_thread_stack_space_get(thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
  (void)system_workqueue;return UINT32_MAX;
}
#endif
static void __attribute__((noinline)) reply(const struct r2_request *r,uint64_t arrival,int result)
{
#ifdef R2_PC_PROFILE
  if(r->op==R2_PC||r->op==R2_PC_RUN||r->op==R2_PC_CYCLE||r->op==R2_PC_ALIGN){
    int n=r2_pc_json(json,sizeof json,r->op==R2_PC?(uint32_t)r->nonce:UINT32_MAX,result);
    if(n<=0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    int extra=snprintf(json+n-1,sizeof json-(size_t)n+1,",\"source\":\"%s\",\"commit\":\"%s\",\"boot\":\"%016llx\",\"silicon\":\"%s\",\"unit\":\"%s\"}",R2_SOURCE_SHA,R2_CHILD_COMMIT,(unsigned long long)identity.boot,identity.silicon,identity.unit);
    if(extra<0||(size_t)extra>=sizeof json-(size_t)n+1)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
#endif
#ifdef R2_LORA
  if(r->op==R2_LORA_CONTROL){
    int n=r2_lora_json(json,sizeof json,result);
    if(n<=0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    int extra=snprintf(json+n-1,sizeof json-(size_t)n+1,",\"source\":\"%s\"}",R2_SOURCE_SHA);
    if(extra<=0||(size_t)extra>=sizeof json-(size_t)n+1)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);
    if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);
    return;
  }
#endif
  if(r->op==R2_CLOCK){clock_reply(r,arrival);return;}
#ifdef R2_DC_PROFILE
  if(r->op==R2_PRECISION){
    uint32_t rms=0;unsigned samples=0;bool valid=r2_dc_metric(&rms,&samples);
    int n=snprintf(json,sizeof json,"{\"source\":\"%s\",\"boot\":\"%016llx\",\"image_profile\":\"DC\",\"metric\":\"GNSS_MAPPED_USB_SOF_SPAN_PER_FRAME_RMS\",\"rms_ns\":%u,\"rms_valid\":%s,\"samples\":%u,\"relay_hops\":0,\"absolute_utc_qualified\":false}",R2_SOURCE_SHA,(unsigned long long)identity.boot,rms,valid?"true":"false",samples);
    if(n<0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);
    if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);
    return;
  }
#endif
  if(r->op==R2_EVENTS){
    int n=r2_timing_json(json,sizeof json,identity.boot,(uint32_t)r->nonce);
    if(n<=0||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);
    if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);
    return;
  }
#ifdef R2_RC_PROFILE
  if(r->op==R2_RC_PARENT){
    uint64_t parent;bool relay_only;uint32_t profile[5],transport[13],uc[8],acq[20],gm[7],connection[20];
    r2_service_test_parent_status(&parent,&relay_only);r2_service_profile_status(profile);
    r2_service_test_transport_status(transport);r2_uc_stats(uc);r2_service_test_acquisition_status(acq);r2_service_test_gm_admission_status(gm);r2_service_test_connection_status(connection);
    int n=snprintf(json,sizeof json,"{\"source\":\"%s\",\"boot\":\"%016llx\",\"result\":%d,\"rc_parent\":\"%016llx\",\"boot_relay_only\":%s,\"profile_rc\":%s,\"pending\":%s,\"transport\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"clock_domains\":[%u,%u,%u],\"unicast\":[%u,%u,%u,%u,%u,%u,%u,%u],\"acquisition\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"gm_admission\":[%u,%u,%u,%u,%u,%u,%u],\"connection\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]}",R2_SOURCE_SHA,(unsigned long long)identity.boot,result,(unsigned long long)parent,relay_only?"true":"false",profile[0]?"true":"false",profile[1]?"true":"false",transport[0],transport[1],transport[2],transport[3],transport[4],transport[5],transport[6],transport[7],transport[8],transport[9],transport[10],transport[11],transport[12],(unsigned)nrf_clock_lf_src_get(NRF_CLOCK),(unsigned)nrf_clock_lf_is_running(NRF_CLOCK),(unsigned)nrf_clock_hf_is_running(NRF_CLOCK,NRF_CLOCK_HFCLK_HIGH_ACCURACY),uc[0],uc[1],uc[2],uc[3],uc[4],uc[5],uc[6],uc[7],acq[0],acq[1],acq[2],acq[3],acq[4],acq[5],acq[6],acq[7],acq[8],acq[9],acq[10],acq[11],acq[12],acq[13],acq[14],acq[15],acq[16],acq[17],acq[18],acq[19],gm[0],gm[1],gm[2],gm[3],gm[4],gm[5],gm[6],connection[0],connection[1],connection[2],connection[3],connection[4],connection[5],connection[6],connection[7],connection[8],connection[9],connection[10],connection[11],connection[12],connection[13],connection[14],connection[15],connection[16],connection[17],connection[18],connection[19]);
    if(n<0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    struct r2_rx_loss_capsule capsule;
    if(r2_radio_capture_loss(&capsule)){
      int extra=snprintf(json+n-1,sizeof json-(size_t)n+1,",\"rx_loss_capsule\":{\"schema\":1,\"generation\":%u,\"sequence\":%u,\"count\":%u,\"lazy\":%u,\"queued\":%u,\"radio_extra_available\":false,\"entries\":[",capsule.generation,capsule.sequence,capsule.count,capsule.lazy,capsule.queued);
      if(extra<0||(size_t)extra>=sizeof json-(size_t)n+1)return;
      n+=extra-1;
      for(unsigned i=0;i<capsule.count;i++){
        unsigned at=(capsule.sequence-capsule.count+i)%8;
        extra=snprintf(json+n,sizeof json-(size_t)n,"%s[\"%016llx\",%u,%u]",i?",":"",(unsigned long long)capsule.event[at],capsule.outcome[at],capsule.owner[at]);
        if(extra<0||(size_t)extra>=sizeof json-(size_t)n)return;
        n+=extra;
      }
      extra=snprintf(json+n,sizeof json-(size_t)n,"]}}");if(extra<0||(size_t)extra>=sizeof json-(size_t)n)return;
        n+=extra;
    }
    n=r2_init_diag_append(json,sizeof json,n);if(n<=0)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
#endif
#ifdef R2_NATIVE_BLE
  if(r->op==R2_RADIO_AUDIT){
    struct r2_radio_audit_record q={0};struct r2_radio_audit_status st;
    bool found=r2_radio_audit_read((uint32_t)r->nonce,&q,&st);
    int n=snprintf(json,sizeof json,
      "{\"schema\":1,\"source\":\"%s\",\"commit\":\"%s\",\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"result\":%d,\"found\":%s,\"reference\":\"RAW_ADDRESS_UNCALIBRATED\",\"produced\":%u,\"overwritten\":%u,\"sequence\":%u,\"kind\":%u,\"peer_boot\":\"%016llx\",\"root_boot\":\"%016llx\",\"epoch\":%u,\"peer_epoch\":%u,\"root_epoch\":%u,\"train\":%u,\"publication\":%u,\"event\":%u,\"channel\":%u,\"pdu_crc32\":%u,\"address_raw\":%u,\"raw\":%u,\"tick\":%llu,\"ns\":%llu,\"wire_ticks\":%llu,\"generation\":%llu,\"available\":%llu,\"original_available\":%llu,\"followup\":%llu,\"activation_lo\":%llu,\"activation_hi\":%llu,\"disciplined\":%s}",
      R2_SOURCE_SHA,R2_CHILD_COMMIT,identity.silicon,identity.unit,(unsigned long long)identity.boot,result,found?"true":"false",st.produced,st.overwritten,q.sequence,q.kind,
      (unsigned long long)q.peer_boot,(unsigned long long)q.root_boot,q.epoch,q.peer_epoch,q.root_epoch,q.train,q.publication,q.event,q.channel,q.crc,q.address_raw,q.raw,
      (unsigned long long)q.tick,(unsigned long long)q.ns,(unsigned long long)q.wire_ticks,(unsigned long long)q.generation,(unsigned long long)q.available,
      (unsigned long long)q.original_available,(unsigned long long)q.followup,(unsigned long long)q.activation_lo,(unsigned long long)q.activation_hi,q.disciplined?"true":"false");
    if(n<0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_SHADOW){
    struct rc_shadow_record q={0};struct rc_shadow_status st;bool found=r2_shadow_read((uint32_t)r->nonce,&q,&st);
    int n=snprintf(json,sizeof json,
      "{\"schema\":1,\"source\":\"%s\",\"commit\":\"%s\",\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"result\":%d,\"found\":%s,\"enabled\":%s,\"edges\":%u,\"queue_drops\":%u,\"overwritten\":%u,\"disabled_edges\":%u,\"duplicates\":%u,\"now_tick\":%llu,\"now_epoch\":%llu,\"sequence\":%u,\"edge_tick\":%llu,\"edge_epoch\":%llu,\"arrival_us\":%lld,\"reason\":%u,\"map_valid\":%s,\"map_generation\":%u,\"activation_lo\":%llu,\"activation_tick\":%llu,\"discipline_tick\":%llu,\"relay_ns\":%llu,\"utc_ns\":%lld,\"phase_ns\":%lld,\"pll\":%s,\"parent\":\"%016llx\",\"parent_boot\":\"%016llx\",\"root\":\"%016llx\",\"root_boot\":\"%016llx\",\"root_epoch\":%u,\"parent_epoch\":%u,\"train\":%u,\"accepted\":%u,\"root_issued_ns\":%llu,\"reference_seen\":%s,\"association\":%u,\"reference_fix\":%s,\"reference_pps\":%s,\"reference_reason\":%u,\"rmc_utc_us\":%lld,\"rmc_at_us\":%lld,\"applied_phase_correction_ns\":%d,\"absolute_error_ns\":null}",
      R2_SOURCE_SHA,R2_CHILD_COMMIT,identity.silicon,identity.unit,(unsigned long long)identity.boot,result,found?"true":"false",st.enabled?"true":"false",st.edges,st.drops,st.overwritten,st.disabled,st.duplicates,(unsigned long long)st.now_tick,(unsigned long long)st.epoch,q.sequence,(unsigned long long)q.edge.tick,(unsigned long long)q.edge.epoch,(long long)q.arrival_us,q.reason,q.valid?"true":"false",q.map.map.generation,(unsigned long long)q.map.lo,(unsigned long long)q.map.active,(unsigned long long)q.map.map.effective,(unsigned long long)q.relay_ns,(long long)q.utc_ns,(long long)q.phase_ns,q.pll?"true":"false",(unsigned long long)q.map.parent,(unsigned long long)q.map.parent_boot,(unsigned long long)q.map.root,(unsigned long long)q.map.root_boot,q.map.root_epoch,q.map.parent_epoch,q.map.train,q.map.accepted,(unsigned long long)q.map.issued,q.reference_seen?"true":"false",q.association,q.reference_fix?"true":"false",q.reference_pps?"true":"false",q.reference_reason,(long long)q.rmc_utc_us,(long long)q.rmc_at_us,(r2_enrollment.boundary_field?rc_direct_pps_correction(r2_enrollment.boundary_profile):RC_DIRECT_PPS_CORRECTION_NS));
    if(n<0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
#ifdef R2_PC_PROFILE
    int extra=snprintf(json+n-1,sizeof json-(size_t)n+1,",\"image_profile\":\"PC\",\"pc_projection\":%s,\"experimental\":true}",q.map.experimental?"true":"false");
    if(extra<0||(size_t)extra>=sizeof json-(size_t)n+1)return;
#endif
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_GM){
#ifdef R2_GM7_NATIVE
    uint32_t gm7_profile[5];r2_service_profile_status(gm7_profile);
    int n=r2_gm7_diagnostic_format(json,sizeof json,(unsigned)r->nonce,R2_SOURCE_SHA,identity.boot,!gm7_profile[0]&&!gm7_profile[1]);
#elif defined(R2_GM5_NATIVE)
    struct r2_gm5_diagnostic d={0};r2_gm_diagnostic_snapshot(&d);
    uint32_t gm_profile[5];r2_service_profile_status(gm_profile);if(gm_profile[0]||gm_profile[1])d.state.enabled=false;
    int n=r2_gm_diagnostic_format(json,sizeof json,&d,(unsigned)r->nonce,R2_SOURCE_SHA,identity.boot);
#else
    int n=snprintf(json,sizeof json,"{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"available\":false}",R2_SOURCE_SHA,(unsigned long long)identity.boot);
#endif
    if(n<0||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_JOIN){
    struct r2_service_status s;r2_service_snapshot(&s);
    char names[5][49];const char *hex="0123456789abcdef";
    for(unsigned i=0;i<5;i++){const char *name=i?s.peer_names[i-1]:s.name;unsigned j=0;
      for(;j<24&&name[j];j++){uint8_t c=(uint8_t)name[j];names[i][2*j]=hex[c>>4];names[i][2*j+1]=hex[c&15];}names[i][2*j]=0;
    }
    int n=snprintf(json,sizeof json,"{\"source\":\"%s\",\"boot\":\"%016llx\",\"group_id\":\"%016llx\",\"key_epoch\":%u,\"name_utf8_hex\":\"%s\",\"authenticated_mask\":%u,\"control_capacity\":%u,\"peers\":[{\"id\":\"%016llx\",\"name_hex\":\"%s\"},{\"id\":\"%016llx\",\"name_hex\":\"%s\"},{\"id\":\"%016llx\",\"name_hex\":\"%s\"},{\"id\":\"%016llx\",\"name_hex\":\"%s\"}],\"gm_transport\":[",
      R2_SOURCE_SHA,(unsigned long long)identity.boot,(unsigned long long)s.group_id,s.key_epoch,names[0],s.authenticated_mask,R2_NET_PEERS,
      (unsigned long long)s.identities[0],names[1],(unsigned long long)s.identities[1],names[2],(unsigned long long)s.identities[2],names[3],(unsigned long long)s.identities[3],names[4]);
    uint32_t gm[R2_GM_TRANSPORT_WORDS]={0},pa[6]={0},reference_sequence=0;
#ifndef R2_PC_PROFILE
    if(!s.profile_rc){r2_ble_gm_diagnostics(gm);r2_radio_service_gm_diagnostics(pa);reference_sequence=r2_radio_service_gnss_sequence();}
#endif
    for(unsigned i=0;i<R2_GM_TRANSPORT_WORDS&&n>=0&&(size_t)n<sizeof json;i++){
      int added=snprintf(json+n,sizeof json-(size_t)n,"%s%u",i?",":"",gm[i]);
      if(added<0){n=-1;break;}n+=added;
    }
    if(n>=0&&(size_t)n<sizeof json){int added=snprintf(json+n,sizeof json-(size_t)n,
      "],\"gm_radio\":[%u,%u,%u,%u,%u,%u],\"gnss_diagnostic_sequence\":%u}",pa[0],pa[1],pa[2],pa[3],pa[4],pa[5],reference_sequence);if(added<0)n=-1;else n+=added;}
    if(n<0||(size_t)n>=sizeof json||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof frame,r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_ROUTE){
    struct r2_service_status s;r2_service_snapshot(&s);struct r2_relay_status *v=&s.relay;
    struct r2_radio_capture_stats raw;r2_radio_capture_stats(&raw);
    bool normal_closed=true;uint32_t idle_discards=0;
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
#ifndef R2_PC_PROFILE
    normal_closed=r2_radio_service_closed();idle_discards=r2_radio_service_idle_discards();
#endif
#endif
    int n=snprintf(json,sizeof(json),"{\"source\":\"%s\",\"commit\":\"%s\",\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"at_us\":%llu,\"session\":\"%016llx\",\"field_profile\":%s,\"activation_until_us\":%llu,\"pending\":%s,\"selected\":%s,\"active\":%s,\"persistent\":%s,\"deadline_us\":%llu,\"version\":2,\"root_slot\":%u,\"upstream_slot\":%u,\"role\":%u,\"root_identity\":\"%016llx\",\"upstream_identity\":\"%016llx\",\"root_boot\":\"%016llx\",\"source_epoch\":%u,\"source_created_ns\":%llu,\"source_expires_ns\":%llu,\"source_frame_ns\":%llu,\"source_projection_revision\":%llu,\"train\":%u,\"map_incarnation\":%u,\"lifecycle\":%llu,\"rx_lifecycle\":%llu,\"tx_lifecycle\":%llu,\"accepted_corrections\":%u,\"tx_corrections\":%u,\"correction_age_ms\":%u,\"upstream_update_age_ms\":%u,\"root_source_age_ms\":%u,\"map_valid\":%s,\"selected_utc_us\":%llu,\"quiet\":%s,\"phase_target_ns\":%lld,\"phase_ns\":%lld,\"phase_error_ns\":%lld,\"phase_admitted\":%s,\"retries\":%u,\"bound_ns\":%u,\"bound_known\":false,\"experimental_hold_us\":500000,\"experimental_held\":%s,\"max_reservation_us\":%u,\"max_prepare_us\":%u,\"max_receive_window_us\":%u,\"capture_completed\":%u,\"capture_invalid\":%u,\"capture_overflow\":%u,\"flushed\":%u,\"cost_refusals\":%u,\"rejected\":%u,\"duplicates\":%u,\"error\":%d,\"error_name\":\"%s\",\"fault_line\":%u,\"window_faults\":%u,\"window_recoveries\":%u,\"window_fault_site\":%u,\"window_fault_at_us\":%llu,\"window_fault_deadline_us\":%llu,\"window_recovering\":%s,\"direct_path\":[%llu,%llu,%llu,%llu],\"raw_capture\":[%u,%u,%u,%s],\"normal_receiver_closed\":%s,\"idle_capture_discards\":%u,\"absolute_utc_qualified\":false,\"output_qualified\":false}",
      R2_SOURCE_SHA,R2_CHILD_COMMIT,identity.silicon,identity.unit,(unsigned long long)identity.boot,(unsigned long long)s.at,(unsigned long long)s.relay_session,
      s.field_profile?"true":"false",(unsigned long long)s.activation_until,s.relay_pending?"true":"false",v->selected?"true":"false",v->active?"true":"false",s.normal?"true":"false",(unsigned long long)s.deadline,
      v->root,v->parent,v->role,(unsigned long long)v->root_identity,(unsigned long long)v->parent_identity,(unsigned long long)v->root_boot,v->source_epoch,
      (unsigned long long)v->source_created_ns,(unsigned long long)v->source_expires_ns,(unsigned long long)v->source_frame_ns,(unsigned long long)v->source_revision,
      v->train,v->map_generation,(unsigned long long)v->lifecycle,(unsigned long long)v->rx_lifecycle,(unsigned long long)v->tx_lifecycle,v->accepted,v->tx_corrections,v->correction_age_ms,v->update_age_ms,v->source_age_ms,
      v->map_valid?"true":"false",(unsigned long long)v->utc_us,v->quiet?"true":"false",(long long)v->target_ns,(long long)v->phase_ns,(long long)v->error_ns,
      v->phase_admitted?"true":"false",v->retries,v->bound_ns,(v->held_time&&v->role==1)?"true":"false",v->reservation_max_us,v->prepare_max_us,v->rx_window_max_us,
      v->capture_completed,v->capture_invalid,v->capture_overflow,v->flushed,v->cost_refusals,v->rejected,v->duplicates,v->error,error_name(v->error),v->fault_line,v->window_faults,v->window_recoveries,v->window_fault_site,(unsigned long long)v->window_fault_at,(unsigned long long)v->window_fault_deadline,v->window_recovering?"true":"false",(unsigned long long)s.direct_path[0],(unsigned long long)s.direct_path[1],(unsigned long long)s.direct_path[2],(unsigned long long)s.direct_path[3],raw.completed,raw.invalid,raw.overflow,raw.armed?"true":"false",normal_closed?"true":"false",idle_discards);
    if(n<0||(size_t)n>=sizeof(json)||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_PRECISION){
    struct r2_service_status s;r2_service_snapshot(&s);
    struct r2_pps_precision_view p;r2_time_precision_snapshot(&p);
    int n=snprintf(json,sizeof(json),"{\"source\":\"%s\",\"boot\":\"%016llx\",\"at_us\":%llu,\"root\":%u,\"metric\":\"%s\",\"rms_ns\":%u,\"rms_valid\":%s,\"samples\":%u,\"fit_valid\":%s,\"correction_age_valid\":%s,\"correction_age_ms\":%u,\"map_valid\":%s,\"locked\":%s,\"trim_ppb\":%d,\"drift_ppb\":%d,\"drift_valid\":%s,\"accepted\":%u,\"rejected\":%u,\"duplicates\":%u,\"overflows\":%u,\"pps_drift_ppb\":%d,\"pps_trim_ppb\":%d,\"pps_rate_valid\":%s,\"pps_prediction_rms_ns\":%u,\"captured\":%u,\"invalid_captures\":%u,\"train\":%u,\"publications\":%u,\"tx_reject\":[%u,%u,%u,%u],\"authorized\":%s,\"lease_expired\":%u,\"start_delay_ticks\":%llu,\"publish_ticks\":%llu,\"stale_tx\":%u,\"slot\":%u,\"slot_scans\":%u,\"slot_scan_verified\":%u,\"slot_scan_failures\":%u,\"slot_scan_owned_us\":%llu,\"slot_changes\":%u,\"slot_rollbacks\":%u,\"slot_busy\":%u,\"slot_requests\":%u,\"client_quiet_faults\":%u,\"upstream_recoveries\":%u,\"sync_creating\":%s,\"sync_active\":%s,\"phase_acquiring\":%s,\"discipline_gap_max_us\":%llu,\"holdover_expiries\":%u,\"holdover_recoveries\":%u,\"gm_capable\":%s,\"source_eligible\":%s,\"admission_ms\":%u,\"coordinator\":%u,\"standby\":%u,\"hot_ready\":%s,\"hot_refusal\":%u,\"hello_map_refusals\":%u,\"hello_width_us\":%u,\"quality\":[%u,%u,%u,%u,%u],\"absolute_utc_qualified\":false}",
      R2_SOURCE_SHA,(unsigned long long)identity.boot,(unsigned long long)s.at,s.root,s.profile_rc?"RELATIVE_CAPTURE_FIT_RMS":s.serving?"GM_RADIO_BEAT":"FOLLOWER_DELIVERED_MAP",
      s.precision.rms_ns,s.precision.rms_valid?"true":"false",s.precision.samples,s.precision.fit_valid?"true":"false",s.precision.correction_age_valid?"true":"false",s.precision.correction_age_ms,s.precision.map_valid?"true":"false",s.precision.locked?"true":"false",s.serving?p.trim_ppb:s.precision.trim_ppb,s.serving?p.raw_rate_ppb:s.precision.drift_ppb,(s.serving?p.rate_valid:s.precision.drift_valid)?"true":"false",s.precision.accepted,s.precision.rejected,s.precision.duplicates,s.precision.overflows,p.raw_rate_ppb,p.trim_ppb,p.rate_valid?"true":"false",p.prediction_rms_ns,s.precision.captured,s.precision.invalid,s.precision.train,s.precision.publications,s.precision.tx_reject[0],s.precision.tx_reject[1],s.precision.tx_reject[2],s.precision.tx_reject[3],s.precision.authorized?"true":"false",s.lease_expired,(unsigned long long)s.relay.start_delay_ticks,(unsigned long long)s.relay.publish_ticks,s.relay.stale_tx,s.relay.slot,s.relay.slot_scans,s.relay.slot_scan_verified,s.relay.slot_scan_failures,(unsigned long long)s.relay.slot_scan_owned_us,s.relay.slot_changes,s.relay.slot_rollbacks,s.relay.slot_busy,s.relay.slot_requests,s.relay.client_quiet_faults,s.relay.upstream_recoveries,s.relay.sync_creating?"true":"false",s.relay.sync_active?"true":"false",s.relay.phase_acquiring?"true":"false",(unsigned long long)(s.relay.discipline_gap_max_ticks/16),s.relay.holdover_expiries,s.relay.holdover_recoveries,s.gm_capable?"true":"false",s.source_eligible?"true":"false",s.admission_ms,s.coordinator,s.standby,s.hot_ready?"true":"false",s.hot_refusal,s.hello_map_refusals,s.hello_width_us,s.quality.priority1,s.quality.clock_class,s.quality.accuracy,s.quality.variance,s.quality.priority2);
    if(n<0||n>=(int)sizeof(json)||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_DIAG){
    uint8_t bytes[64];char encoded[129];int rc=r2_diag_read(r->trace_page/128,r->trace_page%128,bytes);
    for(unsigned i=0;i<64;i++)snprintf(encoded+2*i,3,"%02x",rc?0:bytes[i]);
    int n=snprintf(json,sizeof(json),"{\"source\":\"%s\",\"commit\":\"%s\",\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"result\":%d,\"diag_state\":%d,\"chunk\":%u,\"hex\":\"%s\"}",R2_SOURCE_SHA,R2_CHILD_COMMIT,identity.silicon,identity.unit,(unsigned long long)identity.boot,rc,r2_diag_state(),r->trace_page,encoded);
    if(n<0||n>=(int)sizeof(json)||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
  if(r->op==R2_NETWORK){
    struct r2_service_status s;r2_service_snapshot(&s);
    int n=snprintf(json,sizeof(json),"{\"version\":\"%s\",\"source\":\"%s\",\"commit\":\"%s\",\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"at_us\":%llu,\"deadline_us\":%llu,\"active\":%s,\"closed\":%s,\"error\":%d,\"self\":%u,\"coordinator\":%u,\"root\":%u,\"standby\":%u,\"serving\":%s,\"peer_delivery_confirmed\":%s,\"follower\":%s,\"rx\":%u,\"authenticated\":%u,\"rejected\":%u,\"drops\":%u,\"transport_errors\":%u,\"transport_fault\":[%u,%d,%u,%llu],\"gatt_bridge\":[%u,%u,%u,%u,%s],\"control\":{\"management_until_us\":%llu,\"pair_until_us\":%llu,\"hot_withdrawals\":%u,\"last_hot_withdrawal_us\":%llu,\"last_hot_withdrawal_reason\":%u,\"record_age_us\":%u,\"record_age_max_us\":%u,\"rx_latency_max_us\":%u,\"rx_queue_peak\":%u,\"backlog\":%u},\"worker_max_us\":%u,\"cycles\":%u,\"members\":%u,\"service_receipts\":%u,\"service_until_us\":%llu,\"root_boot\":\"%016llx\",\"source_epoch\":%u,\"follower_utc_us\":%llu,\"follower_clock_valid\":%s,\"time_source\":\"%s\",\"close_deadline_us\":%llu,\"closed_at_us\":%llu,\"configured\":%s,\"diag_state\":%d,\"stack_unused_main\":%u,\"stack_unused_sysworkq\":%u,\"stack_unused_isr\":%u,\"stack_unused_owner\":%u,\"stack_unused_precision\":%u,\"pt\":[%u,%u,%u],\"supervision\":[%u,%llu,%llu,%u,%u,%u,%llu,%llu,%llu,%u],\"worker_stages\":[%u,%llu,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u],\"v14\":[%u,%u,%u,%llu,%llu,%llu,%llu],\"precision\":\"UNKNOWN\"}",
      R2_VERSION,R2_SOURCE_SHA,R2_CHILD_COMMIT,identity.silicon,identity.unit,(unsigned long long)identity.boot,
      (unsigned long long)s.at,(unsigned long long)s.deadline,s.active?"true":"false",s.closed?"true":"false",s.error,s.self,s.coordinator,s.root,s.standby,
      s.serving?"true":"false",s.peer_delivery_confirmed?"true":"false",s.follower?"true":"false",s.rx,s.authenticated,s.rejected,s.drops,s.errors,s.transport_fault.operation,s.transport_fault.error,s.transport_fault.peer,(unsigned long long)s.transport_fault.at,s.transport_fault.bridge_calls,s.transport_fault.bridge_queue_max_us,s.transport_fault.bridge_call_max_us,s.transport_fault.bridge_timeouts,s.transport_fault.bridge_pending?"true":"false",(unsigned long long)s.management_until,(unsigned long long)s.pair_until,s.hot_withdrawals,(unsigned long long)s.hot_withdrawal_at,s.hot_withdrawal_reason,s.record_age_us,s.record_age_max_us,s.rx_latency_max_us,s.rx_queue_peak,s.backlog,s.worker_max_us,s.cycles,s.members,s.service_receipts,(unsigned long long)s.service_until,(unsigned long long)s.root_boot,s.source_epoch,(unsigned long long)s.follower_utc,s.follower_clock_valid?"true":"false",s.serving?"GPS":s.follower&&s.follower_clock_valid?"BLE":"NONE",(unsigned long long)s.close_deadline,(unsigned long long)s.closed_at,s.configured?"true":"false",r2_diag_state(),stack_unused(false),stack_unused(true),isr_stack_unused(),r2_service_stack_unused(),r2_radio_service_stack_unused(),s.precision.pt_cycles,s.precision.pt_wall_max_us,s.precision.pt_capture_age_max_us,s.supervision.state,(unsigned long long)s.supervision.submitted,(unsigned long long)s.supervision.started,s.supervision.queue_max_us,s.supervision.late_queues,s.supervision.fault_site,(unsigned long long)s.supervision.fault_at,(unsigned long long)s.supervision.fault_submitted,(unsigned long long)s.supervision.fault_started,s.supervision.fault_state,s.supervision.stage,(unsigned long long)s.supervision.stage_at,s.supervision.fault_stage,(unsigned long long)s.supervision.fault_stage_at,s.supervision.owner_max_us,s.supervision.stage_max_us[0],s.supervision.stage_max_us[1],s.supervision.stage_max_us[2],s.supervision.stage_max_us[3],s.supervision.stage_max_us[4],s.supervision.stage_max_us[5],s.supervision.stage_max_us[6],s.membership_mask,s.standby_authorized,s.takeover_pending,(unsigned long long)s.root_health_at,(unsigned long long)s.takeover_started,(unsigned long long)s.takeover_not_before,(unsigned long long)s.relay.recovery_until);
    if(n<0||n>=(int)sizeof(json)||reply_sequence==UINT32_MAX)return;
    n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);if(n>0)(void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);return;
  }
#endif
  struct r2_platform p; struct r2_io io;struct r2_ui_status u;r2_ui_snapshot(&u);uint32_t service_profile[5]={0};
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
#ifdef R2_NATIVE_BLE
  r2_service_profile_status(service_profile);
#endif
#endif
  r2_platform_snapshot(&p); r2_console_snapshot(&io);
  const char *rf="OFF";
#ifdef R2_NATIVE_BLE
  struct r2_service_status net_status;r2_service_snapshot(&net_status);
  if(net_status.active||(net_status.closing&&!net_status.closed))rf=net_status.normal?"BLE_NORMAL":"BLE_BENCH";
#endif
  int n=snprintf(json,sizeof(json),
#ifdef R2_DC_PROFILE
    "{\"image_profile\":\"DC\",\"profile_toggleable\":false,\"relay_hops\":0,\"ble_enabled\":false,\"dc_source_active\":%s,"
    "\"version\":\"%s\",\"source\":\"%s\",\"commit\":\"%s\",\"dirty\":%d,"
#elif defined(R2_PC_PROFILE)
    "{\"image_profile\":\"PC\",\"profile_toggleable\":false,\"awake_only\":true,\"relay_transmit\":false,"
    "\"version\":\"%s\",\"source\":\"%s\",\"commit\":\"%s\",\"dirty\":%d,"
#else
    "{\"version\":\"%s\",\"source\":\"%s\",\"commit\":\"%s\",\"dirty\":%d,"
#endif
#ifdef R2_CONTROLLER_DIAGNOSTICS
    "\"controller_diagnostics\":true,"
#endif
    "\"silicon\":\"%s\",\"unit\":\"%s\",\"boot\":\"%016llx\",\"battery\":%d,"
    "\"uptime_ms\":%llu,\"heartbeat\":%u,\"result\":%d,\"identity_rc\":%d,\"pins_rc\":%d,"
    "\"platform_ready\":%s,\"boot_timeout\":%s,\"clock\":[%d,%d,%d,%d,%llu,%llu],"
    "\"bt\":[%d,%d,%s,%llu,%llu],\"usb_rc\":%d,\"dtr\":%s,"
    "\"maintenance\":[%u,%d,%llu],\"close\":[%d,%s,%llu],"
    "\"ui\":[%u,%u,%u,%u,%u,%d,%u,%s,%s,%s,%u],\"ui_input\":[%llu,%u,%u,%s,%s,%s,%s],\"display_time\":[%s,%s,%lld],\"chip_temperature_qc\":%d,\"chip_temperature_valid\":%s,"
    "\"ui_saver\":[%s,%u,%u],\"ui_saver_drawn\":[%s,%u],\"ui_lamps\":[%u,%u,%u,%u,%u,%u,%u,%u],\"profile\":[%u,%u,%u,%u,%u],"
    "\"ui_holds_since_s\":[%u,%u],\"ui_active_since_s\":%u,"
    "\"shadow_ui\":{\"state\":%u,\"active\":%s,\"elapsed_ms\":%llu,\"session\":%u,\"samples\":%u,\"skipped\":%u,\"missing\":%u,\"valid\":%s,\"mean_tenth_ns\":%lld,\"rms_ns\":%u,\"p95_ns\":%u,\"p99_ns\":%u,\"max_ns\":%u,\"approximate\":%s,\"startup\":[%u,%u,%u,%u,%u]},"
    "\"io\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"rejected\":%u,\"trace_lost\":%u,\"trace_count\":%u,\"trace_page\":%u,"
    "\"rf\":\"%s\",\"time_service\":\"GNSS_CLOCK_V1\",\"precision\":\"UNKNOWN\",\"trace\":[",
#ifdef R2_DC_PROFILE
    r2_dc_active()?"true":"false",
#endif
    R2_VERSION,R2_SOURCE_SHA,R2_CHILD_COMMIT,R2_SOURCE_DIRTY,
    identity.silicon,identity.unit,(unsigned long long)identity.boot,identity.battery,
    (unsigned long long)k_uptime_get(),(unsigned)heartbeat,result,identity.result,r2_board_safe(),
    platform_ready?"true":"false",boot_timeout?"true":"false",
    p.clock_request,p.clock_result,p.clock_release,p.clock_status,(unsigned long long)p.clock_start,(unsigned long long)p.clock_end,
    p.bt_submit,p.bt_result,p.bt_done?"true":"false",(unsigned long long)p.bt_start,(unsigned long long)p.bt_end,
    io.usb_rc,io.dtr?"true":"false",(unsigned)maintenance.phase,maintenance.error,(unsigned long long)maintenance.deadline,
    p.close_result,p.close_done?"true":"false",(unsigned long long)p.close_end,
    u.frames,u.missed,u.buttons,u.max_frame_ms,u.last_frame_ms,u.error,u.page,u.ready?"true":"false",u.stopped?"true":"false",u.analog?"true":"false",u.drawn_page,(unsigned long long)u.drawn_model_ms,u.drawn_role,u.poll_max_ms,u.drawn_scope?"true":"false",u.drawn_ble?"true":"false",u.drawn_pps?"true":"false",u.drawn_map?"true":"false",u.drawn_time?"true":"false",u.drawn_coarse?"true":"false",(long long)u.drawn_utc_us,u.temperature_qc,(u.temperature_valid&&u.ready&&k_uptime_get()-u.last_frame_ms<2500)?"true":"false",
    u.screensaver?"true":"false",u.screensaver_position,u.brightness_percent,u.drawn_screensaver?"true":"false",u.drawn_screensaver_position,u.drawn_lamps[0],u.drawn_lamps[1],u.drawn_lamps[2],u.drawn_lamps[3],u.drawn_lamps[4],u.drawn_lamps[5],u.drawn_lamps[6],u.drawn_lamps[7],service_profile[0],service_profile[1],service_profile[2],service_profile[3],service_profile[4],
    u.holds.pps_since,u.holds.hot_since,u.holds.active_since,
    u.shadow.state,u.shadow.active?"true":"false",(unsigned long long)u.shadow.elapsed_ms,u.shadow.session,u.shadow.samples,u.shadow.skipped,u.shadow.missing,u.shadow.valid?"true":"false",(long long)u.shadow.mean_tenth_ns,u.shadow.rms_ns,u.shadow.p95_ns,u.shadow.p99_ns,u.shadow.max_ns,u.shadow.approximate?"true":"false",u.shadow.warmup,u.shadow.warmup_skipped,u.shadow.warmup_missing,u.shadow.warmup_resets,u.shadow.settling_ms,
    io.rx_bytes,io.tx_bytes,io.rx_overflow,io.command_drops,io.tx_drops,io.tx_expired,io.fifo_errors,io.usb_events,io.dtr_changes,
    (unsigned)rejected,trace_lost,trace_count,r->trace_page,rf);
  if(n<0 || (size_t)n>=sizeof(json)) return;
  size_t used=(size_t)n;
  /* Eight records per page, including at maximum integer widths. */
  if(r->op==R2_TRACE) {
    unsigned first=r->trace_page*8;
    for(unsigned i=first;i<trace_count && i<first+8;i++) {
      n=snprintf(json+used,sizeof(json)-used,"%s[%llu,%u,%d]",i==first?"":",",
        (unsigned long long)trace[i].ms,trace[i].kind,trace[i].result);
      if(n<0 || (size_t)n>=sizeof(json)-used) { event(9,-EMSGSIZE); return; }
      used+=(size_t)n;
    }
  }
  if(used+3>sizeof(json)) return;
  memcpy(json+used,"]}",3);
  if(reply_sequence==UINT32_MAX) return;
  n=r2_frame(frame,sizeof(frame),r->id,++reply_sequence,json);
  if(n>0) (void)r2_console_reply(frame,(size_t)n,arrival+R2_REPLY_MS);
}
#ifdef R2_NATIVE_BLE
static void hot_report(const struct r2_service_status *s,const struct r2_clock *clock,uint64_t now)
{
  static uint64_t next,changed_after;
  static uint32_t previous;
  if(s->profile_rc||s->profile_pending||s->closing||!s->active||maintenance.phase!=R2_IDLE)return;
  uint32_t signature=s->hot_refusal|(s->source_eligible<<16)|(s->hot_ready<<17)|
    (clock->view.fix_active<<18)|(s->serving<<19);
  if(now<next&&(signature==previous||now<changed_after))return;
  struct r2_io io;r2_console_snapshot(&io);
  if(!io.dtr){next=now+1000;return;}
  struct r2_pps_precision_view pps;r2_time_precision_snapshot(&pps);
  bool pulse=pps.epoch&&pps.age_us>=0&&pps.age_us<R2_PPS_FRESH_US;
  char note[384];
  int n=snprintf(note,sizeof note,"#R2_HOT unit=%s boot=%016llx scope=%016llx role=%s gps_fix=%u pps_live=%u source=%u dwell_ms=%u members=%u root=%u coordinator=%u hot=%u blockers=0x%x hello_width_us=%u hello_refusals=%u note_drops=%u\n",
    identity.unit,(unsigned long long)identity.boot,(unsigned long long)s->profile_incarnation,
    s->serving?"GM":s->hot_ready?"HOT":"FOLLOW",clock->view.fix_active,pulse,s->source_eligible,
    s->admission_ms,s->membership_mask,s->root,s->coordinator,s->hot_ready,s->hot_refusal,
    s->hello_width_us,s->hello_map_refusals,io.note_drops);
  if(n>0&&(size_t)n<sizeof note&&!r2_console_note(note,(size_t)n,now+150)){
    next=now+60000;changed_after=now+5000;previous=signature;
  }else next=now+1000;
}
#endif
#ifdef R2_GM7_NATIVE
/* Keep temporary UI scratch out of main's unrelated command/reply frame. */
static uint64_t __attribute__((noinline)) gm7_ui_publish(uint64_t now,bool platform_ready,bool bt_ready){
      struct r2_ui_model m={.sampled_ms=now,.platform_ready=platform_ready,.bt_ready=bt_ready,.reset_kind=r2_watchdog_reset_kind()};
      memcpy(m.unit,identity.unit,sizeof(m.unit));r2_time_snapshot(&m.clock,&m.queue_drops);
#ifdef R2_NATIVE_BLE
      r2_service_ui(&m);
      if(m.role==R2_UI_GM){
        int64_t utc;bool valid=m.gm_source_fresh&&r2_time_source_utc_now(m.clock.source_epoch,&utc);
        m.clock.valid=m.coarse_valid=valid;
        if(valid){m.clock.utc_us=utc;m.coarse_utc_us=(uint64_t)utc;m.coarse_at_us=(uint64_t)r2_time_now_us();}
      }
#endif
#ifdef R2_DC_PROFILE
      r2_dc_ui(&m);
#endif
#ifdef R2_LORA
      r2_lora_view(&m.lora);
#endif
      m.sampled_ms=k_uptime_get();r2_ui_publish(&m);return m.sampled_ms+20;
}
#endif
int main(void)
{
  k_thread_priority_set(k_current_get(),7);
  r2_time_init();
  r2_identify(&identity); r2_health_init(identity.boot); event(1,identity.result);
#ifdef R2_PC_PROFILE
  r2_pc_init();r2_shadow_enable(true);
#endif
#ifdef R2_USB_SOF_PROBE
  r2_usb_sof_init(&identity);
#endif
#ifdef R2_USB_SOURCE_STREAM
  r2_usb_source_init(&identity);
#endif
#ifdef R2_DC_PROFILE
  r2_dc_init(&identity);
  r2_shadow_serving(true); /* No mesh witness collection in DC. */
#endif
  int watchdog_rc=r2_watchdog_boot(&identity);
  if(watchdog_rc)identity.result=watchdog_rc;
#ifdef R2_NATIVE_BLE
  int enrollment_rc=r2_service_configure(&identity,&r2_enrollment);
  event(10,enrollment_rc);
#ifdef R2_LORA
  uint64_t lora_factory=0,lora_transmitter=(uint64_t)R2_LORA_TRANSMITTER_FACTORY;
  (void)r2_factory_parse(identity.silicon,&lora_factory);
#endif
  event(11,r2_diag_init(&identity));
#ifdef R2_LORA
  /* Diagnostic scan scratch becomes the LoRa fault observer only after the
   * startup scan completes; the two owners must never overlap. */
  r2_lora_init(enrollment_rc?0:lora_factory,identity.boot,lora_transmitter,&r2_enrollment.group);
#endif
#endif
  r2_platform_start();
  struct r2_platform p; r2_platform_snapshot(&p); event(2,p.clock_result);
  r2_console_start();
  uint64_t boot_deadline=k_uptime_get()+R2_BOOT_MS;
  bool recorded_bt=false, recorded_close=false,source_started=false,close_started=false;
#ifdef R2_NATIVE_BLE
  bool diagnostic_close_started=false,normal_started=false,poweroff_requested=false;int diagnostic_close_error=0;
#ifdef R2_GM7_NATIVE
  bool normal_start_failed=false;
#endif
#endif
  uint64_t ui_due=0;
  for(;;) {
    uint64_t now=k_uptime_get(); if(heartbeat<UINT32_MAX) ++heartbeat;
    r2_health_beat();r2_watchdog_main_beat();
    r2_time_service();
#ifdef R2_DC_PROFILE
    r2_dc_poll(platform_ready&&maintenance.phase==R2_IDLE&&!r2_ui_poweroff_requested());
    if(r2_ui_poweroff_requested()){
      platform_ready=false;r2_ui_stop();
      if(!r2_ui_close_result()){
        r2_time_stop();
        if(!r2_time_close_result()){
          if(!p.close_done)(void)r2_platform_close();
          else if(!p.close_result)(void)r2_board_poweroff();
        }
      }
    }
#endif
#ifdef R2_NATIVE_BLE
    struct r2_clock local_clock;r2_time_copy(&local_clock);r2_service_clock(&local_clock);
    struct r2_service_status service;r2_service_snapshot(&service);
    r2_timing_sample(&service,now);
#ifdef R2_CONTROLLER_DIAGNOSTICS
    /* Ordinary images retain the journal and explicit queries without the
     * unsolicited formatter/USB stream. Watchdog observation stays independent. */
    r2_timing_console(identity.boot,now);
    r2_controller_diag_console(identity.boot,now);
#endif
#ifdef R2_RC_PROFILE
    r2_watchdog_observe(&service,
#ifdef R2_PC_PROFILE
      false /* Intentional HOLD is not a failed parent; core WDT stays active. */
#else
      maintenance.phase==R2_IDLE&&!poweroff_requested&&!r2_ui_poweroff_requested()
#endif
    );
#ifndef R2_PC_PROFILE
    /* A latched GNSS driver failure cannot recover in place. Ordinary
     * no-fix, VOID, PPS absence and source expiry never set this latch.
     * Keep RC, PC, maintenance and intentional poweroff outside this policy. */
    if(normal_started&&service.normal&&!service.profile_rc&&!service.profile_pending&&
       maintenance.phase==R2_IDLE&&!poweroff_requested&&!r2_ui_poweroff_requested()){
      int gnss_error=r2_gnss_input_error();
      if(gnss_error)r2_watchdog_restart_result(gnss_error);
    }
#endif
    unsigned watchdog_retry=r2_watchdog_take_restart();
    if(watchdog_retry){
      r2_diag_watchdog(watchdog_retry,-ETIMEDOUT);
      int restart_rc=maintenance.phase==R2_IDLE&&!poweroff_requested&&!r2_ui_poweroff_requested()?r2_service_restart(k_uptime_get()):-ECANCELED;
      r2_watchdog_restart_result(restart_rc);
    }
#endif
#ifdef R2_LORA
    r2_lora_control(service.profile_rc,platform_ready&&normal_started&&maintenance.phase==R2_IDLE&&!poweroff_requested&&!r2_ui_poweroff_requested()&&!service.profile_pending,
      service.follower_clock_valid?RC8_SELECT_BLE:service.serving&&service.source_eligible?RC8_SELECT_LOCAL_GM:RC8_SELECT_NONE);
    if(maintenance.phase==R2_IDLE&&r2_ui_take_lora_toggle())r2_lora_toggle();
#endif
    r2_shadow_serving(service.serving);
    hot_report(&service,&local_clock,now);
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
    if(maintenance.phase==R2_IDLE&&r2_ui_take_profile_toggle()){
#ifdef R2_LORA
     r2_lora_stop();
#endif
     int profile_rc=r2_service_profile(2,now);if(!profile_rc)r2_ui_profile_flash(!service.profile_rc);
    }
#endif
#if !defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
    /* A boundary profile's GPS is an independent physical witness. Even a
     * coarse acquisition hint from the followed clock would couple it back. */
    uint64_t aid_utc=0,aid_sample=0;
    bool aid_valid=maintenance.phase==R2_IDLE&&!poweroff_requested&&!r2_ui_poweroff_requested()&&!service.profile_rc&&!service.profile_pending&&!r2_ui_shadow_requested()&&
      r2_peer_aid(&service,(uint64_t)r2_time_now_us(),&aid_utc,&aid_sample);
    r2_gnss_aid(aid_utc,aid_sample,aid_valid,local_clock.view.fix_active);
#endif
    if((service.closing&&!service.profile_pending)||poweroff_requested||r2_ui_poweroff_requested()||maintenance.phase!=R2_IDLE)r2_diag_quiesce();
#ifdef R2_LORA
    r2_lora_watchdog_poll();
#endif
    r2_diag_sample(&service,&local_clock.view);
#ifdef R2_RC_PROFILE
    uint32_t uc_trace[3];if(r2_uc_trace(uc_trace))r2_diag_uc_trace(uc_trace);
#endif
    r2_diag_poll();
    if(r2_ui_poweroff_requested()&&!poweroff_requested){poweroff_requested=true;r2_service_stop();r2_ui_stop();}
    if(poweroff_requested&&service.closed&&r2_diag_shutdown_ready()
#ifdef R2_LORA
       &&!r2_lora_close_result()
#endif
       &&!r2_ui_close_result()&&!r2_time_close_result()){
      struct r2_platform off;r2_platform_snapshot(&off);
      if(off.close_done&&!off.close_result)(void)r2_board_poweroff();
    }
    /* A contained normal-service fault fences radio/time authority, but is
     * not an operator request to turn off the clock UI and console. Preserve
     * the failed service and journal; do not blindly restart controller state. */
    bool terminal_close=!service.normal||poweroff_requested||maintenance.phase!=R2_IDLE;
    if(service.normal&&service.closing&&service.closed&&!terminal_close&&!service.profile_pending)
      r2_diag_finish(service.error);
    if(terminal_close&&(service.closing||poweroff_requested)&&service.closed&&!diagnostic_close_started&&!r2_diag_busy()
#ifdef R2_LORA
       &&!r2_lora_close_result()
#endif
       ){
      r2_ui_stop();
      if(!r2_ui_close_result()){
        r2_time_stop();
        if(!r2_time_close_result()){diagnostic_close_started=true;diagnostic_close_error=r2_platform_close();}
      }
    }
    if(terminal_close&&service.closing&&service.close_deadline&&(uint64_t)k_uptime_get()*1000>=service.close_deadline){
      struct r2_platform closing;r2_platform_snapshot(&closing);
      if(!closing.close_done||closing.close_result||closing.close_end*1000>=service.close_deadline||r2_ui_close_result()||r2_time_close_result())diagnostic_close_error=-ETIMEDOUT;
    }
    if(diagnostic_close_started)r2_diag_finish(diagnostic_close_error?diagnostic_close_error:service.error);
#endif
    r2_platform_snapshot(&p);
    if(p.bt_done && !recorded_bt) { event(3,p.bt_result); recorded_bt=true; }
    if(!startup_decided) {
      if(now>=boot_deadline) { boot_timeout=true; startup_decided=true; event(4,-ETIMEDOUT); }
      else if(p.bt_done && p.bt_submit!=-EAGAIN) {
        startup_decided=true;
        platform_ready=!identity.result && !r2_board_safe() && p.clock_done && !p.clock_result &&
          p.clock_release>=0 &&
#ifdef R2_DC_PROFILE
          !R2_SOURCE_DIRTY;
#else
          !p.bt_result && !p.bt_submit && p.bt_end<boot_deadline && !R2_SOURCE_DIRTY;
#endif
        event(4,platform_ready?0:-EIO);
      }
    }
    if(platform_ready&&!source_started){source_started=true;r2_time_start();r2_ui_start();}
#ifdef R2_NATIVE_BLE
#ifdef R2_GM7_NATIVE
    if(platform_ready&&source_started&&!normal_started&&!normal_start_failed&&!r2_switch_rc_active()){
      /* time_start requests capture asynchronously. Do not arm diagnostics or
       * consume the one normal start before the real HF domain is published.
       * Keep the original boot deadline; failed/cancelled boot cannot revive. */
      uint64_t start_now=k_uptime_get();
      int rc=maintenance.phase!=R2_IDLE||poweroff_requested||r2_ui_poweroff_requested()?-ECANCELED:
        start_now>=boot_deadline?-ETIMEDOUT:r2_gm7_start_ready();
      if(rc!=-EAGAIN){
        bool armed=false;
        start_now=k_uptime_get();
        if(!rc&&start_now>=boot_deadline)rc=-ETIMEDOUT;
        if(!rc){
          rc=r2_diag_arm(start_now,0);event(13,rc);armed=!rc;
          if(!rc){start_now=k_uptime_get();rc=start_now>=boot_deadline?-ETIMEDOUT:r2_service_run(start_now);}
        }
        if(rc){normal_start_failed=true;r2_gm7_start_failed(rc);if(armed)r2_diag_cancel_arm(rc);}
        else normal_started=true;
        event(12,rc);
      }
    }
    else if(r2_switch_rc_active())
#endif
    if(platform_ready&&source_started&&!normal_started&&maintenance.phase==R2_IDLE){
      normal_started=true;int diagnostic_rc=r2_diag_arm(now,0);event(13,diagnostic_rc);
      int rc=r2_service_run(now);if(rc)r2_diag_cancel_arm(rc);
      event(12,rc);
    }
#endif
    if(now>=ui_due){
#ifdef R2_GM7_NATIVE
      ui_due=gm7_ui_publish(now,platform_ready,p.bt_done&&!p.bt_result&&!p.bt_submit);
#else
      struct r2_ui_model m={.sampled_ms=now,.platform_ready=platform_ready,.bt_ready=p.bt_done&&!p.bt_result&&!p.bt_submit,.reset_kind=r2_watchdog_reset_kind()};
      memcpy(m.unit,identity.unit,sizeof(m.unit));r2_time_snapshot(&m.clock,&m.queue_drops);
#ifdef R2_NATIVE_BLE
      r2_service_ui(&m);
      if(m.role==R2_UI_GM){
        int64_t utc;bool valid=m.gm_source_fresh&&r2_time_source_utc_now(m.clock.source_epoch,&utc);
        m.clock.valid=m.coarse_valid=valid;
        if(valid){m.clock.utc_us=utc;m.coarse_utc_us=(uint64_t)utc;m.coarse_at_us=(uint64_t)r2_time_now_us();}
      }
#endif
#ifdef R2_DC_PROFILE
      r2_dc_ui(&m);
#endif
#ifdef R2_LORA
      r2_lora_view(&m.lora);
#endif
      m.sampled_ms=k_uptime_get();r2_ui_publish(&m);ui_due=m.sampled_ms+20;
#endif
    }
    if(maintenance.phase==R2_CLOSING&&!close_started){
      int rc=r2_ui_close_result();
#ifdef R2_NATIVE_BLE
      if(!rc)rc=r2_service_close_result();
#ifdef R2_LORA
      if(!rc)rc=r2_lora_close_result();
#endif
#endif
      if(rc!=-EAGAIN&&!diagnostic_busy()){close_started=true;if(!rc){r2_time_stop();rc=p.close_done?p.close_result:r2_platform_close();}
        if(rc){maintenance.phase=R2_FAULT;maintenance.error=rc;}}
    }
    r2_expire(&maintenance,now);
    /* A failed/expired update has withdrawn normal service. A live main loop
     * is not recovery: preserve the fault and let the independent WDT restore
     * the selected profile. Explicit poweroff remains a separate path. */
    if(maintenance.phase==R2_FAULT)r2_watchdog_restart_result(maintenance.error?maintenance.error:-EIO);
    if(maintenance.phase==R2_CLOSING && p.close_done && !recorded_close && r2_time_close_result()!=-EAGAIN) {
      int rc=r2_closed(&maintenance,now,p.close_result?p.close_result:r2_time_close_result()); event(7,rc); recorded_close=true;
    }
    struct r2_command command;
    if(r2_console_get(&command)) {
      /* RX can preempt this loop after its earlier time sample. Admission
       * must compare against time sampled after acquiring this command. */
      now=k_uptime_get();
      struct r2_request r;
      int rc=r2_parse(command.text,&r);
      if(rc || !r2_bound(&r,identity.silicon,R2_SOURCE_SHA,identity.boot) ||
         r.id<=last_request || now<command.arrival || now-command.arrival>=R2_REPLY_MS) {
        if(rejected<UINT32_MAX) ++rejected;
        event(5,rc?rc:-EACCES);
      } else {
        last_request=r.id; event(5,(int)r.op);
#ifdef R2_NATIVE_BLE
        r2_diag_command(r.id,r.op,0,command.arrival);
#endif
        if(r.op==R2_PREPARE) {
          rc=diagnostic_busy()?-EBUSY:r2_prepare(&maintenance,r.nonce,command.arrival,now,platform_ready);
          if(!rc) {
            platform_ready=false;r2_ui_stop();
#ifdef R2_DC_PROFILE
            r2_dc_poll(false);
#endif
#ifdef R2_NATIVE_BLE
            r2_diag_quiesce();
            r2_service_stop();
#ifdef R2_LORA
            r2_lora_stop();
#endif
#endif
            if(rc) { maintenance.phase=R2_FAULT; maintenance.error=rc; }
          }
          event(6,rc);
#ifdef R2_PC_PROFILE
        } else if(r.op==R2_PC_RUN){
          rc=maintenance.phase==R2_IDLE&&platform_ready?r2_pc_begin(r.trace_page,r.nonce):-EACCES;
        } else if(r.op==R2_PC_CYCLE){
          rc=maintenance.phase==R2_IDLE&&platform_ready?r2_pc_begin_cadence(r.seconds,r.nonce):-EACCES;
        } else if(r.op==R2_PC_ALIGN){
          rc=maintenance.phase==R2_IDLE&&platform_ready?r2_pc_begin_aligned(r.seconds,r.nonce):-EACCES;
#endif
#ifdef R2_NATIVE_BLE
#ifdef R2_LORA
        } else if(r.op==R2_LORA_CONTROL){
          if(r.seconds){rc=maintenance.phase==R2_IDLE&&platform_ready?0:-EACCES;if(!rc)r2_lora_request(r.nonce!=0);}
#endif
        } else if(r.op==R2_WITNESS) {
          if(maintenance.phase!=R2_IDLE)rc=-EACCES;else r2_shadow_enable(r.nonce!=0);
        } else if(r.op==R2_PROFILE) {
#if defined(R2_BOUNDARY_FIELD) && !defined(R2_SWITCHABLE_FIELD)
          rc=-ENOTSUP;
#else
          uint32_t profile[5];r2_service_profile_status(profile);
#ifdef R2_LORA
          if(maintenance.phase==R2_IDLE&&platform_ready)r2_lora_stop();
#endif
          rc=maintenance.phase!=R2_IDLE||!platform_ready?-EACCES:r2_service_profile((unsigned)r.nonce,command.arrival);
          if(!rc)r2_ui_profile_flash(r.nonce==2?!profile[0]:r.nonce==1);
#endif
#ifdef R2_RC_PROFILE
        } else if(r.op==R2_RC_PARENT) {
          rc=maintenance.phase!=R2_IDLE||!platform_ready?-EACCES:
             r.seconds?0:r2_service_test_parent(r.nonce,command.arrival);
#endif
        } else if(r.op==R2_MARK) {
          rc=maintenance.phase==R2_IDLE?r2_diag_mark():-EACCES;
        } else if(r.op==R2_BLE) {
          rc=platform_ready&&maintenance.phase==R2_IDLE?0:-EACCES;
          if(!rc&&!r.nonce)r2_diag_quiesce();
          if(!rc&&r.nonce)rc=r2_service_preflight((uint32_t)r.nonce,command.arrival);
          if(!rc&&r.nonce)rc=r2_diag_arm(command.arrival,(uint32_t)r.nonce);
          if(!rc){rc=r2_service_start((uint32_t)r.nonce,command.arrival);if(rc)r2_diag_cancel_arm(rc);}
          r2_diag_command(r.id,r.op,rc,command.arrival);
        } else if(r.op==R2_NAME){
          rc=r2_service_name(r.name);
        } else if(r.op==R2_RELAY){
          rc=platform_ready&&maintenance.phase==R2_IDLE?r2_service_relay(r.nonce,r.seconds,command.arrival):-EACCES;
          r2_diag_command(r.id,r.op,rc,command.arrival);
#endif
#ifdef R2_DC_PROFILE
        } else if(r.op==R2_PROFILE||r.op==R2_BLE||r.op==R2_RELAY||r.op==R2_WITNESS||r.op==R2_NAME) {
          rc=-ENOTSUP;
#endif
        } else if(r.op==R2_UI) {
          if(r.trace_page<R2_UI_PAGES)rc=r2_ui_page(r.trace_page);
        } else if(r.op==R2_COMMIT) {
          rc=r2_commit(&maintenance,r.nonce,command.arrival,now);
          if(!rc) {
            /* Commit only after the independent owner reported public disable
             * completion, no clock reference, and within original READY expiry. */
#ifdef R2_NATIVE_BLE
            if(!r2_diag_maintenance_safe())rc=-EBUSY;
#endif
#ifdef R2_LORA
            if(!rc)rc=r2_lora_close_result();
#endif
            if(rc||!p.close_done || p.close_result || r2_ui_close_result() || r2_time_close_result() || p.clock_release<0 ||
               (uint64_t)k_uptime_get()>=maintenance.deadline) rc=-EACCES;
            else {
              rc=r2_watchdog_bootloader(maintenance.deadline);
              if(!rc)rc=r2_bootloader_request();
            }
            maintenance.phase=R2_FAULT; maintenance.error=rc;
          }
          event(8,rc);
        }
        reply(&r,command.arrival,rc);
      }
    }
    k_sleep(K_MSEC(1));
  }
}
