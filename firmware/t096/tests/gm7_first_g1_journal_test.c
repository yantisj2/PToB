/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual successful first-G1 bound-consumption seam, synthetic RF endpoints. */
#define R2_TIMING_EVENTS_TEST 1
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
static unsigned first_g1_records;
static uint32_t first_address,first_end;
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b){
 if(detail==R2_GM7_JOURNAL(R2_GM7_J_FIRST_G1,0)){
  assert(kind==R2_TIMING_GM_CONTROL&&!result&&peer==identity);
  ++first_g1_records;first_address=a;first_end=b;
 }
}
int main(void){
 prospective_blackout_at(68783100,false);
 assert(first_g1_records==1&&first_address==(uint32_t)((UINT64_C(68812500)-498)*16+623)&&
     first_end==(uint32_t)(UINT64_C(68812500)*16));
 puts("PASS one first-G1 journal only for actual observed bounded completion; raw ADDRESS and derived END exact, late/missing firstG1 emits no success");
 return 0;
}
