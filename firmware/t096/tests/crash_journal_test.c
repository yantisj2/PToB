/* MIT. Copyright(c)2026 Jonathan Yantis. Actual journal and GM7 start gate. */
#define R2_HEALTH_H
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "crash_record.h"
enum {R2_HEALTH_FLASH,R2_HEALTH_PA,R2_HEALTH_LANES};
enum {R2_HEALTH_ERASE=1,R2_HEALTH_WRITE,R2_HEALTH_COMMIT,R2_HEALTH_STORAGE_CHECK};
static uint32_t retained[128];static bool pending;static unsigned handoffs;
static void r2_health_operation(unsigned l,unsigned op,int rc){}
static const volatile uint32_t *r2_health_crash_pending(void){return pending?retained:NULL;}
static void r2_health_crash_saved(uint32_t id){assert(id==retained[126]);pending=false;handoffs++;}
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "time_service.h"
#include "diagnostic.h"
#define __aligned(n) __attribute__((aligned(n)))
#define DT_CHOSEN(x) 0
#define K_FOREVER -1
#define K_SEM_DEFINE(name,a,b) static int name
static int k_sem_take(int *s,int timeout){return 0;}
static void k_sem_give(int *s){}
static int64_t now;static unsigned writes,erases,mode,fail_write;
static void diagnostic_sleep(int ms){now+=ms;}
#define k_sleep diagnostic_sleep
static unsigned read_failure,read_mismatch,read_calls,write_delay;
static uint8_t storage[16384] __aligned(4);static bool closed;
static struct r2_service_status radio_status;
void r2_service_snapshot(struct r2_service_status *s){*s=radio_status;}
void r2_ui_snapshot(struct r2_ui_status *s){memset(s,0,sizeof(*s));s->max_frame_ms=17;}
uint32_t r2_ui_stack_unused(void){return 1024;}
int64_t k_uptime_get(void){return now;}
void r2_platform_snapshot(struct r2_platform *p){memset(p,0,sizeof(*p));p->close_done=closed;}
int r2_ui_close_result(void){return closed?0:-EAGAIN;}
int r2_time_close_result(void){return closed?0:-EAGAIN;}
static int flash_read(const struct device *d,unsigned off,void *out,size_t n){assert(off>=R2_DIAG_START&&off+n<=R2_DIAG_START+sizeof(storage));read_calls++;if(read_failure==read_calls)return -EIO;memcpy(out,storage+(off-R2_DIAG_START),n);if(read_mismatch==read_calls)((uint8_t *)out)[0]^=1;return 0;}
static int flash_write(const struct device *d,unsigned off,const void *in,size_t n){
 writes++;now+=write_delay;assert(off>=R2_DIAG_START&&off+n<=R2_DIAG_START+8192);assert(n==508||n==4);
 if(mode==3)return -EIO;
 bool failing=fail_write&&writes==fail_write;size_t bytes=failing&&mode==8?n/2:n;
 if(failing&&mode!=8)return -EIO;
 for(size_t i=0;i<bytes;i++)assert((storage[off-R2_DIAG_START+i]&((const uint8_t*)in)[i])==((const uint8_t*)in)[i]);
 memcpy(storage+(off-R2_DIAG_START),in,bytes);return failing?-EIO:0;
}
static int flash_erase(const struct device *d,unsigned off,size_t n){
 assert((n==8192&&off==R2_DIAG_START)||(n==4096&&(off==R2_DIAG_START||off==R2_DIAG_START+4096)));erases++;
 bool partial=mode==1||(n==4096&&(mode==13||mode==17||mode==19));
 memset(storage+(off-R2_DIAG_START),255,partial?n/2:n);
 if(mode==2||(mode==18&&n==4096))now+=600;
 if(mode==14&&n==4096)now+=2100;
 return n==4096&&(mode==13||mode==17)?-EIO:0;
}
#include "../src/diagnostic.c"
static unsigned runs,failed_starts;static bool normal_started,normal_start_failed;static uint64_t boot_deadline=10000;
int r2_service_run(uint64_t at){runs++;return 0;}
static void r2_gm7_start_failed(int rc){failed_starts++;}
static void r2_diag_cancel_arm_dummy(void){}
static void event(unsigned a,int b){}
static void actual_start_gate(void){int rc=0;uint64_t start_now=k_uptime_get();
#include "crash_main_start.inc"
}

