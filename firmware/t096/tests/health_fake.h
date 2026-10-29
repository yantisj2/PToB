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
#if defined(HEALTH_FAKE_REGISTERS)
static uint32_t fake_fault_vector=3;
#define __get_IPSR() fake_fault_vector
#else
#define __get_IPSR() 3u
#endif
#define __get_PRIMASK() 0u
#define __get_CONTROL() 0u
struct k_thread {struct {uintptr_t start;size_t size;} stack_info;};
static uint32_t cycle;
static void timing_init(void){}
static void timing_start(void){}
static uint64_t timing_freq_get(void){return 64000000;}
static uint64_t timing_counter_get(void){cycle+=8;return cycle;}
struct _callee_saved {uint32_t v1,v2,v3,v4,v5,v6,v7,v8,psp;};
typedef struct _callee_saved _callee_saved_t;
struct arch_esf {
 struct {uint32_t r0,r1,r2,r3,r12,lr,pc,xpsr;} basic;
#if defined(CONFIG_EXTRA_EXCEPTION_INFO)
 struct {_callee_saved_t *callee;uintptr_t msp;uint32_t exc_return;} extra_info;
#endif
};
#if defined(HEALTH_FAKE_REGISTERS)
#define CONFIG_MP_MAX_NUM_CPUS 1
#define CONFIG_ISR_STACK_SIZE 3072
#define CONFIG_MAIN_STACK_SIZE 8192
#define K_THREAD_STACK_RESERVED 64
#define K_THREAD_STACK_SIZEOF(s) (sizeof(s)-K_THREAD_STACK_RESERVED)
#define K_THREAD_STACK_DECLARE(s,z) unsigned char s[(z)+K_THREAD_STACK_RESERVED]
#define R2_WATCHDOG_BINDING "1111111111111111111111111111111111111111111111111111111111111111"
#define K_KERNEL_STACK_ARRAY_DECLARE(s,n,z) extern unsigned char s[n][z]
#define K_KERNEL_STACK_BUFFER(s) (s)
#define K_KERNEL_STACK_SIZEOF(s) sizeof(s)
enum {K_ERR_ARM_MEM_GENERIC=16,K_ERR_ARM_MEM_STACKING,K_ERR_ARM_MEM_UNSTACKING,
 K_ERR_ARM_MEM_DATA_ACCESS,K_ERR_ARM_MEM_INSTRUCTION_ACCESS,K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION,
 K_ERR_ARM_BUS_GENERIC,K_ERR_ARM_BUS_STACKING,K_ERR_ARM_BUS_UNSTACKING,K_ERR_ARM_BUS_PRECISE_DATA_BUS,
 K_ERR_ARM_BUS_IMPRECISE_DATA_BUS,K_ERR_ARM_BUS_INSTRUCTION_BUS,K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION,
 K_ERR_ARM_USAGE_GENERIC,K_ERR_ARM_USAGE_DIV_0,K_ERR_ARM_USAGE_UNALIGNED_ACCESS,K_ERR_ARM_USAGE_STACK_OVERFLOW,
 K_ERR_ARM_USAGE_NO_COPROCESSOR,K_ERR_ARM_USAGE_ILLEGAL_EXC_RETURN,K_ERR_ARM_USAGE_ILLEGAL_EPSR,
 K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION};
#endif
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
