/* SPDX-License-Identifier: Apache-2.0 */
#include "diagnostic.h"
#include "controller_diag.h"
#include "health.h"
#include "crash_record.h"
#include "core.h"
#include "time_service.h"
#include "build_identity.h"
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#define R2_LOCK_UNIT 6u
#include "health_lock.h"
/* Schema5/6: append512B records in approved bank0 only. Public radio-synchronized
 * flash owner, no ISR/callback writes. Bank1/legacy and layout are untouched. */
static const struct device *flash=DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
static uint32_t package[128] __aligned(4);
#ifdef R2_LORA
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch __aligned(8);
#define scan_page r2_diag_scratch.scan
#else
static uint32_t scan_page[128] __aligned(4);
#endif
static uint32_t previous[16],current[16],commands[3][4];
static struct r2_identity identity;
static struct r2_service_status last_service;
/* Only the diagnostic writer calls storage_window/prepare_spare. Keep this
 * full snapshot off its 2KiB stack: snapshot timing accessors add >600 bytes
 * beneath it. It is distinct from main-owned last_service and never escapes. */
static struct r2_service_status storage_service;
static struct r2_clock_view last_clock;
static uint64_t previous_root_boot,previous_standby_boot;static uint32_t previous_pair_epoch;
static struct k_spinlock lock;
static int phase;
static unsigned command_count,command_next,next_slot,latest_slot=R2_DIAG_SLOTS;
static unsigned crash_slot=R2_DIAG_SLOTS,protected_event_slot=R2_DIAG_SLOTS;
static uint64_t crash_generation;
static int crash_persist_result;
static bool occupied[R2_DIAG_SLOTS],writer_active,pending_post,normal,erase_needed,thread_started,finish_requested,quiescing,have_sample;
static uint64_t due,post_until,sample_due,next_mark,generation,next_event,hour_start,last_event;
static unsigned automatic_count;
static uint32_t seen_signature;
static struct r2_diag_stats totals;
K_SEM_DEFINE(save_requested,0,1);
K_THREAD_STACK_DEFINE(diagnostic_stack,2048);
static struct k_thread thread;
static void increment(uint32_t *p){if(*p<UINT32_MAX)++*p;}
static void write_attempt(unsigned bytes){k_spinlock_key_t k=k_spin_lock(&lock);totals.write_bytes=totals.write_bytes>UINT32_MAX-bytes?UINT32_MAX:totals.write_bytes+bytes;k_spin_unlock(&lock,k);}
static uint32_t millis(uint64_t us){return us/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(us/1000);}
static uint32_t remaining(uint64_t until,uint64_t at){return until>at?millis(until-at):0;}
static int blank(unsigned start,unsigned bytes)
{
 uint32_t words[16];
 for(unsigned off=0;off<bytes;off+=sizeof(words)){
  int rc=flash_read(flash,R2_DIAG_START+start+off,words,sizeof(words));if(rc)return rc;
  for(unsigned i=0;i<16;i++)if(words[i]!=UINT32_MAX)return -EIO;
 }
 return 0;
}
static bool binding(const uint32_t *p)
{return !memcmp(&p[6],identity.silicon,16)&&!memcmp(&p[10],R2_SOURCE_SHA,64);}
static bool valid5(const uint32_t *p,unsigned slot)
{return p[0]==0x44325231&&p[1]==5&&p[2]==slot&&p[3]==512&&p[127]==0x434f4d31&&binding(p)&&p[37]<=3&&p[38]<=3&&p[39]==0&&p[40]==0&&r2_crc32(p,504)==p[126];}
static bool valid6(const uint32_t *p,unsigned slot)
{return p[0]==0x44325231&&p[1]==6&&p[2]==slot&&p[3]==512&&p[127]==0x434f4d31&&binding(p)&&p[37]<=3&&p[38]<=3&&(p[39]||p[40])&&
 ((p[44]&0xc00u)==0x400u||(p[44]&0xc00u)==0x800u)&&!(p[44]&~0x1fffu)&&p[80]==0x4c525731&&r2_crc32(p,504)==p[126];}
