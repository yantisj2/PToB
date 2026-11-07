/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* On-demand trusted firmware field tables only. No writable global state,
 * intermediate value array, allocation, transport, or device-side log store. */
struct r2_json {char *out;size_t capacity,used;int error;};
enum r2_json_type {
 R2_J_U32,R2_J_I32,R2_J_U64,R2_J_I64,R2_J_HEX64,R2_J_BOOL,
 R2_J_STRING,R2_J_CHARS,R2_J_LITERAL_STRING,R2_J_RAW,
 R2_J_U32_ARRAY,R2_J_I32_ARRAY,R2_J_U64_ARRAY,R2_J_U16,R2_J_U8
};
/* Array count occupies the high nibble: zero means16 for array types.
 * Other types use only the low nibble. Native32-bit descriptor is8 bytes. */
struct r2_json_field {const char *key;uint16_t offset;uint8_t base,type_count;};
#if UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(struct r2_json_field)==8,"Bounded JSON native descriptor");
#endif
#define R2_JSON_FIELD(kind,key_,base_,type_,member_) \
 {key_,offsetof(type_,member_),base_,kind}
#define R2_JSON_ARRAY(kind,n,key_,base_,type_,member_) \
 {key_,offsetof(type_,member_),base_,(kind)|(((n)&15)<<4)}
#define R2_JSON_RAW(text_) {text_,0,0,R2_J_RAW}
#define R2_JSON_LITERAL(key_,text_) {key_ "\0" text_,0,0,R2_J_LITERAL_STRING}
void r2_json_init(struct r2_json *,char *,size_t);
void r2_json_text(struct r2_json *,const char *) __attribute__((noinline));
void r2_json_format(struct r2_json *,const char *,...) __attribute__((format(printf,2,3)));
void r2_json_u32_array(struct r2_json *,const uint32_t *,unsigned);
void r2_json_fields(struct r2_json *,const struct r2_json_field *,unsigned,
                    const void *const *,unsigned);
/* snprintf-compatible required length/truncation for bounded supported data;
 * a negative formatting/count error is retained rather than masked. */
int r2_json_result(const struct r2_json *);
