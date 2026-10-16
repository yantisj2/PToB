#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "group_join.h"
#include "group_precision.h"
static size_t vector(unsigned char* out,const char* json,const char* key){char find[80];snprintf(find,sizeof(find),"\"%s\": \"",key);const char* p=strstr(json,find);assert(p);p+=strlen(find);size_t n=0;while(*p!='"'){unsigned v;assert(sscanf(p,"%2x",&v)==1);out[n++]=(unsigned char)v;p+=2;}return n;}
int main(int argc,char** argv){assert(argc==2);FILE* f=fopen(argv[1],"rb");assert(f);char json[16384]={0};assert(fread(json,1,sizeof(json)-1,f)>0);fclose(f);
 struct r2_group_config g={.id=UINT64_C(0x123456789abcdef0),.epoch=0x01020304};for(unsigned i=0;i<32;i++)g.psk[i]=(unsigned char)i;
 const uint64_t ids[4]={111,222,333,444};unsigned char w[256],round[256];
 size_t n=vector(w,json,"join_accept");struct r2_join_record j;assert(!r2_group_join_decode(&j,w,n,&g));assert(j.sender==111&&j.receiver==555&&j.kind==4);assert(!r2_group_join_encode(round,&j,&g)&&!memcmp(w,round,n));
 for(unsigned i=0;i<n;i++){w[i]^=1;assert(r2_group_join_decode(&j,w,n,&g));w[i]^=1;}
 n=vector(w,json,"direct");struct r2_precision_packet p;assert(!r2_group_precision_decode(&p,w,n,&g,ids));assert(!r2_group_precision_encode(round,&p,&g,ids)&&!memcmp(w,round,n));
 for(unsigned i=0;i<n;i++){w[i]^=1;assert(r2_group_precision_decode(&p,w,n,&g,ids));w[i]^=1;}
 n=vector(w,json,"relay");struct r2_relay_packet r;assert(!r2_group_relay_decode(&r,w,n,&g,ids));unsigned char proof[32];assert(!r2_group_origin_sign(proof,&r.origin,&g,ids));assert(!r2_group_relay_encode(round,&r,proof,&g,ids)&&!memcmp(w,round,n));
 for(unsigned i=0;i<n;i++){w[i]^=1;assert(r2_group_relay_decode(&r,w,n,&g,ids));w[i]^=1;}
 for(unsigned k=0;k<2;k++){n=vector(w,json,k?"v14_serving1650_relay":"v14_held_relay");assert(!r2_group_relay_decode3(&r,w,n,&g,ids));assert(!r2_group_origin_sign3(proof,&r.origin,&g,ids));assert(!r2_group_relay_encode3(round,&r,proof,&g,ids)&&!memcmp(w,round,n));
 for(unsigned i=0;i<n;i++){w[i]^=1;assert(r2_group_relay_decode3(&r,w,n,&g,ids));w[i]^=1;}}
 // Every public control vector validates the exact domain with compiled HMAC.
 for(unsigned i=0;i<2;i++){n=vector(w,json,i?"service":"hello");assert(r2_group_verify(w+n-16,&g,"PTOBG1C1",w,n-16));}
 puts("PASS compiled G1 join/direct/relay canonical vectors and every-byte corruption refusal");return 0;
}