static bool valid_record(const uint32_t *p,unsigned slot)
{return valid5(p,slot)||valid6(p,slot);}
/* Crash schema is independently recognized across source versions. Only the
 * factory is compared to this image; original source/build remain in record. */
static bool valid_crash(const uint32_t *p,unsigned slot)
{return p[2]==slot&&r2_crash_valid(p,false)&&r2_crash_silicon(p,identity.silicon);}
static bool event_binding_any_source(const uint32_t *p)
{
 if(memcmp(&p[6],identity.silicon,16))return false;
 const char *source=(const char *)&p[10];
 for(unsigned i=0;i<64;i++)if(r2_crash_nibble(source[i])>15)return false;
 return true;
}
static bool valid_event_any_source(const uint32_t *p,unsigned slot)
{
 bool shape=p[0]==0x44325231&&p[2]==slot&&p[3]==512&&p[127]==0x434f4d31&&event_binding_any_source(p)&&p[37]<=3&&p[38]<=3;
 if(!shape||r2_crc32(p,504)!=p[126])return false;
 if(p[1]==5)return !p[39]&&!p[40];
 return p[1]==6&&(p[39]||p[40])&&((p[44]&0xc00u)==0x400u||(p[44]&0xc00u)==0x800u)&&!(p[44]&~0x1fffu)&&p[80]==0x4c525731;
}
static bool protected_page(unsigned page)
{return (latest_slot<R2_DIAG_SLOTS&&latest_slot/8==page)||
        (protected_event_slot<R2_DIAG_SLOTS&&protected_event_slot/8==page)||
        (crash_slot<R2_DIAG_SLOTS&&crash_slot/8==page);}
static void select_target(void)
{
 unsigned first=latest_slot<R2_DIAG_SLOTS?latest_slot/8:0;
 for(unsigned j=0;j<2;j++)for(unsigned i=0;i<8;i++){
  unsigned slot=(first^j)*8+i;
  if(!occupied[slot]){next_slot=slot;erase_needed=false;return;}
 }
 for(unsigned j=0;j<2;j++){
  unsigned page=first^j;if(protected_page(page))continue;
  next_slot=page*8;erase_needed=true;return;
 }
 next_slot=R2_DIAG_SLOTS;erase_needed=false; /* both pages pinned: retain RAM */
}
static bool storage_window(void)
{
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_STORAGE_CHECK,-EINPROGRESS);
 struct r2_service_status *s=&storage_service;r2_service_snapshot(s);
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_STORAGE_CHECK,0);
 uint64_t now=(uint64_t)k_uptime_get()*1000;
 if(!normal||!s->active||s->error||(!s->serving&&!s->follower))return true;
 if(now<s->at||now-s->at>=50000)return false;
 /* A compact append is one508B public write plus commit. Page erase retains
  * the existing full-permit/all-recipient ACK admission. Neither grants time. */
 return erase_needed?s->storage_window&&!s->backlog:s->service_until>now&&s->service_until-now>=250000;
}
static int erase_page(unsigned page,bool runtime)
{
 if(page>=2||protected_page(page))return -EACCES;
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_ERASE,-EINPROGRESS);
 int rc=flash_erase(flash,R2_DIAG_START+page*4096,4096);
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_ERASE,rc);
 if(!rc)rc=blank(page*4096,4096);
 if(!rc){k_spinlock_key_t k=k_spin_lock(&lock);if(runtime)increment(&totals.page_erases);for(unsigned i=0;i<8;i++)occupied[page*8+i]=false;k_spin_unlock(&lock,k);}
#if defined(R2_SERVICE_NORMAL)
 printk("R2_DIAG_ERASE_END page=%u at=%llu rc=%d runtime=%u\n",page,(unsigned long long)k_uptime_get(),rc,runtime);
