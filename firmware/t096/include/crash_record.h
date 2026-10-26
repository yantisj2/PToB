/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_CRASH_RECORD_H
#define R2_CRASH_RECORD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "snapshot_witness.h"
/* Schema7 is ONE existing512B bank0 slot, not a multi-slot dump. All scalars
 * little-endian. Binary SHA256/factory avoid duplicating ASCII headers.
 * 0..61 header;62..109 thread192B;110..125 ISR64B;126 CRC;127 commit.
 * Header:0magic,1schema,2slot,3bytes,4/5crashedboot,6/7factory,
 * 8..15sourceSHA256,16..23exactwatchdogbuildbinding,24reason,25flags,
 * 26..33r0-r3/r12/LR/PC/xPSR,34..41r4-r11,42PSP,43entryMSP,
 * 44EXC_RETURN,45TCB-value-only,46vector,47..50post-clearSCB,
 * 51/52threadaddress/bytes,53/54ISRaddress/bytes,55allocation-id,
 * 56/57journalgeneration,58originalretainedCRC,59entrymask,60CONTROL,61reserved.
 * Retained slot=UINT32_MAX,generation=0,id=0. Flash owner fills ONLY those
 * fields and its whole-recordCRC. CRC is integrity, not authentication.
 */
#define R2_CRASH_WORDS 128u
#define R2_CRASH_BYTES 512u
#define R2_CRASH_SCHEMA 7u
#define R2_CRASH_WITNESS_SCHEMA 8u
#define R2_CRASH_WITNESS_TAG UINT32_C(0x47574d31)
#define R2_CRASH_MAGIC UINT32_C(0x44325231)
#define R2_CRASH_COMMIT UINT32_C(0x434f4d31)
#define R2_CRASH_THREAD_WORD 62u
#define R2_CRASH_THREAD_BYTES 192u
#define R2_CRASH_SHORT_THREAD_WORD 94u
#define R2_CRASH_SHORT_THREAD_BYTES 64u
#define R2_CRASH_ISR_WORD 110u
#define R2_CRASH_ISR_BYTES 64u
#define R2_CRASH_DIAG_CHUNK 256u
#define R2_CRASH_DIAG_CHUNKS 8u
_Static_assert(R2_CRASH_THREAD_WORD+R2_CRASH_THREAD_BYTES/4==R2_CRASH_ISR_WORD,"Thread window offset");
_Static_assert(R2_CRASH_ISR_WORD+R2_CRASH_ISR_BYTES/4==126,"CRC follows both bounded windows");
_Static_assert(R2_CRASH_WORDS*sizeof(uint32_t)==R2_CRASH_BYTES,"Exactly one journal slot");
_Static_assert(R2_WITNESS_WORD+R2_WITNESS_WORDS==R2_CRASH_SHORT_THREAD_WORD,"Witness then short thread window");
_Static_assert(R2_CRASH_SHORT_THREAD_WORD+R2_CRASH_SHORT_THREAD_BYTES/4==R2_CRASH_ISR_WORD,"Short thread then unchanged ISR window");
enum { R2_CRASH_BASIC=1,R2_CRASH_CALLEE=2,R2_CRASH_RETURN=4,
 R2_CRASH_STACK_UNRELIABLE=8,R2_CRASH_SCB_POST_CLEAR=16,
 R2_CRASH_THREAD=32,R2_CRASH_ISR=64,R2_CRASH_UNKNOWN_THREAD=128,
 R2_CRASH_THREAD_TRUNCATED=256,R2_CRASH_ISR_TRUNCATED=512 };
