/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual ARM production source/UTC/arithmetic path; QEMU only, no hardware. */
#include "gm7_source.h"
#include "snapshot_witness.h"
#include <stddef.h>
#include <stdint.h>
_Static_assert(sizeof(struct r2_capture)==24&&offsetof(struct r2_capture,epoch)==8,"Native capture aggregate ABI");
_Static_assert(sizeof(struct gm7_source)==64&&sizeof(struct r2_clock)==432&&sizeof(struct r2_source_clock)==440,"Native snapshot copy extents");
volatile uint8_t r2_gm7_witness_state=R2_WITNESS_REQUIRED;
volatile uint32_t r2_crash_record[128];
uint32_t test_returned_regs[8];
int test_snapshot_registers(struct gm7_source *,const struct r2_clock *,
 const struct r2_source_clock *,struct r2_capture,uint64_t);
static void message(const char *s){register uint32_t r0 __asm__("r0")=4;
 register const char *r1 __asm__("r1")=s;__asm__ volatile("bkpt #0xab":"+r"(r0):"r"(r1):"memory");}
__attribute__((noreturn)) static void finish(unsigned code){uint32_t v[2]={0x20026,code};
 register uint32_t r0 __asm__("r0")=0x20;register uint32_t *r1 __asm__("r1")=v;
 __asm__ volatile("bkpt #0xab":"+r"(r0):"r"(r1):"memory");for(;;){}}