#endif
 return rc;
}
static bool may_prepare(void)
{k_spinlock_key_t k=k_spin_lock(&lock);bool ok=!quiescing&&phase==3;k_spin_unlock(&lock,k);return ok;}
static bool preparation_budget(void)
{uint64_t now=k_uptime_get();return now<due&&due-now>1000&&may_prepare();}
static int prepare_spare(void)
{
 if(latest_slot>=R2_DIAG_SLOTS)return -EFAULT;
 unsigned page=(latest_slot/8)^1;bool dirty=false;
 if(protected_page(page)){increment(&totals.erase_deferred);return 0;}
 for(unsigned i=0;i<8;i++)dirty|=occupied[page*8+i];
 if(!dirty)return 0;
 /* Current event is already committed. Preparation is event-coupled, optional,
  * and must leave1s of the SAME original deadline before starting an erase.
  * No admission means defer preparation, never pretend an unsaved event passed. */
 bool previous_erase=erase_needed;erase_needed=true;
 while(preparation_budget()&&!storage_window())k_sleep(K_MSEC(2));
 int rc=0;
 if(preparation_budget()&&storage_window())rc=erase_page(page,true);
 else{k_spinlock_key_t k=k_spin_lock(&lock);increment(&totals.erase_deferred);k_spin_unlock(&lock,k);}
 erase_needed=previous_erase;return rc;
}
static void store_once(void)
{
 int rc=(uint64_t)k_uptime_get()>=due?-ETIMEDOUT:0;
#if defined(R2_SERVICE_NORMAL)
 printk("R2_DIAG_BEGIN slot=%u erase=%u at=%llu due=%llu\n",next_slot,erase_needed,(unsigned long long)k_uptime_get(),(unsigned long long)due);
#endif
 while(!rc&&!storage_window()){
  if((uint64_t)k_uptime_get()>=due){rc=-ETIMEDOUT;break;}
  k_sleep(K_MSEC(2));
 }
 unsigned target=next_slot,base=target*512,page=target/8;
#if defined(R2_SERVICE_NORMAL)
 printk("R2_DIAG_ADMITTED slot=%u at=%llu rc=%d\n",target,(unsigned long long)k_uptime_get(),rc);
#endif
 if(!rc&&erase_needed){
  /* Exhausted capacity remains a mandatory erase+append, with no deadline
   * extension or success based on preserving only the preceding record. */
  rc=erase_page(page,true);
 }
 if(!rc&&(uint64_t)k_uptime_get()>=due)rc=-ETIMEDOUT;
 if(!rc){write_attempt(508);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,-EINPROGRESS);rc=flash_write(flash,R2_DIAG_START+base,package,508);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,rc);}
 uint32_t check[16];
 for(unsigned off=0;off<508&&!rc;off+=64){
  unsigned n=508-off;if(n>64)n=64;
  rc=flash_read(flash,R2_DIAG_START+base+off,check,n);
  if(!rc&&memcmp(check,(uint8_t*)package+off,n))rc=-EIO;
 }
 if(!rc&&(uint64_t)k_uptime_get()>=due)rc=-ETIMEDOUT;
 if(!rc){write_attempt(4);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_COMMIT,-EINPROGRESS);rc=flash_write(flash,R2_DIAG_START+base+508,&package[127],4);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_COMMIT,rc);}
 if(!rc){rc=flash_read(flash,R2_DIAG_START+base+508,check,4);if(!rc&&check[0]!=package[127])rc=-EIO;}
 if(!rc&&(uint64_t)k_uptime_get()>=due)rc=-ETIMEDOUT;
 k_spinlock_key_t key=k_spin_lock(&lock);occupied[target]=true;
 if(!rc&&phase==3){latest_slot=protected_event_slot=target;increment(&totals.saved);}
 else if(!rc)rc=phase<0?phase:-EIO;
 k_spin_unlock(&lock,key);
 if(!rc)rc=prepare_spare();
 if(!rc&&(uint64_t)k_uptime_get()>=due)rc=-ETIMEDOUT;
 key=k_spin_lock(&lock);
 if(phase==3)phase=rc?rc:4;
 select_target();writer_active=false;k_spin_unlock(&lock,key);
#if defined(R2_SERVICE_NORMAL)
 printk("R2_DIAG_STORE slot=%u end=%llu rc=%d due=%llu saved=%u bytes=%u erases=%u deferred=%u\n",target,(unsigned long long)k_uptime_get(),rc,(unsigned long long)due,totals.saved,totals.write_bytes,totals.page_erases,totals.erase_deferred);
