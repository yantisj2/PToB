/* Public deterministic fixture keys; no production enrollment. */
#include "relay_codec.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 assert(r2_relay_bound_add(UINT32_MAX,1)==UINT32_MAX);
 assert(r2_relay_bound_add(1,UINT32_MAX)==UINT32_MAX);
 assert(r2_relay_bound_add(UINT32_MAX-10,10)==UINT32_MAX);
 assert(r2_relay_bound_add(20,30)==50);
 if(argc!=3 && argc!=4)return 2;
 size_t len=strlen(argv[2]);if(len!=380)return 2;
 uint8_t data[190],keys[4][4][16],out[190],proof[32];
 for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)for(unsigned b=0;b<16;b++)keys[r][c][b]=1+r*31+c*7+b*13;
 for(unsigned i=0;i<190;i++){unsigned v;if(sscanf(argv[2]+2*i,"%2x",&v)!=1)return 2;data[i]=v;}
 unsigned recipient=strtoul(argv[1],NULL,10),root=data[10],sender=data[88];
 struct r2_relay_packet p;
 int rc=r2_relay_decode(&p,data,190,recipient,keys[root<4?root:0][recipient<4?recipient:0],keys[sender<4?sender:0][recipient<4?recipient:0]);
 if(rc){printf("REJECT %d\n",rc);return 1;}
 if(argc==4){puts("ACCEPT");return 0;}
 assert(!r2_relay_origin_sign(proof,&p.origin,keys[p.origin.root]));
 assert(!r2_relay_encode(out,&p,proof,keys[p.route.transmitter]));
 assert(!memcmp(out,data,190));
 for(unsigned i=0;i<190;i++){printf("%02x",out[i]);}puts("");
 return 0;
}
