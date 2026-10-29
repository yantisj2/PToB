/* MIT. Copyright(c)2026 Jonathan Yantis.
 * Actual scanner + LoRa owner/service; explicit flash/IRQ/kernel peripherals. */
#include "app.h"
#include "lora_service.h"
#include "crash_record.h"
#include "actual_hw_fixture.inc"
static long atomic_and(atomic_t *p,long v){long old=*p;*p&=v;return old;}
static void atomic_or(atomic_t *p,long v){*p|=v;}
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *p){(void)p;return 0;}
static void k_spin_unlock(struct k_spinlock *p,int k){(void)p;(void)k;}
struct k_thread{int unused;};
#define K_THREAD_STACK_DEFINE(n,s) unsigned char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
static void observer_probe(unsigned where);
static void init_probe(unsigned where);
static void k_thread_priority_set(struct k_thread *t,int p){(void)t;assert(p==9||p==11);}
static void k_wakeup(struct k_thread *t){(void)t;}
static void k_thread_create(struct k_thread *t,void *s,size_t n,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int p,int q,int r){(void)t;(void)s;(void)n;(void)fn;(void)a;(void)b;(void)c;(void)p;(void)q;(void)r;init_probe(4);}
static void k_thread_name_set(struct k_thread *t,const char *n){(void)t;(void)n;}
#define R2_DIAG_SLOTS 16u
#define R2_DIAG_START 0xe1000u
#define R2_SOURCE_SHA "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define scan_page r2_diag_scratch.scan
static int diag_phase;
static uint32_t package[128];
static struct r2_identity identity;
static unsigned latest_slot=R2_DIAG_SLOTS,next_slot;
static uint64_t diag_generation,crash_generation;
static unsigned crash_slot=R2_DIAG_SLOTS,protected_event_slot=R2_DIAG_SLOTS;
static int crash_persist_result;
/* This scanner/LoRa fixture has no retained crash; crash persistence is tested
 * separately by watchdog_tests.py. Keep that unexercised leaf explicit. */