static inline uint32_t r2_crash_crc(const volatile uint32_t *p)
{
 uint32_t c=UINT32_MAX;
 for(unsigned i=0;i<126;i++)for(unsigned j=0;j<4;j++){
  c^=(p[i]>>(8*j))&255u;
  for(unsigned b=0;b<8;b++)c=(c>>1)^(UINT32_C(0xedb88320)&-(c&1u));
 }
 return ~c;
}
static inline unsigned r2_crash_nibble(char c)
{return c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):256u;}
static inline uint32_t r2_crash_hex32(const char *s)
{uint32_t n=0;for(unsigned i=0;i<8;i++)n=(n<<4)|r2_crash_nibble(s[i]);return n;}
static inline bool r2_crash_silicon(const volatile uint32_t *p,const char *silicon)
{return p[6]==r2_crash_hex32(silicon+8)&&p[7]==r2_crash_hex32(silicon);}
static inline bool r2_crash_provenance(const volatile uint32_t *p)
{
 uint32_t f=p[25];
 if(!(f&R2_CRASH_SCB_POST_CLEAR)||f>1023)return false;
 if((f&R2_CRASH_CALLEE)&&(f&7)!=7)return false;
 if(f&R2_CRASH_RETURN){if(!(f&R2_CRASH_BASIC)||(p[44]!=0xfffffff1u&&p[44]!=0xfffffff9u&&p[44]!=0xfffffffdu))return false;}
 else if(p[42]||p[43]||p[44])return false;
 if(!(f&R2_CRASH_CALLEE))for(unsigned i=34;i<=42;i++)if(p[i])return false;
 if(f&R2_CRASH_THREAD){if((f&7)!=7||(f&(R2_CRASH_STACK_UNRELIABLE|R2_CRASH_UNKNOWN_THREAD))||!p[52]||p[51]!=p[42]||!p[55]||p[55]>12)return false;}
 else if(p[51]||p[52]||p[55])return false;
 if(f&R2_CRASH_ISR){if(!(f&R2_CRASH_RETURN)||(f&R2_CRASH_STACK_UNRELIABLE)||!p[54]||p[53]!=p[43])return false;}
 else if(p[53]||p[54])return false;
 if((f&R2_CRASH_THREAD_TRUNCATED)&&!(f&R2_CRASH_THREAD))return false;
 if((f&R2_CRASH_ISR_TRUNCATED)&&!(f&R2_CRASH_ISR))return false;
 return true;
}
static inline bool r2_crash_valid(const volatile uint32_t *p,bool retained)
{
 bool witness=p[1]==R2_CRASH_WITNESS_SCHEMA;
 return p[0]==R2_CRASH_MAGIC&&(witness||p[1]==R2_CRASH_SCHEMA)&&p[3]==R2_CRASH_BYTES&&
  (p[4]||p[5])&&(p[6]||p[7])&&r2_crash_provenance(p)&&
  /* Base crash evidence survives inconsistent witness semantics. The producer
   * selects schema8 only from a valid live witness; journal/export preserve
   * an already committed CRC-valid capsule without granting any authority. */
  (witness?p[61]==R2_CRASH_WITNESS_TAG:!p[61])&&
  p[52]<=(witness?R2_CRASH_SHORT_THREAD_BYTES:R2_CRASH_THREAD_BYTES)&&!(p[52]&3)&&p[54]<=R2_CRASH_ISR_BYTES&&!(p[54]&3)&&
  ((!p[52])||((p[25]&R2_CRASH_THREAD)&&!(p[51]&3)&&p[51]>=0x20002000u&&p[51]<=0x20040000u-p[52]))&&
  ((!p[54])||((p[25]&R2_CRASH_ISR)&&!(p[53]&3)&&p[53]>=0x20002000u&&p[53]<=0x20040000u-p[54]))&&
  (retained?(p[2]==UINT32_MAX&&!p[56]&&!p[57]&&!p[58]):(p[2]<16&&(p[56]||p[57])))&&
  p[126]==r2_crash_crc(p)&&p[127]==R2_CRASH_COMMIT;
}
/* Numeric known-allocation bound before ANY source access; no TCB dereference. */
static inline size_t r2_crash_window(uintptr_t start,size_t size,uintptr_t sp,size_t cap)
{
 if((sp&3)||start<0x20002000u||start>0x20040000u||size>0x20040000u-start||sp<start||sp-start>=size)return 0;
 size_t n=size-(sp-start);if(n>cap)n=cap;return n&~(size_t)3;
}
#endif
