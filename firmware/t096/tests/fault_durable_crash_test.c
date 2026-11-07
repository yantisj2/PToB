/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define R2_FAULT_DURABLE 1
#define main historical_retention_crash_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "fault_retention_crash_test.c"
#pragma clang diagnostic pop
#undef main
int main(void)
{
 uint32_t original[128];uint8_t bytes[512];seed(original);
 /* Intact historical-image bytes are preserved; this does not prove physical
  * SRAM/linker address survival when a different image is installed. */
 r2_crash_record[8]^=0x12345678;r2_crash_record[16]^=0x87654321;
 r2_crash_record[126]=r2_crash_crc(r2_crash_record);memcpy(original,(const void *)r2_crash_record,512);
 r2_health_init(2);assert(r2_health_crash_pending()&&!memcmp(original,(const void *)r2_crash_record,512));
 for(unsigned i=0;i<8;i++)assert(!r2_health_crash_read(i,bytes+i*64));
 assert(!memcmp(original,bytes,512));capture(9);assert(!memcmp(original,(const void *)r2_crash_record,512));
 r2_health_crash_saved(original[126]^1);assert(r2_health_crash_pending());
 r2_health_crash_saved(original[126]);assert(!r2_health_crash_pending());
 capture(10);assert(r2_crash_record[4]==2&&r2_crash_record[24]==10&&r2_health_crash_pending());
 for(unsigned i=0;i<128;i++){
  memcpy((void *)r2_crash_record,original,512);r2_crash_record[i]^=1;r2_health_init(3);assert(!r2_health_crash_pending()&&!r2_crash_record[0]);
 }
 for(unsigned i=6;i<8;i++){
  memcpy((void *)r2_crash_record,original,512);r2_crash_record[i]^=1;r2_crash_record[126]=r2_crash_crc(r2_crash_record);
  r2_health_init(3);assert(!r2_health_crash_pending());
 }
 puts("PASS original-image CPU provenance retained only for intact same-factory bytes; first pending and verified ACK; future fault capture; CRC/factory negatives; no SRAM layout-survival claim");
 return 0;
}
