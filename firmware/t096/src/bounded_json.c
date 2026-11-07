/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include "bounded_json.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
void r2_json_init(struct r2_json *j,char *out,size_t capacity){
 *j=(struct r2_json){.out=out,.capacity=capacity};if(capacity)out[0]=0;
}
/* Literal/key/string bytes need no printf parser or temporary string. Keep
 * snprintf's required length and exact truncated prefix/NUL behavior. */
void r2_json_text(struct r2_json *j,const char *text){
 if(j->error)return;
 size_t n=strlen(text);
 if(n>(size_t)INT_MAX||j->used>(size_t)INT_MAX-n){j->error=-EOVERFLOW;return;}
 if(j->used<j->capacity){
  size_t copied=j->capacity-j->used-1;if(copied>n)copied=n;
  memcpy(j->out+j->used,text,copied);j->out[j->used+copied]=0;
 }
 j->used+=n;
}
void r2_json_format(struct r2_json *j,const char *format,...){
 if(j->error)return;
 size_t left=j->used<j->capacity?j->capacity-j->used:0;
 va_list ap;va_start(ap,format);
 int n=vsnprintf(left?j->out+j->used:NULL,left,format,ap);va_end(ap);
 if(n<0)j->error=n;
 else if(j->used>(size_t)INT_MAX-(unsigned)n)j->error=-EOVERFLOW;
 else j->used+=(unsigned)n;
}
int r2_json_result(const struct r2_json *j){return j->error?j->error:(int)j->used;}
#if defined(__GNUC__) && !defined(__clang__)
#define JSON_SHARED __attribute__((noinline,noclone))
#else
#define JSON_SHARED __attribute__((noinline))
#endif
static void JSON_SHARED quoted(struct r2_json *j,const char *text,const char *end){
 r2_json_text(j,"\"");r2_json_text(j,text);r2_json_text(j,end);
}
#undef JSON_SHARED
void r2_json_u32_array(struct r2_json *j,const uint32_t *values,unsigned count){
 r2_json_text(j,"[");
 for(unsigned i=0;i<count&&!j->error;i++){
  if(i)r2_json_text(j,",");
  r2_json_format(j,"%u",values[i]);
 }
 r2_json_text(j,"]");
}
/* memcpy also permits a naturally aligned object's subfield/array member;
 * never introduce unaligned typed loads or a descriptor's aliasing fiction. */
static void scalar(struct r2_json *j,unsigned type,const void *p){
 union {uint32_t u32;int32_t i32;uint64_t u64;int64_t i64;uint16_t u16;uint8_t u8;const char *s;bool b;} v;
 switch(type){
 case R2_J_U32:memcpy(&v.u32,p,4);r2_json_format(j,"%u",v.u32);break;
 case R2_J_U16:memcpy(&v.u16,p,2);r2_json_format(j,"%u",(unsigned)v.u16);break;
 case R2_J_U8:memcpy(&v.u8,p,1);r2_json_format(j,"%u",(unsigned)v.u8);break;
 case R2_J_I32:memcpy(&v.i32,p,4);r2_json_format(j,"%d",v.i32);break;
 case R2_J_U64:memcpy(&v.u64,p,8);r2_json_format(j,"%llu",(unsigned long long)v.u64);break;
 case R2_J_I64:memcpy(&v.i64,p,8);r2_json_format(j,"%lld",(long long)v.i64);break;
 case R2_J_HEX64:memcpy(&v.u64,p,8);r2_json_format(j,"\"%016llx\"",(unsigned long long)v.u64);break;
 case R2_J_BOOL:memcpy(&v.b,p,sizeof v.b);r2_json_text(j,v.b?"true":"false");break;
 case R2_J_STRING:memcpy(&v.s,p,sizeof v.s);quoted(j,v.s,"\"");break;
 case R2_J_CHARS:quoted(j,p,"\"");break;
 default:j->error=-EINVAL;break;
 }
}
void r2_json_fields(struct r2_json *j,const struct r2_json_field *fields,unsigned count,
 const void *const *bases,unsigned base_count){
 for(unsigned i=0;i<count&&!j->error;i++){
  const struct r2_json_field *f=&fields[i];unsigned type=f->type_count&15;
  if(i)r2_json_text(j,",");
  if(type==R2_J_RAW){r2_json_text(j,f->key);continue;}
  quoted(j,f->key,"\":");
  if(type==R2_J_LITERAL_STRING){quoted(j,f->key+strlen(f->key)+1,"\"");continue;}
  if(f->base>=base_count||!bases[f->base]){j->error=-EINVAL;break;}
  const uint8_t *p=(const uint8_t *)bases[f->base]+f->offset;
  if(type>=R2_J_U32_ARRAY&&type<=R2_J_U64_ARRAY){
   unsigned n=f->type_count>>4;if(!n)n=16;
   unsigned element=type==R2_J_U32_ARRAY?R2_J_U32:type==R2_J_I32_ARRAY?R2_J_I32:R2_J_U64;
   unsigned stride=element==R2_J_U64?8:4;
   r2_json_text(j,"[");
   for(unsigned k=0;k<n;k++){if(k)r2_json_text(j,",");scalar(j,element,p+k*stride);}
   r2_json_text(j,"]");
  }else scalar(j,type,p);
 }
}
