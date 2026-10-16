#include "durability.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_hold_bound b={.reference_ns=100,.transfer_ns=50,.quantization_ns=1,.rate_ppb=100,.provenance=R2_BOUND_FIXTURE};
 assert(r2_hold_error(&b,0,0)==151);
 assert(r2_hold_error(&b,1,0)==152);
 assert(r2_hold_error(&b,1000000,0)==251);
 assert(r2_hold_error(&b,UINT64_MAX,0)==UINT32_MAX);
 assert(r2_hold_error(&b,1,UINT32_MAX)==UINT32_MAX);
 assert(r2_hold_valid(&b,8490000,0,20000000,0,1000));
 assert(!r2_hold_valid(&b,8490001,0,20000000,0,1000));
 assert(!r2_hold_valid(&b,20000000,0,20000000,0,1000));
 b.provenance=R2_BOUND_UNKNOWN;assert(!r2_hold_valid(&b,1,0,20000000,0,1000));
 b.provenance=R2_BOUND_FIXTURE;b.wander_ppb_per_s=10;
 assert(r2_hold_error(&b,2000000,0)==391);
 struct r2_recovery r={0};r2_recovery_update(&r,false,100);
 uint64_t gaps[]={249999,250000,250001,1499999,1500000,1500001,1999999,2000000,2000001,4999999,5000000,5000001,9999999,10000000,10000001};
 for(unsigned i=0;i<sizeof gaps/sizeof gaps[0];i++){
  r2_recovery_update(&r,false,100+gaps[i]);assert(r.lost_at==100);
  assert(r2_recovery_retained(&r,false,100+gaps[i])==(gaps[i]<5000000));
  assert(r2_recovery_retained(&r,true,100+gaps[i]));
 }
 r2_recovery_update(&r,true,10000101);assert(!r.losing);
 r2_recovery_update(&r,false,10000102);assert(r.lost_at==10000102);
 puts("V14 conservative error, exact expiry, overflow, unknown bound and independent recovery boundaries PASS");
}