#define CHECK(x) do{if(!(x)){message("FAIL ARM snapshot check: " #x "\n");finish(__LINE__);}}while(0)
void *memset(void *p,int value,size_t n){unsigned char *q=p;for(size_t i=0;i<n;i++)q[i]=(unsigned char)value;return p;}
void *memcpy(void *p,const void *src,size_t n){unsigned char *q=p;const unsigned char *s=src;for(size_t i=0;i<n;i++)q[i]=s[i];return p;}
static bool same(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return false;return true;}
void test_fault_c(const uint32_t *frame){
#if TEST_CASE > 0 && TEST_CASE < 5
 const volatile uint32_t *p=r2_crash_record+R2_WITNESS_WORD;
 CHECK(r2_witness_valid(p));CHECK((p[1]&3)==3);
 unsigned expected=TEST_CASE==1?1:TEST_CASE==2?2:TEST_CASE==3?4:3;
 CHECK(((p[1]>>8)&15)==expected);
 extern const char gm7_snapshot_witness_fault[];
 CHECK((frame[6]&~1u)==((uintptr_t)gm7_snapshot_witness_fault&~1u));
 CHECK(p[6]==0x55555555);CHECK(p[14]==(TEST_CASE==1||TEST_CASE==4?1:0x55555555));
 CHECK(p[22]==(TEST_CASE==2||TEST_CASE==4?1:0x55555555));
 message("PASS ARM real veneer detected expected live/saved/LR boundary mismatch before return\n");finish(0);
#else
 (void)frame;message("FAIL unexpected ARM exception\n");finish(250);
#endif
}
static void registers(void){for(unsigned i=0;i<8;i++)CHECK(test_returned_regs[i]==UINT32_C(0x44444444)+i*UINT32_C(0x11111111));}
void test_main(void){
 /* C initialization is deliberately explicit in this freestanding image. */
 r2_gm7_witness_state=R2_WITNESS_REQUIRED;
 struct r2_clock c={0};struct r2_source_clock map={0};struct gm7_source out={0};
 struct r2_capture at={UINT64_C(0x1122334455667788),UINT64_C(0x102030405060708),R2_CAPTURE_NOW};
#if TEST_CASE > 0 && TEST_CASE < 5
 (void)test_snapshot_registers(&out,&c,&map,at,UINT64_C(0x123456789abcdef));
 message("FAIL mismatch returned\n");finish(251);
#elif TEST_CASE == 5
 r2_gm7_witness_state=R2_WITNESS_GM; /* Pending prior crash: no overlay. */
 for(unsigned i=0;i<128;i++)r2_crash_record[i]=0xa5000000+i;
 CHECK(test_snapshot_registers(&out,&c,&map,at,123)==-22);registers();
 for(unsigned i=0;i<128;i++)CHECK(r2_crash_record[i]==0xa5000000+i);
 message("PASS ARM unavailable witness preserves all previous512B and original return\n");finish(0);
#else
 struct {uint64_t before;struct gm7_source source;uint64_t after;} guarded;
 struct r2_clock original;struct r2_source_clock oldmap;
 for(unsigned n=0;n<20000;n++){
  memset(&c,0,sizeof c);r2_source_clock_init(&map);
  struct r2_pps_precision pps={.epoch=4,.last=16000000,.rate_q16=UINT64_C(16000000)*65536,
    .seen=true,.rate_valid=true};
  at=(struct r2_capture){16000000+1234567+(n%7)*321,4,R2_CAPTURE_NOW};
  CHECK(r2_source_clock_publish(&map,&pps,(struct r2_capture){16000010,4,R2_CAPTURE_NOW}));
  CHECK(r2_source_utc_bind(&map,at,INT64_C(1792800000000000000)+77160437,3,0)==1);
  c.last_pps=5000000;c.last_rmc=5000010;c.last_rmc_utc=INT64_C(1792800000000000);
  c.view.source_epoch=3;c.view.valid=c.view.pps_present=c.view.fix_active=true;
  c.translated_epoch=3;c.translated_at_us=5000000;c.translated_utc_us=INT64_C(1792800000000000);
  uint64_t sampled=5100000+n%9999;
  unsigned mode=n%8;
  if(mode==1)c.view.fix_active=false;
  if(mode==2)at.epoch=5;
  if(mode==3)sampled=9000000;
  if(mode==4)map.utc_bound=false;
  if(mode==5)c.last_pps=(int64_t)sampled+1;
  if(mode==6)at.epoch=UINT64_C(0x100000000);
  if(mode==7)sampled=UINT64_MAX;
  original=c;oldmap=map;guarded.before=UINT64_C(0xa5a5a5a55a5a5a5a);guarded.after=~guarded.before;
  memset(&guarded.source,0xcc,sizeof guarded.source);out=guarded.source;
  int rc=test_snapshot_registers(&guarded.source,&c,&map,at,sampled);registers();
  CHECK(guarded.before==UINT64_C(0xa5a5a5a55a5a5a5a)&&guarded.after==UINT64_C(0x5a5a5a5aa5a5a5a5));
  CHECK(same(&c,&original,sizeof c)&&same(&map,&oldmap,sizeof map));
  if(mode>=6){CHECK(rc==-22&&same(&guarded.source,&out,sizeof out));}
  else{
   CHECK(!rc&&guarded.source.sampled_us==sampled&&guarded.source.source_gen==3);
   bool physical=mode==0;
   CHECK(guarded.source.physical_ready==physical);
   if(physical){int64_t utc;CHECK(r2_source_utc_read(&map,at,3,&utc));CHECK(guarded.source.utc_us==(uint64_t)utc/1000);}
   if(mode==1||mode==3||mode==5)CHECK(!guarded.source.gnss_healthy);
   if(mode==2||mode==4)CHECK(guarded.source.utc_available&&!guarded.source.physical_ready);
  }
#ifndef TEST_BASELINE
  CHECK(r2_witness_valid(r2_crash_record+R2_WITNESS_WORD));
  CHECK(r2_crash_record[R2_WITNESS_WORD+1]==(R2_WITNESS_VERSION|3|R2_WITNESS_COMPARED));
  CHECK(r2_crash_record[R2_WITNESS_WORD+30]==(uint32_t)at.epoch);
#endif
 }
 message("PASS ARM production snapshot20000 physical/coarse/noGNSS/expiry/epoch/overflow cases; real source_clock/UTC/libgcc arithmetic, callee-register sentinels and copy canaries\n");finish(0);
#endif
}