static void seed5(unsigned slot,uint64_t seq,const struct r2_identity *id){
 uint32_t p[128]={0};p[0]=0x44325231;p[1]=5;p[2]=slot;p[3]=512;p[127]=0x434f4d31;
 memcpy(&p[4],&id->boot,8);memcpy(&p[6],id->silicon,16);memcpy(&p[10],R2_SOURCE_SHA,64);memcpy(&p[42],&seq,8);p[126]=r2_crc32(p,504);memcpy(storage+slot*512,p,512);
}
static void finish_write(void){now+=100;r2_diag_poll();assert(!pending_post);store_once();}
static void reboot(const struct r2_identity *id){phase=0;writer_active=pending_post=quiescing=finish_requested=false;assert(!r2_diag_init(id));}
static void seed_crash(unsigned slot,uint64_t seq)
{uint32_t p[128];memcpy(p,retained,512);p[2]=slot;memcpy(&p[56],&seq,8);p[58]=retained[126]^1;p[126]=r2_crc32(p,504);assert(r2_crash_valid(p,false));memcpy(storage+slot*512,p,512);}
static void reset_boot(void)
{phase=0;writer_active=pending_post=quiescing=finish_requested=false;crash_persist_result=0;normal_started=normal_start_failed=false;runs=failed_starts=0;memset(&r2_diag_scratch,0,sizeof r2_diag_scratch);}
int main(int argc,char **argv)
{
 assert(argc==3);unsigned scenario=(unsigned)atoi(argv[1]);FILE *f=fopen(argv[2],"rb");assert(f&&fread(retained,4,128,f)==128&&!fclose(f));assert(r2_crash_valid(retained,true));
 memset(storage,255,sizeof storage);memset(storage+8192,0x53,8192);pending=true;
 struct r2_identity id={.boot=99};strcpy(id.silicon,"123456789abcdef0");
 seed5(0,10,&id);seed_crash(8,9); /* independent previous event/crash pages */
 unsigned old_crash_slot=8;uint8_t old_event[512],old_crash[512];memcpy(old_event,storage,512);memcpy(old_crash,storage+old_crash_slot*512,512);
 if(scenario==1||scenario==9||scenario==10){for(unsigned i=1;i<8;i++)seed5(i,i,&id);for(unsigned i=9;i<16;i++)seed5(i,i-8,&id);} /* both full+pinned */
 if(scenario==9)pending=false;
 if(scenario==10){uint32_t *p=(uint32_t *)(storage+8*512);p[58]=retained[126];p[126]=r2_crc32(p,504);memcpy(old_crash,p,512);}
 if(scenario==11){
  seed_crash(1,11);old_crash_slot=1;memcpy(old_crash,storage+512,512);
  for(unsigned i=2;i<8;i++)seed5(i,i-1,&id);for(unsigned i=9;i<16;i++)seed5(i,i-8,&id);
 }
 if(scenario==12)pending=false;
 if(scenario==13)read_failure=17; /* first actual payload readback */
 if(scenario==14)read_mismatch=17;
 if(scenario==2||scenario==3){mode=scenario==3?8:0;fail_write=1;}
 if(scenario==4)fail_write=2; /* torn commit */
 if(scenario==5)read_failure=16+9; /* payload reads8 + commit read1 after16scan reads */
 if(scenario==6)write_delay=500;
 if(scenario==7){ /* old-source event remains protected; not current authority */
  uint32_t *p=(uint32_t *)storage;memset(&p[10],'a',64);p[126]=r2_crc32(p,504);
  memcpy(old_event,storage,512);assert(!valid_record(p,0));
 }
 if(scenario==8){pending=false; /* flash-only cross-source crash survives cleanup */
  uint32_t *p=(uint32_t *)(storage+8*512);p[8]^=1;p[126]=r2_crc32(p,504);memcpy(old_crash,p,512);
 }
 assert(!r2_diag_init(&id));assert(phase==1);actual_start_gate();assert(normal_started&&!normal_start_failed&&runs==1&&!failed_starts);
 assert(!memcmp(old_event,storage,512)&&!memcmp(old_crash,storage+old_crash_slot*512,512));
 for(unsigned i=8192;i<sizeof storage;i++)assert(storage[i]==0x53);
 if(scenario==11){assert(!pending&&handoffs==1&&writes==2&&erases==1&&crash_slot==8&&protected_event_slot==0);}
 else if(scenario==12){
  assert(!r2_diag_mark());finish_write();assert(valid5((uint32_t *)(storage+512),1)&&!erases&&totals.erase_deferred&&crash_slot==8);
  assert(!memcmp(old_crash,storage+old_crash_slot*512,512));
 }
 else if(scenario==9||scenario==10){assert(!r2_diag_crash_result()&&!pending&&!writes&&!erases&&next_slot==R2_DIAG_SLOTS&&!erase_needed);assert(r2_diag_mark()==-ENOSPC);if(scenario==10)assert(handoffs==1);}
 else if(scenario==1){assert(r2_diag_crash_result()==-ENOSPC&&pending&&!writes&&!erases);assert(r2_diag_mark()==-ENOSPC);}
 else if((scenario>=2&&scenario<=6)||scenario==13||scenario==14){
  assert(r2_diag_crash_result()==(scenario==6?-ETIMEDOUT:-EIO)&&pending&&!handoffs);
  assert(r2_diag_mark()==r2_diag_crash_result());
  unsigned before=writes;mode=fail_write=read_failure=read_mismatch=write_delay=0;reset_boot();assert(!r2_diag_init(&id));
  assert(!pending&&handoffs==1&&!r2_diag_crash_result());
  if(scenario==5)assert(writes==before);else assert(writes==before+2);
 }else if(scenario==8){assert(!writes&&!erases&&!r2_diag_crash_result()&&crash_slot==8);}
 else{assert(!pending&&handoffs==1&&writes==2&&!erases&&crash_slot==1);assert(valid_crash((uint32_t *)(storage+512),1));}
 /* Original main deadline still rejects a late start even with healthy diag. */
 now=boot_deadline;normal_started=normal_start_failed=false;runs=failed_starts=0;actual_start_gate();assert(normal_start_failed&&!runs&&failed_starts==1);
 printf("PASS actual crash journal/start gate scenario%u, bank1 untouched, old event/crash preserved\n",scenario);
}