#endif
}
static void worker(void *a,void *b,void *c)
{for(;;){(void)k_sem_take(&save_requested,K_FOREVER);store_once();}}
/* Startup main owns package before the writer thread exists. Exactly the same
 *508B+4B public driver commit/readback mechanics, one slot, no new partition. */
static int store_boot_crash(const volatile uint32_t *retained)
{
 if(!retained)return 0;
 if(next_slot>=R2_DIAG_SLOTS||generation==UINT64_MAX)return -ENOSPC;
 uint64_t begin=k_uptime_get();int rc=0;
 if(erase_needed)rc=erase_page(next_slot/8,false);
 if(!rc&&(uint64_t)k_uptime_get()-begin>=500)rc=-ETIMEDOUT;
 if(rc)return rc;
 uint32_t id=retained[126];
 for(unsigned i=0;i<128;i++)package[i]=retained[i];
 package[2]=next_slot;generation++;memcpy(&package[56],&generation,8);package[58]=id;
 package[126]=r2_crc32(package,504);package[127]=0x434f4d31;
 if(!valid_crash(package,next_slot))return -EBADMSG;
 unsigned base=next_slot*512;uint32_t check[16];
 write_attempt(508);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,-EINPROGRESS);
 rc=flash_write(flash,R2_DIAG_START+base,package,508);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,rc);
 for(unsigned off=0;off<508&&!rc;off+=64){unsigned n=508-off;if(n>64)n=64;rc=flash_read(flash,R2_DIAG_START+base+off,check,n);if(!rc&&memcmp(check,(uint8_t *)package+off,n))rc=-EIO;}
 if(!rc&&(uint64_t)k_uptime_get()-begin>=500)rc=-ETIMEDOUT;
 if(!rc){write_attempt(4);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_COMMIT,-EINPROGRESS);rc=flash_write(flash,R2_DIAG_START+base+508,&package[127],4);r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_COMMIT,rc);}
 if(!rc){rc=flash_read(flash,R2_DIAG_START+base+508,check,4);if(!rc&&check[0]!=package[127])rc=-EIO;}
 if(!rc&&(uint64_t)k_uptime_get()-begin>=500)rc=-ETIMEDOUT;
 occupied[next_slot]=true;
 if(rc)return rc; /* RAM stays pending and prior crash page stays protected. */
 crash_slot=next_slot;crash_generation=generation;increment(&totals.saved);
 r2_health_crash_saved(id); /* only after every payload byte+commit read back */
 select_target();return 0;
}

