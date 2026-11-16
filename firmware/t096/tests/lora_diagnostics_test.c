/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual passive owner, JSON, frame and main reply with synthetic boundaries. */
#include "lora_ram_fixture.h"
#include "core.h"
static uint32_t reply_sequence,reply_count;
static unsigned compact_max,full_max;
#define R2_SOURCE_SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
static struct {uint64_t a;union {char json[R2_REPLY_BYTES-96];char frame[R2_REPLY_BYTES];} data;uint64_t b;} guard;
#define json guard.data.json
#define frame guard.data.frame
static char saved[R2_REPLY_BYTES];
static int r2_console_reply(const char *p,size_t n,uint64_t expiry){
 assert(!lock_depth&&expiry>now_us/1000&&n>0&&n<R2_REPLY_BYTES);
 memcpy(saved,p,n);saved[n]=0;++reply_count;
 unsigned req,seq,len,crc;int h=0;
 assert(sscanf(saved,"R2 %x %x %u %x\n%n",&req,&seq,&len,&crc,&h)==4);
 assert(req==123&&seq==reply_sequence&&len<2976&&len+strlen(saved+h+len)+(unsigned)h==n);
 assert(r2_crc32(saved+h,len)==crc&&saved[h+len]=='\n');
 return 0;
}
#include R2_LORA_REPLY_BODY
#undef json
#undef frame
static void sentinel(void){assert(guard.a==UINT64_C(0x1234567890abcdef)&&guard.b==UINT64_C(0xfedcba0987654321));}
static void poll(void){
 struct r2_request r={.id=123,.op=R2_LORA_DIAGNOSTICS};
 unsigned old=reply_count;lora_reply(&r,now_us/1000,0);assert(reply_count==old+1);sentinel();
 unsigned n=(unsigned)strlen(saved);if(n>compact_max)compact_max=n;
 r.op=R2_LORA_CONTROL;lora_reply(&r,now_us/1000,0);assert(reply_count==old+2);sentinel();
 n=(unsigned)strlen(saved);if(n>full_max)full_max=n;
}
static void interleave_mode(void){
 assert(!r2_lora_mode_request(!transmitter));
 for(unsigned i=0;i<4&&atomic_get(&mode_request);i++)step();
 assert(!atomic_get(&mode_request));
}
static void query_does_not_change(void){
 unsigned char before[sizeof role];memcpy(before,&role,sizeof role);
 union r2_diag_scratch scratch=r2_diag_scratch;
 struct lp_diagnostics d=diagnostics;
 struct r2_lora_view p=published;
 poll();
 assert(!memcmp(before,&role,sizeof role)&&!memcmp(&scratch,&r2_diag_scratch,sizeof scratch));
 assert(!memcmp(&d,&diagnostics,sizeof d)&&!memcmp(&p,&published,sizeof p));
}
int main(void){
 guard.a=UINT64_C(0x1234567890abcdef);guard.b=UINT64_C(0xfedcba0987654321);
 init(false);step();
 for(unsigned cycle=0;cycle<400;cycle++){
  query_does_not_change();
  assert(!r2_lora_mode_request(!transmitter));
  poll(); /* pending closure/role request */
  for(unsigned k=0;k<4&&atomic_get(&mode_request);k++){step();poll();}
  assert(!atomic_get(&mode_request));
  if(transmitter){struct rc8_export m=source();m.scope.emitter=m.scope.root=factory;r2_lora_source_publish(&m);}
  for(unsigned k=0;k<3;k++){step();query_does_not_change();}
  /* Real worker can commit a role between snapshot release and encoding. */
  unlock_hook=interleave_mode;poll();assert(!unlock_hook);
  query_does_not_change();
 }
 char out[4096];int expected=r2_lora_diagnostics_json(out,sizeof out,0);assert(expected>0);
 for(size_t capacity=0;capacity<=(size_t)expected+1;capacity++){
  memset(out,0xa5,sizeof out);int n=r2_lora_diagnostics_json(out,capacity,0);
  assert(n==expected);for(size_t i=capacity;i<sizeof out;i++)assert((unsigned char)out[i]==0xa5);
  if(capacity)assert(out[capacity-1]==0||capacity>(size_t)expected);
 }
 printf("PASS %u actual replies,400 role cycles,post-snapshot role changes,no state mutation;max compact%u/full%u;all compact output capacities0..%d bounded\n",reply_count,compact_max,full_max,expected+1);
 return 0;
}