static const volatile uint32_t *r2_health_crash_pending(void){return NULL;}
static void r2_health_crash_saved(uint32_t crc){(void)crc;assert(false);}
static int store_boot_crash(const volatile uint32_t *p){(void)p;assert(false);return -EIO;}
static bool occupied[R2_DIAG_SLOTS],erase_needed;
static const struct device *flash=&device;
static uint8_t storage[8192];
static unsigned probes,mutations,init_probes;
static int r2_diag_state(void){return diag_phase;}
static int flash_read(const struct device *d,unsigned at,void *out,size_t n){(void)d;assert(at>=R2_DIAG_START&&at+n<=R2_DIAG_START+sizeof storage);memcpy(out,storage+(at-R2_DIAG_START),n);observer_probe(0);return 0;}
static int flash_erase(const struct device *d,unsigned at,size_t n){(void)d;assert(at==R2_DIAG_START&&n==sizeof storage);memset(storage,255,n);return 0;}
static int erase_page(unsigned page,bool runtime){(void)page;(void)runtime;assert(false);return -EIO;}
static unsigned journal_calls,reset_calls;static int reset_error;
static int r2_diag_lora(const uint32_t p[16],bool terminal,uint64_t *token){assert(terminal&&p[0]==R2_LORA_FAULT_MAGIC);journal_calls++;*token=7;return 0;}
static int r2_diag_record_result(uint64_t token){assert(token==7);return 0;}
static bool r2_diag_busy(void){return false;}
static void r2_watchdog_lora(int e,uint32_t s,uint32_t g,uint32_t c,int j){assert(s==256&&!g&&c==31&&!j);reset_calls++;reset_error=e;}
static bool r2_capture_now_at(struct r2_capture *c,uint64_t *us){*us=mock_us;return r2_capture_now(c);}
#include "actual_diag.inc"
#include "actual_service.inc"
static const struct r2_group_config test_group={.id=11,.epoch=1,.psk={1,2,3}};
static void observer_probe(unsigned where){
 uint8_t before[512];memcpy(before,&r2_diag_scratch,512);++probes;
 r2_lora_watchdog_check();if(memcmp(before,&r2_diag_scratch,512))++mutations;
#ifdef FIXED
 assert(!memcmp(before,&r2_diag_scratch,512)); /* No scratch read leads to a write. */
 assert(!atomic_get(&fault)&&!atomic_get(&stop_serial)&&!submissions);
#endif
 (void)where;
}
static void init_probe(unsigned where){
 ++init_probes;
#ifdef FIXED
 assert(!!atomic_get(&observer_ready)==(where==4));
#endif
 r2_lora_watchdog_check();
}
static void seed_record(struct r2_identity *id){
 uint32_t words[128]={0};memset(storage,255,sizeof storage);
 words[0]=0x44325231;words[1]=5;words[2]=0;words[3]=512;
 memcpy(words+4,&id->boot,8);memcpy(words+6,id->silicon,16);memcpy(words+10,R2_SOURCE_SHA,64);
 words[28]=0; /* No earlier appointed pair: aliases observer terminal=false. */
 words[42]=1;words[126]=r2_crc32(words,504);words[127]=0x434f4d31;
 memcpy(storage,words,512);
}
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 if(mode==1){
  struct r2_identity id={.boot=123};memcpy(id.silicon,"0123456789abcdef",17);seed_record(&id);
  mock_us=125000;assert(!r2_diag_init(&id));
  assert(probes>=32);
#ifdef FIXED
  assert(!mutations&&!atomic_get(&fault)&&latest_slot==0&&diag_generation==1);
#else
  assert(mutations&&atomic_get(&fault)==-ETIMEDOUT&&!submissions&&!hw_generation);
#endif
  mock_us=226000;r2_lora_init(2,22,1,&test_group);assert(init_probes==4);
  r2_lora_control(true,true,RC8_SELECT_NONE);service_step();
#ifdef FIXED
  assert(owner.active&&!owner.stopping&&!atomic_get(&faulted)&&!atomic_get(&fault)&&submissions&&hw_generation==1);
  puts("PASS actual scanner read/CRC + init publication: no preinit scratch mutation/fault, receiver starts");
#else
  assert(owner.stopping&&atomic_get(&faulted)&&!submissions&&!hw_generation&&atomic_get(&stop_serial)==2);
  mock_us=227000;r2_lora_watchdog_poll();
  assert(wd.record[1]==(uint32_t)-EIO&&wd.record[2]==227&&wd.record[3]==256&&wd.record[4]==226&&wd.record[5]==0&&wd.record[6]==0&&wd.record[11]==3);
  assert(atomic_get(&fault)==-ETIMEDOUT);
  mock_us=229000;service_step();assert(wd.closure==31&&atomic_get(&closed));r2_lora_watchdog_poll();
  mock_us=351000;r2_lora_watchdog_poll();assert(reset_calls==1&&reset_error==-EIO);
  puts("PASS original negative: preinit scan aliases monitor, sticky HWtimeout then START/EIO/gen0/SPI0/serial3/closure31");
#endif
 }else if(mode==2){
  diag_phase=1;mock_us=226000;r2_lora_init(2,22,1,&test_group);
  wd.monitor=0;wd.beat=1;mock_us=90000000;r2_lora_watchdog_check();assert(!wd.terminal&&!atomic_get(&fault));
  r2_lora_control(true,true,RC8_SELECT_NONE);service_step();uint32_t beat=wd.beat;
  mock_us=(uint64_t)(beat+R2_LORA_WORKER_MS-1)*1000;r2_lora_watchdog_check();assert(!wd.terminal&&!atomic_get(&fault));
  mock_us+=1000;r2_lora_watchdog_check();assert(wd.terminal&&atomic_get(&fault)==-ETIMEDOUT&&wd.serial==1);
  uint32_t first[16];memcpy(first,wd.record,64);mock_us+=1000000;r2_lora_watchdog_check();assert(!memcmp(first,wd.record,64)&&wd.serial==1);
  puts("PASS initialized observer: unmonitored silence, original29999/30000ms bound, first terminal freeze");
 }else if(mode==3){
  /* Previously terminal-looking scan data is not active evidence either. */
  wd.monitor=1;wd.terminal=1;wd.beat=0;mock_us=60000000;observer_probe(2);assert(!atomic_get(&fault)&&wd.terminal==1);
  puts("PASS terminal-looking preinit bytes preserved without hardware side effects");
 }else assert(false);
 puts("LIMIT explicit observer interleavings and fake flash/GPIO/SPI/capture/kernel/reset leaves; synthetic public schema5 fixture, no private field record or physical initiating proof.");
}
