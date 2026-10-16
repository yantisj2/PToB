#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>
#define CONFIG_ARM 1
#define __noinit
#define R2_APP_H
#define R2_SOURCE_SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define CODE_UNREACHABLE __builtin_unreachable()
#define __DMB() ((void)0)
#define __DSB() ((void)0)
#define __get_MSP() 128u
#define __get_PSP() 256u
#define __get_IPSR() 3u
#define __get_PRIMASK() 0u
#define __get_CONTROL() 0u
struct k_thread {struct {uintptr_t start;size_t size;} stack_info;};
static uint32_t cycle;
static void timing_init(void){}
static void timing_start(void){}
static uint64_t timing_freq_get(void){return 64000000;}
static uint64_t timing_counter_get(void){cycle+=8;return cycle;}
struct arch_esf {struct {uint32_t pc,lr,xpsr;} basic;};
static struct {uint32_t CFSR,HFSR,MMFAR,BFAR;} scb;
#define SCB (&scb)
static uint64_t clock_ms;
static bool dtr=true;
static unsigned sends,irq_depth;
static char sent[1536];
static jmp_buf fatal_exit;
struct r2_io {bool dtr;unsigned tx_drops,tx_expired;};
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned key){irq_depth=key;}
static unsigned arch_irq_lock(void){return irq_lock();}
static uint64_t k_uptime_get(void){return clock_ms;}
static uint32_t k_uptime_get_32(void){return (uint32_t)clock_ms;}
static void *k_current_get(void){return (void *)(uintptr_t)0x20001000;}
static int k_thread_stack_space_get(void *t,size_t *n){*n=512;return 0;}
static void r2_console_snapshot(struct r2_io *io){*io=(struct r2_io){dtr,0,0};}
static int r2_console_note(const char *s,size_t n,uint64_t expiry);
static void k_fatal_halt(unsigned reason){longjmp(fatal_exit,1);}

uint32_t r2_diag_stack_unused(void){return 1024;}
