/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define HEALTH_FAKE_REGISTERS 1
#define CONFIG_EXTRA_EXCEPTION_INFO 1
#define R2_GM7_NATIVE 1
#include "health_fake.h"
#include "../src/health.c"
#include <assert.h>
_Alignas(8) unsigned char z_interrupt_stacks[1][CONFIG_ISR_STACK_SIZE];
static int r2_console_note(const char *s,size_t n,uint64_t e){return 0;}
static void capture(unsigned reason)
{if(!setjmp(fatal_exit))k_sys_fatal_error_handler(reason,NULL);irq_depth=0;}
static void seed(uint32_t out[128])
{
 memset((void *)r2_crash_record,0,sizeof r2_crash_record);r2_health_init(1);capture(4);
 assert(crash_bound()&&r2_health_crash_pending());memcpy(out,(const void *)r2_crash_record,512);
}
int main(void)
{
 uint32_t original[128];uint8_t bytes[512];
 memset((void *)r2_crash_record,0xa5,sizeof r2_crash_record);r2_health_init(1);
 assert(!r2_health_crash_pending()&&r2_health_crash_read(0,bytes)==-ENOENT);
 assert(!r2_crash_record[0]&&!r2_fault_capsule[0]); /* WD without CPU exception creates no dump. */
 seed(original);r2_health_init(2);
 assert(r2_health_crash_pending()&&!memcmp(original,(const void *)r2_crash_record,512));
 assert(boot_id==2&&!r2_fault_capsule[0]&&!previous[0]);
 for(unsigned i=0;i<8;i++)assert(!r2_health_crash_read(i,bytes+64*i));
 assert(!memcmp(bytes,original,512)&&r2_health_crash_read(8,bytes)==-EINVAL);
 r2_health_gm_witness(true);struct r2_snapshot_witness_view view;r2_health_gm_witness_view(&view);
 assert(view.prior_crash_blocked&&!view.armed&&!view.current&&view.boot==2);
 capture(9);assert(!memcmp(original,(const void *)r2_crash_record,512));
 assert(r2_fault_capsule[4]==9&&r2_fault_capsule[2]==2); /* current-boot fault256 remains independent */
 r2_health_init(3);capture(10);assert(!memcmp(original,(const void *)r2_crash_record,512));
 /* Every torn/corrupted word fails actual boot validation. */
 for(unsigned i=0;i<128;i++){
  memcpy((void *)r2_crash_record,original,512);r2_crash_record[i]^=1;r2_health_init(4);
  assert(!r2_health_crash_pending()&&!r2_crash_record[0]&&r2_health_crash_read(0,bytes)==-ENOENT);
 }
 /* Recomputed CRC cannot admit a foreign factory, source or private build. */
 for(unsigned i=6;i<24;i++){
  memcpy((void *)r2_crash_record,original,512);r2_crash_record[i]^=1;r2_crash_record[126]=r2_crash_crc(r2_crash_record);
  assert(r2_crash_valid(r2_crash_record,true));r2_health_init(4);assert(!r2_health_crash_pending()&&!r2_crash_record[0]);
 }
 memcpy((void *)r2_crash_record,original,512);r2_health_init(1);assert(!r2_health_crash_pending());
 seed(original);r2_health_init(2);uint32_t crc_before=r2_crash_record[126];
 for(unsigned pass=0;pass<3;pass++)for(unsigned i=0;i<8;i++)assert(!r2_health_crash_read(i,bytes));
 assert(r2_health_crash_pending()&&crc_before==r2_crash_record[126]);
 puts("PASS actual CPU capture/boot binding, first pending preservation through resets/faults, read-only chunks, torn/foreign records and absent CPU-fault evidence");
}
