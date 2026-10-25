/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual fatal producer, fake ARM entry. */
#define HEALTH_FAKE_REGISTERS 1
#define CONFIG_EXTRA_EXCEPTION_INFO 1
#define R2_GM7_NATIVE 1
#define R2_CONTROLLER_FAULT_CAPTURE 1
#include "health_fake.h"
#include "../src/health.c"
#include <assert.h>
_Alignas(8) unsigned char z_interrupt_stacks[1][CONFIG_ISR_STACK_SIZE];
static int r2_console_note(const char *s,size_t n,uint64_t expiry){return 0;}
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS])
{for(unsigned i=0;i<R2_CONTROLLER_DIAG_WORDS;i++)out[i]=0xc0000000u+i;}
static void capture(unsigned reason,const struct arch_esf *esf)
{
 if(!setjmp(fatal_exit))k_sys_fatal_error_handler(reason,esf);
 irq_depth=0;
 assert(r2_fault_capsule[0]==FAULT_MAGIC&&r2_fault_capsule[63]==crc(r2_fault_capsule));
}
int main(int argc,char **argv)
{
 assert(argc==2&&sizeof r2_fault_capsule==256&&sizeof previous==256);
 r2_health_init(UINT64_C(0x1122334455667788));
 uintptr_t start=(uintptr_t)z_interrupt_stacks[0],top=start+CONFIG_ISR_STACK_SIZE;
 _callee_saved_t *c=(_callee_saved_t *)(top-48);
 *c=(_callee_saved_t){0x44444444,0x55555555,0x66666666,0x77777777,
                       0x88888888,0x99999999,0xaaaaaaaa,0xbbbbbbbb,0x20031000};
 struct arch_esf esf={.basic={0,0x11111111,0x22222222,0x33333333,
      0xcccccccc,0xeeeeeeee,0x64640,0x41000000},.extra_info={c,top,0xfffffffd}};
 fake_fault_vector=5;capture(K_ERR_ARM_BUS_PRECISE_DATA_BUS,&esf);
 assert(r2_fault_capsule[1]==4&&r2_fault_capsule[40]==23);
 assert(r2_fault_capsule[41]==0&&r2_fault_capsule[42]==0x11111111&&
        r2_fault_capsule[45]==0xcccccccc&&r2_fault_capsule[47]==0x55555555&&
        r2_fault_capsule[54]==0x20031000&&r2_fault_capsule[56]==0xfffffffd);
 FILE *f=fopen(argv[1],"wb");assert(f&&fwrite((const void *)r2_fault_capsule,4,64,f)==64&&!fclose(f));
 r2_health_init(9);assert(previous[1]==4&&previous[47]==0x55555555&&!r2_fault_capsule[0]);
 /* Each unavailable pointer is rejected numerically before any access. */
 uintptr_t bad[]={0,1,start-4,top-47,top-44,top-36,UINTPTR_MAX-3};
 for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
  esf.extra_info.callee=(_callee_saved_t *)bad[i];capture(25,&esf);
  assert(r2_fault_capsule[1]==4&&r2_fault_capsule[40]==21);
  for(unsigned j=46;j<=54;j++)assert(!r2_fault_capsule[j]);
 }
 esf.extra_info.callee=c;
 uint32_t invalid[]={0,1,0xfffffff0,0xffffffe1,0xfffffff5,UINT32_MAX};
 for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++){
  esf.extra_info.exc_return=invalid[i];capture(25,&esf);
  assert(r2_fault_capsule[40]==17);
  for(unsigned j=46;j<=56;j++)assert(!r2_fault_capsule[j]);
 }
 esf.extra_info.exc_return=0xfffffffd;
 esf.extra_info.msp=top-4;capture(25,&esf);assert(r2_fault_capsule[40]==21);
 esf.extra_info.msp=top;
 assert(callee_window(start,start+48,start,CONFIG_ISR_STACK_SIZE));
 assert(callee_window(top-48,top,start,CONFIG_ISR_STACK_SIZE));
 assert(!callee_window(top-44,top+4,start,CONFIG_ISR_STACK_SIZE));
 assert(!callee_window(UINTPTR_MAX-47,0,UINTPTR_MAX-47,48));
 for(unsigned vector=3;vector<=6;vector++)for(unsigned reason=0;reason<=40;reason++){
  fake_fault_vector=vector;capture(reason,&esf);
  bool expected=(vector==4&&reason>=16&&reason<=21)||(vector==5&&reason>=22&&reason<=28)||
                (vector==6&&reason>=29&&reason<=36);
  assert((r2_fault_capsule[1]==4)==expected);
  if(expected){bool unreliable=reason==17||reason==18||reason==23||reason==24||reason==32;
   assert(!!(r2_fault_capsule[40]&8)==unreliable);
  }else for(unsigned i=0;i<23;i++)assert(r2_fault_capsule[40+i]==0xc0000000u+i);
 }
 fake_fault_vector=5;capture(25,NULL);assert(r2_fault_capsule[1]==4&&r2_fault_capsule[40]==16);
 for(unsigned i=41;i<=56;i++)assert(!r2_fault_capsule[i]);
 /* Accept every retained schema without turning diagnostic words into role state. */
 for(unsigned schema=2;schema<=4;schema++){
  r2_fault_capsule[0]=FAULT_MAGIC;r2_fault_capsule[1]=schema;r2_fault_capsule[63]=crc(r2_fault_capsule);
  r2_health_init(schema);assert(previous[1]==schema);
 }
 for(unsigned schema=1;schema<=5;schema+=4){
  memset(previous,0,sizeof previous);r2_fault_capsule[0]=FAULT_MAGIC;
  r2_fault_capsule[1]=schema;r2_fault_capsule[63]=crc(r2_fault_capsule);
  r2_health_init(99);assert(!previous[0]);
 }
 capture(25,&esf);r2_fault_capsule[47]^=1;memset(previous,0,sizeof previous);
 r2_health_init(99);assert(!previous[0]);
 puts("PASS actual format4 all registers/provenance/negative ranges/CRC/legacy2+3/controller3; fake fault boundaries only");
}
