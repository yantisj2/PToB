/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Current-boot RAM/USB diagnostics. Historical NV banks are never opened. */
#include "diagnostic.h"
#include "controller_diag.h"
#include "health.h"
#include "core.h"
#include "time_service.h"
#include "build_identity.h"
#include <errno.h>
#include <string.h>
#ifdef R2_LORA
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch __aligned(8);
#endif
static uint32_t package[128] __aligned(4),previous[16],current[16],commands[3][4];
static struct r2_identity identity;
static struct r2_service_status last_service;
static struct r2_clock_view last_clock;
static uint64_t previous_root_boot,previous_standby_boot,generation;
static uint32_t previous_pair_epoch,seen_signature;
static unsigned command_count,command_next,automatic_count;
static int phase;
static bool normal,finish_requested,quiescing,have_sample,pending_post;
static uint64_t sample_due,next_mark,next_event,hour_start,last_event;
static struct r2_diag_stats totals;
static void increment(uint32_t *p){if(*p<UINT32_MAX)++*p;}
static uint32_t millis(uint64_t us){return us/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(us/1000);}
static uint32_t remaining(uint64_t until,uint64_t at){return until>at?millis(until-at):0;}
int r2_diag_init(const struct r2_identity *id){
 if(!id||id->result)return -EACCES;
 identity=*id;memset(package,0,sizeof package);memset(previous,0,sizeof previous);
 memset(current,0,sizeof current);memset(commands,0,sizeof commands);memset(&totals,0,sizeof totals);
#ifdef R2_LORA
 memset(&r2_diag_scratch,0,sizeof r2_diag_scratch);
#endif
 memset(&last_service,0,sizeof last_service);memset(&last_clock,0,sizeof last_clock);
 previous_root_boot=previous_standby_boot=previous_pair_epoch=seen_signature=0;
 sample_due=next_mark=next_event=hour_start=last_event=0;
 generation=0;command_count=command_next=automatic_count=0;
 normal=finish_requested=quiescing=have_sample=pending_post=false;
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
 r2_controller_diag_init();
#endif
 phase=1;return 0;
}
int r2_diag_arm(uint64_t arrival,uint32_t seconds){
 (void)arrival;if(phase!=1||seconds>120)return -EACCES;
 normal=seconds==0;phase=2;return 0;
}
static void snapshot(uint32_t p[16],const struct r2_service_status *s,const struct r2_ui_status *u)
{
 p[0]=millis(s->at);p[1]=s->self|(s->coordinator<<8)|(s->root<<16)|(s->standby<<24);
 p[2]=s->serving|((uint32_t)s->follower<<1)|((uint32_t)s->active<<2)|((uint32_t)s->hot_ready<<3)|((uint32_t)s->source_eligible<<4)|((uint32_t)s->precision.rms_valid<<5)|((uint32_t)s->precision.map_valid<<6)|((uint32_t)s->precision.locked<<7);
 p[2]|=((uint32_t)s->relay.selected<<8)|((uint32_t)s->relay_pending<<9)|((uint32_t)s->relay.active<<10)|
  ((uint32_t)(s->relay.selected&&s->relay.role==1)<<11)|((uint32_t)!!s->direct_path[0]<<12)|
  ((uint32_t)s->relay.map_valid<<13)|((uint32_t)s->relay.phase_admitted<<14)|((uint32_t)(s->relay.slot&3)<<15);
 p[3]=s->hot_refusal;p[4]=s->source_epoch;p[5]=s->source_evidence&&s->at>=s->source_evidence?millis(s->at-s->source_evidence):UINT32_MAX;
 p[6]=remaining(s->service_until,s->at);uint32_t last_age=millis((uint64_t)s->record_age_us+999),max_age=millis((uint64_t)s->record_age_max_us+999);
 p[7]=(last_age>65535?65535:last_age)|((max_age>65535?65535:max_age)<<16);p[8]=s->rx_latency_max_us;
 p[9]=s->backlog|(s->rx_queue_peak<<16);p[10]=s->drops;p[11]=s->rejected;p[12]=s->worker_max_us;p[13]=s->precision.rms_ns;p[14]=u->max_frame_ms;p[15]=u->missed;
}
static uint32_t ram_stack_unused(bool system){
#ifdef CONFIG_INIT_STACKS
 size_t unused;struct k_thread *t=system?&k_sys_work_q.thread:k_current_get();
 if(!k_thread_stack_space_get(t,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 (void)system;return UINT32_MAX;
}
static int submit(unsigned reason,int error){
 if(quiescing&&reason!=2)return -ESHUTDOWN;
 if(phase<1||pending_post)return -EAGAIN;
 if(generation==UINT64_MAX)return -EOVERFLOW;
 memset(package,0,sizeof package);package[0]=0x44325231;package[1]=8;package[3]=512;
 memcpy(&package[4],&identity.boot,8);memcpy(&package[6],identity.silicon,16);
 memcpy(&package[10],R2_SOURCE_SHA,64);
 memcpy(&package[26],&previous_standby_boot,8);package[28]=previous_pair_epoch;
 package[29]=last_service.precision.train;package[30]=r2_ui_stack_unused();package[31]=totals.coalesced;
 package[32]=last_service.precision.overflows;package[33]=(previous[0]?1u:0u)|(have_sample?2u:0u);
 uint64_t now=k_uptime_get(),trigger=(reason&64)&&last_service.hot_withdrawal_at?last_service.hot_withdrawal_at/1000:now;
 memcpy(&package[34],&trigger,8);package[37]=have_sample?2:0;package[38]=command_count;
 package[41]=UINT32_MAX; /* no diagnostic storage thread */
 package[47]=last_clock.reason;package[48]=ram_stack_unused(false);package[49]=ram_stack_unused(true);
 struct r2_ui_status ui;r2_ui_snapshot(&ui);package[50]=ui.error;package[51]=ui.max_frame_ms;
 memcpy(&package[52],&last_service.root_boot,8);memcpy(&package[54],&last_service.standby_boot,8);
 package[56]=last_service.source_epoch;package[57]=last_service.pair_epoch;package[58]=last_service.precision.samples;
 package[59]=(reason&64)?last_service.hot_withdrawal_reason:last_service.hot_refusal;
 package[60]=remaining(last_service.service_until,last_service.at);package[61]=remaining(last_service.pair_until,last_service.at);
 package[62]=remaining(last_service.management_until,last_service.at);package[63]=last_clock.pps_age_us>=0?millis(last_clock.pps_age_us):UINT32_MAX;
 memcpy(&package[124],&previous_root_boot,8);
 ++generation;memcpy(&package[42],&generation,8);package[44]=reason;package[36]=(uint32_t)error;
 memcpy(&package[64],previous,sizeof previous);memcpy(&package[80],current,sizeof current);
 for(unsigned i=0;i<command_count;i++)memcpy(&package[112+i*4],commands[(command_next+3-command_count+i)%3],16);
#ifdef R2_LORA
 const struct r2_lora_retained_fault *f=&r2_diag_scratch.lora.retained;
 if(f->boot&&f->record[0]==R2_LORA_FAULT_MAGIC){
  memcpy(&package[39],&f->boot,8);memcpy(&package[80],f->record,64);
  package[44]|=0x800u|(f->terminal?0x1000u:0u);
 }
#endif
 pending_post=true;phase=3;return 0;
}
int r2_diag_mark(void){
 if(quiescing)return -ESHUTDOWN;
 if(!normal)return -EACCES;
 uint64_t now=k_uptime_get();if(now<next_mark)return -EAGAIN;
 int rc=submit(0x100,0);if(!rc)next_mark=now+60000;return rc;
}
void r2_diag_sample(const struct r2_service_status *s,const struct r2_clock_view *clock)
{
 uint64_t now=k_uptime_get();struct r2_ui_status ui;r2_ui_snapshot(&ui);
 uint32_t reason=0;
 if(have_sample){
  if(s->root_boot!=last_service.root_boot||s->source_epoch!=last_service.source_epoch||s->coordinator!=last_service.coordinator||s->standby_boot!=last_service.standby_boot)reason|=4;
  if(s->hot_ready!=last_service.hot_ready)reason|=8;
  if(s->hot_withdrawals!=last_service.hot_withdrawals)reason|=64;
  if((last_service.serving||last_service.follower)&&!s->serving&&!s->follower&&!s->relay.quiet&&!last_service.relay.quiet)reason|=16;
#if defined(R2_BOUNDARY_FIELD) || defined(R2_SWITCHABLE_FIELD)
  if(s->field_profile&&(s->relay.slot_changes!=last_service.relay.slot_changes||s->relay.slot_rollbacks!=last_service.relay.slot_rollbacks))reason|=128;
#endif
  /* Ordinary map warmup/route transitions stay in bounded RAM/status.
   * They do not initiate RAM record updates on a healthy field clock. */
  if((s->error&&s->error!=last_service.error)||s->drops>last_service.drops||ui.error)reason|=1;
  if(s->precision.overflows>last_service.precision.overflows||(clock->faults>last_clock.faults&&(clock->reason==R2_INPUT_DRIVER||clock->reason==R2_INPUT_OVERFLOW||clock->reason==R2_INPUT_TIMEBASE)))reason|=32;
 }
 if(!have_sample||now>=sample_due){memcpy(previous,current,sizeof previous);previous_root_boot=last_service.root_boot;previous_standby_boot=last_service.standby_boot;previous_pair_epoch=last_service.pair_epoch;sample_due=now+250;}
 snapshot(current,s,&ui);last_service=*s;last_clock=*clock;have_sample=true;
 if(phase==2)phase=5;
 if(!reason)return;
 if(!normal||quiescing||finish_requested||r2_diag_state()<1)return;
 if(r2_diag_busy()){increment(&totals.coalesced);return;}
 if(now-hour_start>=3600000){hour_start=now;automatic_count=0;}
 uint32_t signature=reason^((reason&64)?s->hot_withdrawal_reason:s->hot_refusal)^(s->root<<16)^(s->coordinator<<24)^s->source_epoch;
 if(now<next_event||automatic_count>=8||(signature==seen_signature&&now-last_event<60000)){increment(&totals.coalesced);return;}
 /* At most8 automatic records/hour, minimum10s apart; identical faults at
  * least60s apart. No timer alone records: a real pending event is needed. */
 if(!submit(reason,s->error)){automatic_count++;next_event=now+10000;last_event=now;seen_signature=signature;}
}
void r2_diag_command(uint32_t request,unsigned op,int result,uint64_t arrival)
{
 if(r2_diag_state()<1||quiescing)return;
 /* Read-only polling must not evict the useful recent command context. */
 if(!result&&op!=R2_PREPARE&&op!=R2_COMMIT&&op!=R2_UI&&op!=R2_BLE&&op!=R2_MARK)return;
 uint32_t *p=commands[command_next];command_next=(command_next+1)%3;if(command_count<3)command_count++;
 p[0]=(uint32_t)arrival;p[1]=request;p[2]=op;p[3]=result;
}
void r2_diag_uc_trace(const uint32_t trace[3])
{
 if(r2_diag_state()<1||quiescing)return;
 uint32_t *p=commands[command_next];command_next=(command_next+1)%3;if(command_count<3)command_count++;
 uint64_t now=k_uptime_get();p[0]=(uint32_t)now;p[1]=trace[0];p[2]=trace[1];p[3]=trace[2];
 /* Actual first submission, not an idle pre-run marker. Existing pages,
  * record/mark limits and writer remain. At most8 UCS1 marks per hour. */
 static uint64_t hour;static unsigned count;
 if(now-hour>=3600000){hour=now;count=0;}
 if(trace[1]==UINT32_C(0x55430104)&&count<8&&!r2_diag_mark())++count;
}
void r2_diag_finish(int error){
 r2_diag_quiesce();if(finish_requested||pending_post||phase<1)return;
 finish_requested=true;(void)submit(2,error);
}
void r2_diag_watchdog(unsigned retry,int error){
 r2_diag_command(0x57440000u|retry,0x5744u,error,k_uptime_get());
 if(normal&&!quiescing&&!finish_requested&&!pending_post&&phase>=1)(void)submit(0x200,error);
}
int r2_diag_lora(const uint32_t record[16],bool terminal,uint64_t *token){
 if(!record||!token||record[0]!=0x4c525731)return -EINVAL;
 if(!normal||quiescing||finish_requested||phase<1)return -ESHUTDOWN;
 if(pending_post)return -EAGAIN;
#ifdef R2_LORA
 if(!terminal&&r2_diag_scratch.lora.retained.terminal)return -EAGAIN;
#endif
 int rc=submit(0x400,(int32_t)record[1]);if(rc)return rc;
 memcpy(&package[80],record,64);*token=generation;
#ifdef R2_LORA
 struct r2_lora_retained_fault *f=&r2_diag_scratch.lora.retained;
 memcpy(f->record,record,64);f->boot=identity.boot;f->terminal=terminal;
 memcpy(&package[39],&f->boot,8);package[44]|=terminal?0x1000u:0;
#endif
 return 0;
}
void r2_diag_poll(void){
 if(!pending_post)return;
 uint64_t now=k_uptime_get();memcpy(&package[45],&now,8);
 if(have_sample){memcpy(&package[96],current,sizeof current);package[37]=3;}
 package[126]=r2_crc32(package,504);package[127]=0x434f4d31;
 pending_post=false;phase=4;increment(&totals.saved);
}
int r2_diag_record_result(uint64_t token){if(!token||token>generation)return -EINVAL;return token==generation&&pending_post?-EINPROGRESS:0;}
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
bool r2_controller_diag_ready_locked(void){return phase>=1;}
#endif
int r2_diag_crash_result(void){return -ENOTSUP;} /* no cross-boot crash persistence */
int r2_diag_state(void){return phase;}
int r2_diag_read(unsigned bank,unsigned chunk,uint8_t out[64]){
 if(bank||chunk>=8||pending_post||phase!=4)return -ENOENT;
 memcpy(out,(const uint8_t *)package+chunk*64,64);return 0;
}
bool r2_diag_busy(void){return pending_post;}
bool r2_diag_saved(void){return phase==4&&!pending_post;}
bool r2_diag_maintenance_safe(void){return !pending_post;}
bool r2_diag_shutdown_ready(void){return !pending_post&&(finish_requested||phase<0);}
void r2_diag_quiesce(void){quiescing=true;}
void r2_diag_cancel_arm(int error){if(phase==2&&!have_sample&&!command_count)phase=error?error:-ECANCELED;}
void r2_diag_stats(struct r2_diag_stats *out){*out=totals;} /* write/erase fields always zero */
uint32_t r2_diag_stack_unused(void){return UINT32_MAX;} /* no storage thread */
