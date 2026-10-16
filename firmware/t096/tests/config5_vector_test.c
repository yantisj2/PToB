#include "authority_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 const char *hex="0c0a02032222000000000000222200000000000004000000000000000500000000000000060000000000000002333300000000000009000000a0bb0d0000000000f07e0e000000000040420f0000000000d01213000000000080841e0000000000050000000180f8fe80ffff60ea000060ea000001021111000000000000222200000000000007000000c0d8a70000000000f408d0967c69cc46";
 uint8_t wire[R2_AUTH_BYTES],encoded[R2_AUTH_BYTES],key[16];
 for(unsigned i=0;i<16;i++)key[i]=i;
 for(unsigned i=0;i<R2_AUTH_BYTES;i++){unsigned b;assert(sscanf(hex+2*i,"%2x",&b)==1);wire[i]=b;}
 struct r2_auth_record r;assert(!r2_auth_decode(&r,wire,sizeof wire,key));
 assert(r.pair_root==1&&r.pair_standby==2&&r.pair_root_boot==0x1111&&r.pair_standby_boot==0x2222&&r.pair_epoch==7&&r.pair_until_us==11000000);
 assert(!r2_auth_encode(encoded,&r,key)&&!memcmp(encoded,wire,sizeof wire));
 r.pair_until_us++;assert(r2_auth_encode(encoded,&r,key));r.pair_until_us--;
 r.kind=R2_AUTH_SERVICE;assert(r2_auth_encode(encoded,&r,key));r.kind=R2_AUTH_HELLO;
 r.sender=0;assert(r2_auth_encode(encoded,&r,key));r.sender=2;
 r.pair_standby=1;assert(r2_auth_encode(encoded,&r,key));r.pair_standby=2;
 r.pair_until_us=0;assert(r2_auth_encode(encoded,&r,key));
 r.pair_root=r.pair_standby=0;r.pair_root_boot=r.pair_standby_boot=0;r.pair_epoch=0;r.kind=R2_AUTH_SERVICE;
 assert(!r2_auth_encode(encoded,&r,key));
 const char *short_hex="0c0802032222000000000000222200000000000004000000000000000500000000000000060000000000000002333300000000000009000000a0bb0d0000000000f07e0e000000000040420f0000000000d01213000000000080841e0000000000050000000180f8fe80ffff60ea000060ea00004b8c51d2debf3ab5";
 for(unsigned i=0;i<R2_AUTH_BASE_BYTES;i++){unsigned b;assert(sscanf(short_hex+2*i,"%2x",&b)==1);assert(encoded[i]==b);}
 assert(!r2_auth_decode(&r,encoded,R2_AUTH_BASE_BYTES,key));
 assert(r2_auth_decode(&r,encoded,R2_AUTH_BYTES,key));
 puts("PASS Config5 independent golden vector, bounded pair TTL, sender/member, canonical absent fields");
}