int r2_diag_init(const struct r2_identity *id)
{
 if(phase||!id||id->result)return -EINVAL;
 identity=*id;if(!device_is_ready(flash)){phase=-ENODEV;return phase;}
 int rc=0;latest_slot=crash_slot=protected_event_slot=R2_DIAG_SLOTS;generation=crash_generation=0;bool dirty=false;
 uint64_t latest_event_generation=0,protected_generation=0;
 bool foreign_page[2]={true,true};
 const volatile uint32_t *pending=r2_health_crash_pending();bool pending_saved=false;
#ifdef R2_LORA
 uint64_t fault_generation=0;unsigned fault_slot=R2_DIAG_SLOTS;
#endif
 for(unsigned page=0;page<2;page++){
  uint32_t header[64],crc=UINT32_MAX;
  for(unsigned j=0;j<8;j++){
   unsigned slot=page*8+j;
   rc=flash_read(flash,R2_DIAG_START+slot*512,scan_page,512);if(rc){phase=rc;return rc;}
   uint32_t *p=scan_page;if(!j)memcpy(header,p,sizeof header);
   const uint8_t *bytes=(const uint8_t*)p;
   for(unsigned k=0;k<(j==7?504u:512u);k++){crc^=bytes[k];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1u));}
   occupied[slot]=false;for(unsigned k=0;k<128;k++)if(p[k]!=UINT32_MAX)occupied[slot]=dirty=true;
   uint64_t seq=0;memcpy(&seq,&p[42],8);
   bool valid=valid_record(p,slot),any_event=valid_event_any_source(p,slot);
   /* Reuse this scanner's full CRC/commit/factory validation. Only foreign
    * ordinary events qualify; current, fault, unknown or torn records do not. */
   foreign_page[page]&=p[1]==5&&any_event&&seq&&
    memcmp(&p[10],R2_SOURCE_SHA,64)&&!(p[44]&~0x3ffu)&&p[80]!=0x4c525731;
   if(any_event&&seq){
    if(seq>protected_generation){protected_generation=seq;protected_event_slot=slot;}
    if(seq>generation)generation=seq;
   }
   if(valid&&seq&&(latest_slot==R2_DIAG_SLOTS||seq>latest_event_generation)){latest_event_generation=seq;latest_slot=slot;}
   if(valid_crash(p,slot)){
    uint64_t crash_seq;memcpy(&crash_seq,&p[56],8);
    if(crash_seq>crash_generation){crash_generation=crash_seq;crash_slot=slot;}
    if(crash_seq>generation)generation=crash_seq;
    if(pending&&p[58]==pending[126]){
     bool same=true;for(unsigned n=4;n<24;n++)same&=p[n]==pending[n];
     if(same)pending_saved=true;
    }
   }
#ifdef R2_LORA
   /* package has no writer during startup. Keep the newest committed fault
    * copy there until the512B scanner finishes; no second scan reserve. */
   if(valid&&seq>fault_generation&&(p[44]&0xc00u)&&p[80]==R2_LORA_FAULT_MAGIC){
    memcpy(package,p,sizeof package);fault_generation=seq;fault_slot=slot;
   }
#endif
  }
  /* Legacy schema4 is scanned with512B RAM, never a retained4KiB buffer. */
  if(header[0]==0x44325231&&header[1]==4&&header[2]==page&&header[3]==4096&&scan_page[127]==0x434f4d31&&binding(header)&&scan_page[64]==64&&scan_page[65]==64&&~crc==scan_page[126]){
   uint64_t seq=0;memcpy(&seq,&header[42],8);for(unsigned j=0;j<8;j++)occupied[page*8+j]=true;
   if(seq&&(latest_slot==R2_DIAG_SLOTS||seq>latest_event_generation)){latest_event_generation=seq;latest_slot=page*8+7;}
   if(seq>protected_generation){protected_generation=seq;protected_event_slot=page*8+7;}if(seq>generation)generation=seq;
  }
 }
 /* New-image/malformed cleanup only before radio startup, original500ms. */
 if(latest_slot==R2_DIAG_SLOTS&&crash_slot==R2_DIAG_SLOTS&&protected_event_slot==R2_DIAG_SLOTS&&dirty){
  uint64_t begin=k_uptime_get();rc=flash_erase(flash,R2_DIAG_START,8192);
  if(!rc)rc=blank(0,8192);
  if(!rc&&(uint64_t)k_uptime_get()-begin>=500)rc=-ETIMEDOUT;
  if(rc){phase=rc;return rc;}
  memset(occupied,0,sizeof occupied);
 }
 if(pending_saved){r2_health_crash_saved(pending[126]);pending=NULL;}
 memset(scan_page,0,sizeof scan_page);select_target();
 if(pending&&next_slot==R2_DIAG_SLOTS&&generation<UINT64_MAX&&
    r2_crash_valid(pending,true)&&r2_crash_silicon(pending,identity.silicon)){
  unsigned page=protected_event_slot/8;
  if(page<2&&foreign_page[page]&&
     (latest_slot>=R2_DIAG_SLOTS||latest_slot/8!=page)&&
     (crash_slot>=R2_DIAG_SLOTS||crash_slot/8!=page)){
   /* Only the foreign ordinary-event pin is retired. Existing erase/append
    * keeps the committed crash page until a new complete crash is durable. */
   protected_event_slot=R2_DIAG_SLOTS;select_target();
  }
 }
