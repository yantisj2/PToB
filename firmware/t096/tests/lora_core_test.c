/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void){
 char line[192];char source[65];memset(source,'f',64);source[64]=0;struct r2_request r;
 snprintf(line,sizeof line,"r2 00000001 1111111111111111 %s 2222222222222222 lora",source);
#ifdef R2_LORA
 assert(!r2_parse(line,&r)&&r.op==R2_LORA_CONTROL&&!r.seconds);
 strcat(line," 01");assert(!r2_parse(line,&r)&&r.seconds&&r.nonce==1);
 line[strlen(line)-1]='0';assert(!r2_parse(line,&r)&&r.seconds&&!r.nonce);
 line[strlen(line)-1]='2';assert(r2_parse(line,&r)==-EINVAL);
#else
 assert(r2_parse(line,&r)==-ENOTSUP);strcat(line," 01");assert(r2_parse(line,&r)==-ENOTSUP);
#endif
 uint64_t factory;assert(r2_factory_parse("0123456789abcdef",&factory)&&factory==UINT64_C(0x0123456789abcdef));assert(!r2_factory_parse("TC-01",&factory)&&!factory);assert(!r2_factory_parse("0000000000000000",&factory)&&!factory);
 puts("PASS feature-scoped LoRa query/control and exact factory parser");
}
