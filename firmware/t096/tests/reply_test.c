/* Exercise the actual formatter at maximum integer widths and all trace pages. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "app.h"
#include "time_service.h"
#include "ui.h"
void r2_time_precision_snapshot(struct r2_pps_precision_view *out)
{*out=(struct r2_pps_precision_view){.epoch=UINT64_MAX,.age_us=INT64_MAX,.samples=UINT32_MAX,.drift_ppb=INT32_MIN,.trim_ppb=INT32_MAX,.fit_rms_ns=UINT32_MAX,.prediction_rms_ns=UINT32_MAX};}
static unsigned ui_stops,time_stops,platform_closes;
int r2_console_note(const char *bytes,size_t size,uint64_t expiry){return 0;}
int r2_capture_error(void){return INT_MIN;}
void r2_ui_start(void){}
void r2_ui_stop(void){++ui_stops;}
bool r2_ui_take_profile_toggle(void){return false;}
void r2_ui_profile_flash(bool rc){}
bool r2_ui_poweroff_requested(void){return false;}
bool r2_ui_shadow_requested(void){return false;}
bool r2_time_source_utc_now(uint32_t epoch,int64_t *utc){(void)epoch;(void)utc;return false;}
int64_t r2_time_now_us(void){return 1000000;}
void r2_gnss_aid(uint64_t utc,uint64_t at,bool valid,bool fix){}
void r2_gnss_aid_status(struct r2_gnss_aid_status *out){memset(out,0xff,sizeof *out);out->busy=true;}
void r2_time_source_pps_snapshot(struct r2_source_pps_view *out){memset(out,0xff,sizeof *out);out->valid=true;}
int r2_board_poweroff(void){return -EAGAIN;}
int r2_ui_close_result(void){return 0;}
int r2_ui_page(unsigned p){return 0;}
void r2_ui_publish(const struct r2_ui_model *m){}
void r2_ui_snapshot(struct r2_ui_status *s){memset(s,0xff,sizeof(*s));s->profile_target_rc=s->screensaver=s->drawn_screensaver=true;s->shadow.active=s->shadow.valid=s->shadow.approximate=true;s->drawn_time=s->drawn_coarse=s->ready=s->stopped=s->analog=s->temperature_valid=s->drawn_scope=s->drawn_ble=s->drawn_pps=s->drawn_map=true;}
#ifdef R2_NATIVE_BLE
static bool gm_diagnostic_reply;
#include "ble_service.h"
#include "diagnostic.h"
#include "radio_audit.h"
#include "radio_capture.h"
void r2_radio_capture_stats(struct r2_radio_capture_stats *s){memset(s,0xff,sizeof *s);s->armed=true;}
bool r2_radio_service_closed(void){return false;}
uint32_t r2_radio_service_idle_discards(void){return UINT32_MAX;}
#include "rc_shadow.h"
bool r2_radio_audit_read(uint32_t after,struct r2_radio_audit_record *q,struct r2_radio_audit_status *s){memset(q,0xff,sizeof *q);q->disciplined=true;memset(s,0xff,sizeof *s);return true;}
void r2_service_snapshot(struct r2_service_status *s){memset(s,0xff,sizeof(*s));s->profile_rc=s->profile_pending=s->profile_target_rc=true;s->profile_rc=!gm_diagnostic_reply;s->transport_fault.bridge_pending=s->standby_authorized=s->takeover_pending=s->field_profile=s->gm_capable=s->source_eligible=s->hot_ready=s->normal=s->configured=s->active=s->closing=s->closed=s->serving=s->follower=s->follower_clock_valid=s->peer_delivery_confirmed=s->relay_pending=true;s->precision.fit_valid=s->precision.correction_age_valid=s->precision.rms_valid=s->precision.map_valid=s->precision.locked=s->precision.authorized=true;s->relay.sync_creating=s->relay.sync_active=s->relay.phase_acquiring=s->relay.recovery_grace_active=s->relay.holdover_bound_known=s->relay.window_recovering=s->relay.held_time=s->relay.selected=s->relay.active=s->relay.map_valid=s->relay.phase_admitted=s->relay.quiet=true;}
void r2_ble_gm_diagnostics(uint32_t out[R2_GM_TRANSPORT_WORDS]){for(unsigned i=0;i<R2_GM_TRANSPORT_WORDS;i++)out[i]=UINT32_MAX;}
void r2_radio_service_gm_diagnostics(uint32_t out[6]){for(unsigned i=0;i<6;i++)out[i]=UINT32_MAX;}
uint32_t r2_radio_service_gnss_sequence(void){return UINT32_MAX;}
int r2_service_configure(const struct r2_identity *i,const struct r2_enrollment *e){return 0;}
void r2_service_clock(const struct r2_clock *c){}
void r2_service_supervise(void){}
uint32_t r2_service_stack_unused(void){return UINT32_MAX;}
uint32_t r2_radio_service_profile_flushes(void){return 0;}
uint32_t r2_radio_service_stack_unused(void){return UINT32_MAX;}
void r2_service_ui(struct r2_ui_model *m){}
int r2_service_profile(unsigned mode,uint64_t at){return 0;}
void r2_service_profile_status(uint32_t out[5]){out[0]=out[1]=out[2]=1;out[3]=4;out[4]=65535;}
void r2_service_stop(void){}
int r2_service_close_result(void){return 0;}
int r2_service_preflight(uint32_t s,uint64_t a){return 0;}
int r2_service_start(uint32_t s,uint64_t a){return 0;}
static unsigned normal_starts;
int r2_service_run(uint64_t a){normal_starts++;return 0;}
int r2_service_relay(uint64_t session,uint32_t seconds,uint64_t arrival){return 0;}
int r2_service_name(const char *name){return 0;}
bool r2_shadow_enabled(void){return false;}
void r2_shadow_serving(bool gm){}
void r2_shadow_enable(bool enabled){}
bool r2_shadow_read(uint32_t after,struct rc_shadow_record *r,struct rc_shadow_status *s){memset(r,0,sizeof *r);memset(s,0,sizeof *s);return false;}
void r2_time_copy(struct r2_clock *c){memset(c,0,sizeof(*c));}
int r2_diag_init(const struct r2_identity *i){return -ENOSPC;}
int r2_diag_state(void){return INT_MIN;}
int r2_diag_read(unsigned b,unsigned c,uint8_t out[64]){memset(out,0xff,64);return 0;}
bool r2_diag_busy(void){return false;}
bool r2_diag_saved(void){return false;}
bool r2_diag_maintenance_safe(void){return false;}
bool r2_diag_shutdown_ready(void){return false;}
void r2_diag_poll(void){}
void r2_diag_sample(const struct r2_service_status *s,const struct r2_clock_view *c){}
void r2_diag_command(uint32_t r,unsigned o,int v,uint64_t a){}
void r2_diag_finish(int e){}
int r2_diag_arm(uint64_t a,uint32_t s){return -EACCES;}
int r2_diag_mark(void){return 0;}
void r2_diag_quiesce(void){}
void r2_diag_cancel_arm(int e){}
#endif
#include "build_identity.h"
static struct r2_platform fixture;
static char captured[R2_REPLY_BYTES+1];
static int64_t fake_now=INT64_MAX;
static bool dequeue_case,delivered;
static jmp_buf completed;
int64_t k_uptime_get(void) { return fake_now; }
static inline void k_thread_priority_set(void *thread,int priority) {}
static inline void *k_current_get(void) {return NULL;}
void r2_platform_snapshot(struct r2_platform *out) {*out=fixture;}
void r2_console_snapshot(struct r2_io *out) {memset(out,0xff,sizeof(*out));out->dtr=true;}
int r2_board_safe(void) {return INT_MIN;}
int r2_console_reply(const char *bytes,size_t size,uint64_t expiry) {assert(size<=R2_REPLY_BYTES);memcpy(captured,bytes,size);captured[size]=0;return 0;}
void r2_identify(struct r2_identity *id) {}
void r2_platform_start(void) {}
void r2_console_start(void) {}
bool r2_console_get(struct r2_command *out) {
 if(!dequeue_case)return false;
 if(delivered)longjmp(completed,1);
 delivered=true;out->arrival=++fake_now;
 snprintf(out->text,sizeof(out->text),"r2 00000001 1111111111111111 %s 000000000000007b status",R2_SOURCE_SHA);
 return true;
}
int r2_platform_close(void) {++platform_closes;return 0;}
int r2_bootloader_request(void) {return -1;}
void r2_time_init(void) {}
void r2_time_start(void) {}
void r2_time_service(void) {}
void r2_time_stop(void) {++time_stops;}
int r2_time_close_result(void) { return 0; }
void r2_time_snapshot(struct r2_clock_view *out,uint32_t *drops) {
 memset(out,0,sizeof(*out));out->utc_us=out->mono_us=INT64_MAX;out->phase_us=INT64_MIN;
 out->gps_age_us=out->pps_age_us=INT64_MAX;out->rate_ppb=INT32_MIN;
 out->generation=out->source_epoch=out->pairs=out->accepted=out->rejected=out->faults=UINT32_MAX;
 out->state=out->reason=out->association=out->sats_used=UINT32_MAX;
 out->pps_edges=out->pps_rejected=out->gga_count=UINT32_MAX;*drops=UINT32_MAX;
}
#define main r2_application_entry
#include "../src/main.c"
#undef main
int main(void) {
 memset(&fixture,0xff,sizeof(fixture));fixture.clock_done=fixture.bt_done=fixture.close_done=true;
 memset(&identity,0xff,sizeof(identity));memset(identity.silicon,'f',16);identity.silicon[16]=0;
 strcpy(identity.unit,"TC-03");identity.result=INT_MIN;identity.battery=-1;
 heartbeat=last_request=rejected=trace_lost=UINT32_MAX;trace_count=R2_TRACE_COUNT;
 maintenance.phase=R2_FAULT;maintenance.error=INT_MIN;maintenance.deadline=UINT64_MAX;
 for(unsigned i=0;i<R2_TRACE_COUNT;i++) trace[i]=(struct trace_record){UINT64_MAX,UINT32_MAX,INT_MIN};
 struct r2_request request={.id=UINT32_MAX,.op=R2_TRACE};
 for(unsigned page=0;page<4;page++) {
   request.trace_page=page;captured[0]=0;reply(&request,1,INT_MIN);
   assert(captured[0] && strstr(captured,"R2 END ffffffff") && strlen(captured)<R2_REPLY_BYTES);
   assert(strstr(captured,"18446744073709551615,4294967295,-2147483648"));
 }
 request.op=R2_CLOCK;captured[0]=0;reply(&request,1,0);assert(strstr(captured,"POSIX_UTC_ESTIMATE")&&strstr(captured,"R2 END ffffffff"));
#ifdef R2_NATIVE_BLE
 request.op=R2_RADIO_AUDIT;reply(&request,1,0);assert(strstr(captured,"RAW_ADDRESS_UNCALIBRATED")&&strstr(captured,"18446744073709551615")&&strstr(captured,"R2 END ffffffff"));
 request.op=R2_JOIN;captured[0]=0;reply(&request,1,0);
 assert(strstr(captured,"name_utf8_hex")&&strstr(captured,"control_capacity\":4")&&strstr(captured,"R2 END ffffffff"));
 gm_diagnostic_reply=true;reply(&request,1,0);gm_diagnostic_reply=false;
 assert(strstr(captured,"\"gm_transport\":[4294967295,")&&strstr(captured,"\"gm_radio\":[4294967295,")&&strstr(captured,"R2 END ffffffff"));
 request.op=R2_ROUTE;captured[0]=0;reply(&request,1,0);
 assert(strstr(captured,"\"unit\":\"TC-03\"")&&strstr(captured,"source_projection_revision")&&strstr(captured,"upstream_identity")&&strstr(captured,"accepted_corrections")&&strstr(captured,"R2 END ffffffff"));
 request.op=R2_PRECISION;captured[0]=0;reply(&request,1,0);assert(strstr(captured,"absolute_utc_qualified")&&strstr(captured,"source_eligible")&&strstr(captured,"hot_ready")&&strstr(captured,"quality")&&strstr(captured,"R2 END ffffffff"));
 request.op=R2_NETWORK;captured[0]=0;reply(&request,1,0);assert(strstr(captured,"service_receipts")&&strstr(captured,"R2 END ffffffff"));
 assert(strstr(captured,"\"management_until_us\":18446744073709551615")&&strstr(captured,"\"last_hot_withdrawal_reason\":4294967295")&&strstr(captured,"\"rx_queue_peak\":4294967295"));
 assert(strlen(captured)<R2_REPLY_BYTES);
 request.op=R2_DIAG;request.trace_page=255;reply(&request,1,0);assert(strstr(captured,"chunk")&&strstr(captured,"R2 END ffffffff"));
#endif
 puts("R2 actual response formatter: all pages at maximum field widths PASS");

 /* Exercise actual main admission when RX arrives after loop time sampling. */
 strcpy(identity.silicon,"1111111111111111");strcpy(identity.unit,"TC-01");identity.boot=123;
 last_request=rejected=trace_count=trace_lost=heartbeat=0;maintenance=(struct r2_maint){0};
 startup_decided=platform_ready=true;fake_now=1000;dequeue_case=true;
 if(!setjmp(completed))r2_application_entry();
 assert(last_request==1&&rejected==0&&strstr(captured,"R2 END 00000001"));
#ifdef R2_NATIVE_BLE
 assert(normal_starts==1);puts("Actual main starts normal BLE despite diagnostic init/arm failure PASS");
 assert(!ui_stops&&!time_stops&&!platform_closes);
 puts("Actual main retains UI/time/platform after contained normal service fault PASS");
#endif
 puts("Actual main accepts RX arriving after loop-start sample; original age gate retained PASS");
}
