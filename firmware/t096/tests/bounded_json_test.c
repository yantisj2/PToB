/* Actual shared writer: required length, exact bytes and every truncation. */
#include "bounded_json.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
struct values {
 uint32_t u;int32_t i;uint64_t wide;int64_t negative;bool yes;
 const char *string;char chars[8];uint32_t a[16];int32_t b[2];uint64_t c[2];
};
#define F(type,key,member) R2_JSON_FIELD(type,key,0,struct values,member)
static const struct r2_json_field fields[]={
 R2_JSON_RAW("\"schema\":1"),F(R2_J_U32,"u",u),F(R2_J_I32,"i",i),
 F(R2_J_U64,"wide",wide),F(R2_J_I64,"negative",negative),F(R2_J_HEX64,"hex",wide),
 F(R2_J_BOOL,"yes",yes),F(R2_J_STRING,"string",string),F(R2_J_CHARS,"chars",chars),
 R2_JSON_LITERAL("literal","fixed"),
 R2_JSON_ARRAY(R2_J_U32_ARRAY,16,"a",0,struct values,a),
 R2_JSON_ARRAY(R2_J_I32_ARRAY,2,"b",0,struct values,b),
 R2_JSON_ARRAY(R2_J_U64_ARRAY,2,"c",0,struct values,c)
};
#undef F
int main(void){
 struct values v={.u=UINT32_MAX,.i=INT32_MIN,.wide=UINT64_MAX,.negative=INT64_MIN,
  .yes=true,.string="pointer",.chars="inline",.b={INT32_MIN,INT32_MAX},.c={0,UINT64_MAX}};
 for(unsigned i=0;i<16;i++)v.a[i]=UINT32_MAX-i;
 char expected[1024];int n=snprintf(expected,sizeof expected,
  "{\"schema\":1,\"u\":%u,\"i\":%d,\"wide\":%llu,\"negative\":%lld,\"hex\":\"%016llx\",\"yes\":true,\"string\":\"pointer\",\"chars\":\"inline\",\"literal\":\"fixed\",\"a\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"b\":[%d,%d],\"c\":[%llu,%llu]}",
  v.u,v.i,(unsigned long long)v.wide,(long long)v.negative,(unsigned long long)v.wide,
  v.a[0],v.a[1],v.a[2],v.a[3],v.a[4],v.a[5],v.a[6],v.a[7],v.a[8],v.a[9],v.a[10],v.a[11],v.a[12],v.a[13],v.a[14],v.a[15],v.b[0],v.b[1],(unsigned long long)v.c[0],(unsigned long long)v.c[1]);
 assert(n>0&&(size_t)n<sizeof expected);const void *bases[]={&v};
 for(size_t capacity=0;capacity<=(size_t)n+2;capacity++){
  unsigned char storage[1056];memset(storage,0xa5,sizeof storage);
  char *out=(char *)storage+8;struct r2_json j;r2_json_init(&j,out,capacity);
  r2_json_format(&j,"{");r2_json_fields(&j,fields,sizeof fields/sizeof fields[0],bases,1);r2_json_format(&j,"}");
  assert(r2_json_result(&j)==n);
  for(unsigned k=0;k<8;k++)assert(storage[k]==0xa5);
  for(size_t k=8+capacity;k<sizeof storage;k++)assert(storage[k]==0xa5);
  if(capacity){size_t length=capacity-1<(size_t)n?capacity-1:(size_t)n;assert(!memcmp(out,expected,length)&&out[length]==0);}
 }
 struct r2_json j;r2_json_init(&j,NULL,0);r2_json_format(&j,"{");r2_json_fields(&j,fields,sizeof fields/sizeof fields[0],bases,1);r2_json_format(&j,"}");assert(r2_json_result(&j)==n);
 j.used=INT_MAX;r2_json_format(&j,"x");assert(r2_json_result(&j)==-EOVERFLOW);
 j.error=-77;r2_json_format(&j,"ignored");assert(r2_json_result(&j)==-77);
 r2_json_init(&j,NULL,0);r2_json_fields(&j,fields,sizeof fields/sizeof fields[0],bases,0);assert(r2_json_result(&j)==-EINVAL);
 printf("PASS exact shared JSON: %d bytes, %d capacities, all scalar/array types and negative/overflow preservation\n",n,n+3);
}