#ifdef R2_LORA
 if(fault_generation){
  struct r2_lora_retained_fault *f=&r2_diag_scratch.lora.retained;
  memcpy(f->record,&package[80],64);
  memcpy(&f->boot,&package[package[1]==6?39:4],8);
  f->terminal=package[1]==6?!!(package[44]&0x1000u):(int32_t)f->record[1]!=-EINTR;
  /* A legacy full journal may have separate latest event/fault pages and no
   * empty handoff slot. Preserve both, rather than erase its only fault copy.
   * New schema6 records carry the fault before any obsolete-page erase. */
  if(erase_needed&&next_slot/8==fault_slot/8&&fault_generation!=generation){phase=-ENOSPC;return phase;}
 }
#endif
 /* Both pages full: prepare only the obsolete page before Bluetooth. Latest
  * committed evidence survives; this is capacity recovery, not a new event. */
 if(erase_needed&&!pending){uint64_t begin=k_uptime_get();rc=erase_page(next_slot/8,false);
  if(!rc&&(uint64_t)k_uptime_get()-begin>=500)rc=-ETIMEDOUT;
  if(rc){phase=rc;return rc;}select_target();
 }
 if(pending){
  /* Optional crash persistence must never turn a healthy scanner into a
   * normal-start failure. Preserve pendingRAM and freeze optional writes on
   * error; original GM7 readiness/boot deadlines remain unchanged. */
  crash_persist_result=store_boot_crash(pending);
 }
 /* Publish observer readiness only after the scanner has retired. Before
  * phase1, fatal capture returns synthetic UNKNOWN without reading this tail. */
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
 r2_controller_diag_init();
