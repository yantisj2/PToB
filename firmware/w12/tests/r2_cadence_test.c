#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "network.h"
#include "precision_codec.h"
static void decode_hex(uint8_t *out,const char *hex)
{
 assert(strlen(hex)==2*R2_PRECISION_AD_BYTES);
 for(unsigned i=0;i<R2_PRECISION_AD_BYTES;i++){unsigned value;assert(sscanf(hex+2*i,"%2x",&value)==1);out[i]=(uint8_t)value;}
}
int main(int argc,char **argv)
{
 assert(argc==3);uint8_t current[R2_PRECISION_AD_BYTES],old[R2_PRECISION_AD_BYTES];decode_hex(current,argv[1]);decode_hex(old,argv[2]);
 assert(R2_PRECISION_INTERVAL_UNITS==50&&R2_PRECISION_INTERVAL_US==62500&&R2_PRECISION_HZ==16);
 assert(R2_FIXED_SCHEDULE==UINT64_C(0x503132020000f424));
 for(unsigned recipient=0;recipient<4;recipient++){
  uint8_t key[16];for(unsigned i=0;i<16;i++)key[i]=(uint8_t)(recipient*31+i);
  struct r2_precision_packet p;assert(!r2_precision_decode(&p,current,sizeof current,recipient,key));
  assert(p.root==2&&p.source_epoch==7&&p.train==3&&p.count==2);
  assert(r2_precision_decode(&p,old,sizeof old,recipient,key)==-EINVAL);
 }
 puts("PASS independent signed16Hz golden vector; authentic32Hz vector refused; schedule revision2 bound");return 0;
}
