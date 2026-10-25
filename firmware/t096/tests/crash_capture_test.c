/* MIT. Copyright(c)2026 Jonathan Yantis. Actual producer/copy, fake ARM entry.
 * Host pointers and numeric ARM SRAM bounds are tested separately, no low map. */
#define HEALTH_FAKE_REGISTERS 1
#define CONFIG_EXTRA_EXCEPTION_INFO 1
#define R2_GM7_NATIVE 1
#define R2_CONTROLLER_FAULT_CAPTURE 1
#include "health_fake.h"
#include "../src/health.c"
#include <assert.h>
_Alignas(8) unsigned char z_interrupt_stacks[1][CONFIG_ISR_STACK_SIZE];
static int r2_console_note(const char *s,size_t n,uint64_t e){return 0;}
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS]){memset(out,0,R2_CONTROLLER_DIAG_WORDS*4);}
static void capture(unsigned reason,const struct arch_esf *e)
{if(!setjmp(fatal_exit))k_sys_fatal_error_handler(reason,e);irq_depth=0;assert(r2_fault_capsule[63]==crc(r2_fault_capsule));}
static void fresh(void){r2_crash_record[127]=0;crash_handed_off=false;}
int main(int argc,char **argv)
{
 assert(argc==2);r2_health_init(UINT64_C(0x1122334455667788));fake_fault_vector=5;
 uintptr_t entry=(uintptr_t)z_interrupt_stacks[0]+CONFIG_ISR_STACK_SIZE;_callee_saved_t *c=(_callee_saved_t *)(entry-48);
 *c=(_callee_saved_t){4,5,6,7,8,9,10,11,0x20008100};
 struct arch_esf e={.basic={0,1,2,3,12,0x64641,0xa2740,0x21000000},.extra_info={c,entry,0xfffffffd}};
 capture(25,&e);assert(r2_crash_valid(r2_crash_record,true));assert(r2_crash_record[34]==4&&r2_crash_record[35]==5&&r2_crash_record[42]==c->psp);
 /* Host64-bit symbol addresses are deliberately not invented32-bit SRAM.
  * Actual caller takes UNKNOWN; native literal allocation guards are checked
  * separately against ELF. Exercise real numeric bounds and fixed copy loops. */
 assert(!r2_crash_record[52]&&!r2_crash_record[54]&&(r2_crash_record[25]&R2_CRASH_UNKNOWN_THREAD));
 assert(r2_crash_window(0x20008040,8192,0x20008100,192)==192);
 assert(r2_crash_window(0x20008040,8192,0x2000a03c,192)==4);
 uintptr_t badsp[]={0,1,0x2000803c,0x20008041,0x2000a040,0x2003ffff,UINTPTR_MAX};
 for(unsigned i=0;i<sizeof badsp/sizeof badsp[0];i++)assert(!r2_crash_window(0x20008040,8192,badsp[i],192));
 assert(!r2_crash_window(0x20002000,SIZE_MAX,0x20002000,192));assert(!r2_crash_window(0x1fffffff,4096,0x20000000,192));
 uint32_t thread_words[48],isr_words[16];for(unsigned i=0;i<48;i++)thread_words[i]=0xa0000000+i;for(unsigned i=0;i<16;i++)isr_words[i]=0xb0000000+i;
 crash_words(62,(uintptr_t)thread_words,192);crash_words(110,(uintptr_t)isr_words,64);
 assert(!memcmp((const void *)&r2_crash_record[62],thread_words,192)&&!memcmp((const void *)&r2_crash_record[110],isr_words,64));
 /* Synthetic ARM addresses pair with actual copy results for the offline
  * journal/decoder boundary only; not an actual native stack association. */
 r2_crash_record[43]=0x20030400;r2_crash_record[51]=c->psp;r2_crash_record[52]=192;r2_crash_record[53]=0x20030400;r2_crash_record[54]=64;r2_crash_record[55]=2;
 r2_crash_record[25]=(r2_crash_record[25]&~R2_CRASH_UNKNOWN_THREAD)|R2_CRASH_THREAD|R2_CRASH_ISR|R2_CRASH_THREAD_TRUNCATED|R2_CRASH_ISR_TRUNCATED;r2_crash_record[126]=r2_crash_crc(r2_crash_record);assert(r2_crash_valid(r2_crash_record,true));
 FILE *f=fopen(argv[1],"wb");assert(f&&fwrite((const void *)r2_crash_record,4,128,f)==128&&!fclose(f));
 uint32_t id=r2_crash_record[126];uint8_t bytes[512];for(unsigned i=0;i<8;i++)assert(!r2_health_crash_read(i,bytes+64*i));assert(!memcmp(bytes,(const void *)r2_crash_record,512)&&r2_health_crash_pending());assert(r2_health_crash_read(8,bytes)==-EINVAL);
 e.basic.pc=0x1234;capture(25,&e);assert(r2_crash_record[126]==id&&r2_fault_capsule[5]==0x1234);
 r2_health_crash_saved(id^1);assert(r2_health_crash_pending());r2_health_crash_saved(id);assert(!r2_health_crash_pending());capture(25,&e);assert(r2_crash_record[32]==0x1234&&r2_crash_record[126]!=id);
 fresh();capture(23,&e);assert(r2_crash_record[25]&R2_CRASH_STACK_UNRELIABLE);assert(!r2_crash_record[52]&&!r2_crash_record[54]);
 fresh();fake_fault_vector=11;e.extra_info.callee=(_callee_saved_t *)1;e.extra_info.msp=1;e.extra_info.exc_return=0xfffffffd;capture(0,&e);assert(r2_crash_valid(r2_crash_record,true)&&!(r2_crash_record[25]&R2_CRASH_RETURN));
 fresh();fake_fault_vector=3;e.extra_info.callee=c;e.extra_info.msp=entry;e.extra_info.exc_return=0xfffffff1;capture(0,&e);assert(r2_crash_valid(r2_crash_record,true)&&(r2_crash_record[25]&R2_CRASH_CALLEE));
 for(unsigned i=0;i<128;i++){uint32_t w[128];memcpy(w,(const void *)r2_crash_record,512);w[i]^=1;assert(!r2_crash_valid(w,true));}
 fresh();capture(25,NULL);assert(r2_crash_valid(r2_crash_record,true)&&!(r2_crash_record[25]&R2_CRASH_BASIC));
 puts("PASS actual fatalregister+copy bodies; independent numericARM192/64 bounds; pending handoff,CRC/commit negatives, softwarefatal andHardFault");
}
