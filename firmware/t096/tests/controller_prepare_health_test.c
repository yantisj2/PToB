/* Copyright (c) 2026 Jonathan Yantis. MIT. Actual health/schema3 copy/CRC. */
#include "health_fake.h"
#include "../src/health.c"
#include "../src/controller_diag.c"
#include <assert.h>
#include <stdlib.h>
union r2_diag_scratch r2_diag_scratch;
static int phase;
int r2_diag_state(void){return phase;}
bool r2_controller_diag_ready_locked(void){assert(irq_depth);return phase>=1;}
static int r2_console_note(const char *s,size_t n,uint64_t e){(void)s;(void)n;(void)e;return 0;}
int main(int argc,char **argv){
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);uint32_t produced[23];assert(fread(produced,4,23,f)==23&&!fclose(f));
 memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);union r2_diag_scratch saved=r2_diag_scratch;
 r2_health_init(UINT64_C(0x1122334455667788));r2_health_operation(R2_HEALTH_PA,0xc1590005,(int32_t)0x80000000u);
 if(!setjmp(fatal_exit))k_sys_fatal_error_handler(3,NULL);irq_depth=0;
 assert(r2_fault_capsule[1]==3&&r2_fault_capsule[62]==(R2_CONTROLLER_DIAG_UNKNOWN|R2_CONTROLLER_DIAG_FAILURE_ONLY));
 assert(!r2_fault_capsule[46]&&!memcmp(&saved,&r2_diag_scratch,sizeof saved));
 phase=1;r2_controller_diag_init();memcpy(r2_diag_scratch.controller.words,produced,sizeof produced);
 /* Execute the actual lora_init prefix in both phase cases. */
 for(int p=-1;p<=1;p+=2){phase=p;
 #include "lora_scratch_init.inc"
 }
 assert(!memcmp(r2_diag_scratch.controller.words,produced,sizeof produced));
 if(!setjmp(fatal_exit))k_sys_fatal_error_handler(3,NULL);irq_depth=0;
 assert(r2_fault_capsule[1]==3&&r2_fault_capsule[21]==0xc1590005&&r2_fault_capsule[22]==0x80000000);
 assert(!memcmp((const void *)&r2_fault_capsule[40],produced,sizeof produced)&&r2_fault_capsule[63]==crc(r2_fault_capsule));
 f=fopen(argv[2],"wb");assert(f&&fwrite((const void *)r2_fault_capsule,4,64,f)==64&&!fclose(f));
 r2_health_init(9);assert(previous[1]==3&&previous[46]==8&&previous[21]==0xc1590005);
 puts("PASS actual schema3 fatal-copy/CRC and PA C1, scanner UNKNOWN, source/boot retained, actual LoRa prefix preserves observer");
}
