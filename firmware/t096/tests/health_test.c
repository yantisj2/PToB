#include "health_fake.h"
#include "../src/health.c"
#include <assert.h>
static int r2_console_reply(const char *s,size_t n,uint64_t expiry){
 assert(!irq_depth);assert(n<sizeof sent);assert(expiry==clock_ms+250);
 memcpy(sent,s,n);sent[n]=0;++sends;return -ENOBUFS;
}
#if defined(R2_WATCHDOG)
static unsigned watchdog_reports;
void r2_watchdog_console(char *line,size_t capacity){assert(capacity==1536&&!irq_depth);++watchdog_reports;}
void r2_watchdog_fatal(unsigned reason){(void)reason;}
#endif
static void wrapper_check(void);
int main(void){
 assert(stack_window_valid(0x20001000,128,0x20001060));
 assert(!stack_window_valid(0x20001000,128,0x20001064));
 assert(!stack_window_valid(0x20001000,128,0x20001001));
 assert(!stack_window_valid(0x20001000,128,0x20000ffc));
 assert(!stack_window_valid(0x20001000,SIZE_MAX,0x20001000));
 assert(!stack_window_valid(0x1fffffff,128,0x20000000));
 assert(!stack_window_valid(0x20040000,128,0x20040000));
 memset((void *)r2_fault_capsule,0xa5,sizeof r2_fault_capsule);
 r2_health_init(0x1122334455667788ULL);assert(!previous[0]);
 assert(latency[0]==64000000&&latency[1]==8&&latency[2]==8);
 unsigned k=irq_lock();r2_health_lock_enter(0x10022,k);cycle+=6400;
 r2_health_lock_enter(0x20033,1);r2_health_lock_leave(1);
 r2_health_lock_leave(k);irq_unlock(k);
 assert(latency[3]==0x10022&&latency[4]==6408&&latency[8]==1);
 cycle=UINT32_MAX-10;k=irq_lock();r2_health_lock_enter(0x30044,k);cycle+=100;
 r2_health_lock_leave(k);irq_unlock(k);assert(latency[6]==108);
 wrapper_check();timing_setup();
 /* Restore a known maximum for capsule assertions after wrapper exercise. */
 latency[3]=0x10022;latency[4]=6408;
 clock_ms=65000;r2_health_beat();r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,-EINPROGRESS);
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_PA_START,0);
 r2_health_operation(99,99,99);assert(operations[0].op==R2_HEALTH_WRITE);
 r2_health_console();assert(sends==1&&strstr(sent,"\"main\":[1,65000]")&&strstr(sent,"\"flash\":[2,"));
 r2_health_console();assert(sends==1);clock_ms+=1000;dtr=false;r2_health_console();assert(sends==1);
 struct arch_esf esf={.basic={0x123456,0x654321,0x21000000}};
 scb.CFSR=0x10;
 latency[7]=UINT32_MAX;latency[8]=UINT32_MAX;
 if(!setjmp(fatal_exit))k_sys_fatal_error_handler(2,&esf);
 irq_depth=0;assert(r2_fault_capsule[43]==0x10022&&r2_fault_capsule[44]==6408&&!r2_fault_capsule[49]);
 assert(r2_fault_capsule[0]==FAULT_MAGIC&&r2_fault_capsule[FAULT_WORDS-1]==crc(r2_fault_capsule));
 assert(r2_fault_capsule[5]==esf.basic.pc&&r2_fault_capsule[8]==0x10&&r2_fault_capsule[18]==R2_HEALTH_WRITE);
 r2_health_init(9);assert(previous[2]==0x55667788&&previous[3]==0x11223344&&!r2_fault_capsule[0]);
 /* Full-width capsule must fit. Reporting never retries a saturated queue. */
 for(unsigned i=1;i<FAULT_WORDS;i++)previous[i]=UINT32_MAX;
 clock_ms+=1000;dtr=true;
#if defined(R2_WATCHDOG)
 r2_health_console();assert(watchdog_reports==1&&sends==1);
 r2_health_console();assert(watchdog_reports==1&&sends==1);clock_ms+=1000;
#endif
 r2_health_console();assert(sends==2&&strlen(sent)<sizeof sent&&strstr(sent,"]}\n"));
 memset(previous,0,sizeof previous);r2_fault_capsule[0]=FAULT_MAGIC;r2_fault_capsule[5]^=1;
 r2_health_init(10);assert(!previous[0]);
 if(!setjmp(fatal_exit))k_sys_fatal_error_handler(0,NULL);
 irq_depth=0;assert(!r2_fault_capsule[5]&&r2_fault_capsule[FAULT_WORDS-1]==crc(r2_fault_capsule));
 puts("health: corrupt/valid retained capsule, NULL frame, fault registers, pacing, no DTR, queue saturation, maximum widths PASS");
}

/* Compile and exercise the actual wrapper, preserving the saved native key. */
struct k_spinlock {unsigned held;};
typedef struct {int key;} k_spinlock_key_t;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *l){
 assert(!l->held);l->held=1;return (k_spinlock_key_t){(int)irq_lock()};
}
static void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t k){
 assert(l->held);l->held=0;irq_unlock((unsigned)k.key);
}
#define R2_LOCK_UNIT 42u
#include "health_lock.h"
static void wrapper_check(void){
 struct k_spinlock a={0},b={0};unsigned before=latency[7];
 k_spinlock_key_t ka=k_spin_lock(&a);unsigned outer=active_site;
 k_spinlock_key_t kb=k_spin_lock(&b);assert(active_site==outer);
 k_spin_unlock(&b,kb);assert(active_site==outer&&irq_depth==1);
 k_spin_unlock(&a,ka);assert(!active_site&&!irq_depth&&latency[7]==before+1);
 unsigned key=irq_lock();assert(active_site>>16==42);irq_unlock(key);
 assert(!active_site&&!irq_depth&&latency[7]==before+2);
}