#endif
 phase=1;return 0;
}
int r2_diag_arm(uint64_t arrival,uint32_t seconds)
{
 if(r2_diag_state()!=1||seconds>120)return -EACCES;
 normal=seconds==0;command_count=command_next=0;sample_due=0;phase=2;return 0;
}
static uint32_t stack_unused(bool system)
{
#ifdef CONFIG_INIT_STACKS
 size_t unused;struct k_thread *t=system?&k_sys_work_q.thread:k_current_get();
 if(!k_thread_stack_space_get(t,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 (void)system;return UINT32_MAX;
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
static int submit(unsigned reason,int error)
{
 if(quiescing&&reason!=2)return -ESHUTDOWN;
 if(r2_diag_state()<1||r2_diag_busy())return -EAGAIN;
 if(crash_persist_result)return crash_persist_result;
 if(generation==UINT64_MAX)return -EOVERFLOW;
 if(next_slot>=R2_DIAG_SLOTS)return -ENOSPC;
 uint64_t now=k_uptime_get(),trigger=(reason&64)&&last_service.hot_withdrawal_at?last_service.hot_withdrawal_at/1000:now;struct r2_ui_status ui;r2_ui_snapshot(&ui);
 if(trigger>now||now-trigger>=2000){phase=-ETIMEDOUT;return -ETIMEDOUT;}
 memset(package,0,sizeof package);package[0]=0x44325231;package[1]=5;package[2]=next_slot;package[3]=512;
 memcpy(&package[4],&identity.boot,8);memcpy(&package[6],identity.silicon,16);memcpy(&package[10],R2_SOURCE_SHA,64);
 memcpy(&package[26],&previous_standby_boot,8);package[28]=previous_pair_epoch;package[29]=last_service.precision.train;package[30]=r2_ui_stack_unused();package[31]=totals.coalesced;
 package[32]=last_service.precision.overflows;package[33]=(previous[0]?1u:0u)|(have_sample?2u:0u)|((last_service.hot_withdrawals>0xffffff?0xffffff:last_service.hot_withdrawals)<<8);memcpy(&package[34],&trigger,8);package[36]=error;package[37]=have_sample?2:0;package[38]=command_count;package[41]=UINT32_MAX;
#ifdef CONFIG_INIT_STACKS
 size_t unused;if(thread_started&&!k_thread_stack_space_get(&thread,&unused)&&unused<=UINT32_MAX)package[41]=(uint32_t)unused;
#endif
 generation++;memcpy(&package[42],&generation,8);package[44]=reason;memcpy(&package[45],&now,8);package[47]=last_clock.reason;package[48]=stack_unused(false);package[49]=stack_unused(true);package[50]=ui.error;package[51]=ui.max_frame_ms;
 memcpy(&package[52],&last_service.root_boot,8);memcpy(&package[54],&last_service.standby_boot,8);package[56]=last_service.source_epoch;package[57]=last_service.pair_epoch;package[58]=last_service.precision.samples;package[59]=(reason&64)?last_service.hot_withdrawal_reason:last_service.hot_refusal;
 package[60]=remaining(last_service.service_until,last_service.at);package[61]=remaining(last_service.pair_until,last_service.at);package[62]=remaining(last_service.management_until,last_service.at);package[63]=last_clock.pps_age_us>=0?millis(last_clock.pps_age_us):UINT32_MAX;
 memcpy(&package[64],previous,sizeof previous);memcpy(&package[80],current,sizeof current);
 for(unsigned i=0;i<command_count;i++)memcpy(&package[112+i*4],commands[(command_next+3-command_count+i)%3],16);
 memcpy(&package[124],&previous_root_boot,8);
#ifdef R2_LORA
 const struct r2_lora_retained_fault *f=&r2_diag_scratch.lora.retained;
 if(f->boot&&f->record[0]==R2_LORA_FAULT_MAGIC){
  /* Existing record, write count, admission and deadline. Its verified commit
   * hands the primary fault to this page BEFORE prepare_spare may erase old.
   * Original fault boot/time stay distinct from this event's current boot. */
  package[1]=6;memcpy(&package[39],&f->boot,8);
  package[44]|=0x800u|(f->terminal?0x1000u:0u);memcpy(&package[80],f->record,64);
 }
#endif
 k_spinlock_key_t key=k_spin_lock(&lock);phase=3;writer_active=pending_post=true;due=trigger+2000;post_until=now+(reason==2?0:100);k_spin_unlock(&lock,key);return 0;
}
int r2_diag_mark(void)
{
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
   * They do not initiate background flash writes on a healthy field clock. */
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
  * least60s apart. No timer alone causes flash: a real pending event is needed. */
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
void r2_diag_finish(int error)
{
 r2_diag_quiesce();if(finish_requested||r2_diag_busy()||r2_diag_state()<1)return;
 struct r2_platform p;r2_platform_snapshot(&p);
 if(!p.close_done||p.close_result||r2_ui_close_result()||r2_time_close_result())return;
 finish_requested=true;(void)submit(2,error);
}
void r2_diag_watchdog(unsigned retry,int error)
{
 uint64_t now=k_uptime_get();
 r2_diag_command(0x57440000u|retry,0x5744u,error,now);
 if(!normal||quiescing||finish_requested||r2_diag_busy()||r2_diag_state()<1)return;
 if(now-hour_start>=3600000){hour_start=now;automatic_count=0;}
 if(now<next_event||automatic_count>=8)return;
 if(!submit(0x200,error)){automatic_count++;next_event=now+10000;last_event=now;}
}
int r2_diag_lora(const uint32_t record[16],bool terminal,uint64_t *token)
{
 /* LoRa fault uses snapshot80..95 plus schema6 reserved39/40 for original
  * boot. BLE pre/post, binding, CRC/commit and original writer/layout/deadline
  * remain. Main is the only caller; later ordinary events carry the primary.
  * Fatal evidence has one immediate opportunity even during a rate window;
  * recoverable launches retain the existing8/hour,10s event budget. */
 uint64_t now=k_uptime_get();
 if(!record||!token||record[0]!=0x4c525731)return -EINVAL;
 if(!normal||quiescing||finish_requested||r2_diag_state()<1)return -ESHUTDOWN;
 if(r2_diag_busy())return -EAGAIN;
 if(now-hour_start>=3600000){hour_start=now;automatic_count=0;}
 if(!terminal&&(now<next_event||automatic_count>=8))return -EAGAIN;
#ifdef R2_LORA
 /* An unacknowledged terminal cause survives recoverable launch noise. A new
  * terminal fault may supersede it, through the same verified commit handoff. */
 if(!terminal&&r2_diag_scratch.lora.retained.terminal)return -EAGAIN;
#endif
 int rc=submit(0x400,(int32_t)record[1]);
 if(!rc){memcpy(&package[80],record,64);*token=generation;
#ifdef R2_LORA
  struct r2_lora_retained_fault *f=&r2_diag_scratch.lora.retained;
  memcpy(f->record,record,64);f->boot=identity.boot;f->terminal=terminal;
  package[1]=6;memcpy(&package[39],&f->boot,8);
  package[44]=0x400u|(terminal?0x1000u:0u);
#endif
  if(automatic_count<UINT32_MAX)++automatic_count;
  next_event=now+10000;last_event=now;
 }
 return rc;
}
int r2_diag_record_result(uint64_t token)
{
 if(!token||token>generation)return -EINVAL;
 /* A later submission is admitted only after the prior writer finished
  * successfully: a failed phase is terminal until reboot. */
 if(token<generation)return 0;
 int state=r2_diag_state();
 return state<0?state:r2_diag_saved()?0:-EINPROGRESS;
}
void r2_diag_poll(void)
{
 uint64_t now=k_uptime_get();k_spinlock_key_t key=k_spin_lock(&lock);
 if(phase==3&&now>=due){phase=-ETIMEDOUT;if(pending_post){pending_post=false;writer_active=false;}k_spin_unlock(&lock,key);return;}
 if(!pending_post||(now<post_until&&!quiescing)){k_spin_unlock(&lock,key);return;}
 pending_post=false;k_spin_unlock(&lock,key);
 /* Main owns the frozen package until the semaphore admits the flash writer. */
 if(have_sample){memcpy(&package[96],current,sizeof current);package[37]=3;package[33]|=4u|(now<post_until?8u:0u);}
 memcpy(&package[45],&now,8);package[126]=r2_crc32(package,504);package[127]=0x434f4d31;
 if(!thread_started){k_thread_create(&thread,diagnostic_stack,K_THREAD_STACK_SIZEOF(diagnostic_stack),worker,NULL,NULL,NULL,10,0,K_NO_WAIT);
  key=k_spin_lock(&lock);thread_started=true;k_spin_unlock(&lock,key);}
 k_sem_give(&save_requested);
}

#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
/* Fatal/failure readers already hold the UP IRQ mask. Do not reacquire the
 * diagnostic spinlock: a fatal may have interrupted its owner. No state added. */
bool r2_controller_diag_ready_locked(void){return phase>=1;}
#endif
int r2_diag_crash_result(void){return crash_persist_result;}
int r2_diag_state(void){k_spinlock_key_t key=k_spin_lock(&lock);int p=phase;k_spin_unlock(&lock,key);return p;}
int r2_diag_read(unsigned bank,unsigned chunk,uint8_t out[64])
{if(bank>=2||chunk>=128||!device_is_ready(flash)||r2_diag_busy())return -EACCES;return flash_read(flash,R2_DIAG_START+bank*8192+chunk*64,out,64);}
bool r2_diag_busy(void){k_spinlock_key_t key=k_spin_lock(&lock);bool active=writer_active;k_spin_unlock(&lock,key);return active;}
bool r2_diag_saved(void){return r2_diag_state()==4&&!r2_diag_busy();}
bool r2_diag_maintenance_safe(void){return !r2_diag_busy();}
bool r2_diag_shutdown_ready(void){return !r2_diag_busy()&&(finish_requested||r2_diag_state()<0);}
void r2_diag_quiesce(void){k_spinlock_key_t key=k_spin_lock(&lock);quiescing=true;k_spin_unlock(&lock,key);}
void r2_diag_cancel_arm(int error){if(phase==2&&!have_sample&&!command_count)phase=error?error:-ECANCELED;}
void r2_diag_stats(struct r2_diag_stats *out){k_spinlock_key_t key=k_spin_lock(&lock);*out=totals;k_spin_unlock(&lock,key);}

uint32_t r2_diag_stack_unused(void)
{
#ifdef CONFIG_INIT_STACKS
 k_spinlock_key_t key=k_spin_lock(&lock);bool started=thread_started;k_spin_unlock(&lock,key);
 size_t unused;if(started&&!k_thread_stack_space_get(&thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}
