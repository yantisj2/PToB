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
static void fresh(void){r2_crash_record[127]=0;r2_gm7_witness_state=0;}
static void witness(void){
 volatile uint32_t *p=r2_crash_record+R2_WITNESS_WORD;
 for(unsigned i=0;i<32;i++)p[i]=0;
 p[0]=R2_WITNESS_MAGIC;p[1]=R2_WITNESS_VERSION|3|R2_WITNESS_COMPARED;p[2]=9;p[3]=0x20030900;
 p[4]=p[29]=0x12345;p[30]=4;
 for(unsigned i=0;i<8;i++)p[5+i]=p[13+i]=p[21+i]=0x44444444+i*0x11111111;
 p[31]=p[2]^R2_WITNESS_COMMIT;
 r2_gm7_witness_state=R2_WITNESS_REQUIRED|R2_WITNESS_STARTED;
}
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
 /* Exact overlay lifecycle: durable acknowledgment, first entry, partial
  *stage, reboot provenance and normalRC disarm use actual producer/getter. */
 fresh();witness();capture(25,&e);assert(r2_crash_valid(r2_crash_record,true)&&r2_crash_record[1]==8&&r2_crash_record[61]==R2_CRASH_WITNESS_TAG);
 uint32_t saved[128];memcpy(saved,(const void *)r2_crash_record,512);
 r2_health_gm_witness(true);struct r2_snapshot_witness_view view;r2_health_gm_witness_view(&view);
 assert(view.enabled&&view.prior_crash_blocked&&!view.armed&&!view.current&&view.pending_crc==saved[126]);
 r2_health_crash_saved(saved[126]^1);assert(!memcmp(saved,(const void *)r2_crash_record,512));
 r2_health_crash_saved(saved[126]);r2_health_gm_witness_view(&view);
 assert(view.armed&&!view.prior_crash_blocked&&!view.current&&!memcmp(saved,(const void *)r2_crash_record,512));
 char path[1024];assert(snprintf(path,sizeof path,"%s.witness",argv[1])>0);
 f=fopen(path,"wb");assert(f&&fwrite(saved,4,128,f)==128&&!fclose(f));
 /* A CRC-valid capsule remains exportable even when its witness is corrupt.
  * This is crash evidence only, never a source/control authority. */
 r2_crash_record[R2_WITNESS_WORD]=0;r2_crash_record[126]=r2_crash_crc(r2_crash_record);
 assert(r2_crash_valid(r2_crash_record,true));uint8_t preserved[64];assert(!r2_health_crash_read(0,preserved));
 /* Complete entry + partially written post-inner view is never a good return. */
 fresh();witness();r2_crash_record[R2_WITNESS_WORD+1]=R2_WITNESS_VERSION|0x21;
 r2_crash_record[R2_WITNESS_WORD+13]=1;
 capture(25,&e);assert(r2_crash_valid(r2_crash_record,true)&&r2_crash_record[1]==8);
 assert(snprintf(path,sizeof path,"%s.partial",argv[1])>0);f=fopen(path,"wb");assert(f&&fwrite((const void *)r2_crash_record,4,128,f)==128&&!fclose(f));
 r2_health_init(UINT64_C(0xaabbccddeeff0011));r2_health_gm_witness(true);r2_health_gm_witness_view(&view);
 assert(view.boot==UINT64_C(0xaabbccddeeff0011)&&view.prior_crash_blocked&&!view.current);
 r2_health_crash_saved(r2_crash_record[126]);r2_health_gm_witness_view(&view);assert(view.armed&&!view.current);
 /* All three views may finish before the comparison does. Differing views
  *with no committed mismatch flags remain valuable, readable evidence. */
 fresh();witness();r2_crash_record[R2_WITNESS_WORD+1]=R2_WITNESS_VERSION|3;
 r2_crash_record[R2_WITNESS_WORD+14]=1;
 capture(25,&e);assert(r2_crash_valid(r2_crash_record,true)&&r2_crash_record[1]==8);
 assert(snprintf(path,sizeof path,"%s.unchecked",argv[1])>0);f=fopen(path,"wb");assert(f&&fwrite((const void *)r2_crash_record,4,128,f)==128&&!fclose(f));
 fresh();witness();r2_health_gm_witness(false);capture(25,&e);
 assert(r2_crash_record[1]==7&&r2_crash_record[61]==0&&r2_crash_valid(r2_crash_record,true));
 fresh();r2_health_init(99);r2_health_gm_witness(true);r2_health_gm_witness_view(&view);
 assert(view.armed&&!view.current&&!view.sequence);
 puts("PASS actual fatalregister+copy bodies; independent numericARM192/64 bounds; pending handoff,CRC/commit negatives, softwarefatal andHardFault");
 puts("PASS schema8 witness/partial preservation, exact durableack, pendingoldrecord intact, reboot notcurrent, normalRC schema7 and read-only witness lifecycle");
}
