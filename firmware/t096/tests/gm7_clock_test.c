/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
uint32_t gm7_fake_counter,gm7_fake_guard;
int main(void){
 uint64_t now,tick,lo,hi;uint32_t epoch;
 assert(!r2_gm7_clock_now(&now,&epoch));
 r2_gm7_clock_begin(7);r2_gm7_clock_publish(UINT64_C(0x100000064),100,7,true);gm7_fake_counter=200;
 assert(r2_gm7_clock_now(&now,&epoch)&&epoch==7&&now==UINT64_C(0x1000000c8)/16);
 assert(r2_gm7_clock_event(UINT32_MAX-99,7,&tick)&&tick==UINT64_C(0xffffff9c));
 assert(r2_gm7_clock_start(UINT32_MAX-99,7,1,&tick)&&tick==UINT64_C(0xfffffd1c));
 assert(r2_gm7_clock_start(UINT32_MAX-99,7,2,&tick)&&tick==UINT64_C(0xfffffe1c));
 assert(!r2_gm7_clock_start(200,7,3,&tick));
 assert(r2_gm7_clock_bounds(UINT32_MAX-99,7,40,1,&lo,&hi)&&lo==UINT64_C(0xfffffd0b)&&hi==UINT64_C(0xfffffd2d));
 assert(r2_gm7_clock_bounds(200,7,0,5,&lo,&hi)&&hi-lo==162);
 assert(!r2_gm7_clock_event(200,8,&tick));
 assert(!r2_gm7_clock_event(100-4000001u,7,&tick));
 /* Publication0.9s old plus event0.2s before it is1.1s queue age. */
 gm7_fake_counter=14400100;assert(!r2_gm7_clock_event(100-3200000u,7,&tick));
 gm7_fake_counter=16000101;assert(!r2_gm7_clock_now(&now,&epoch));
 gm7_fake_guard=32769;gm7_fake_counter=100;assert(!r2_gm7_clock_now(&now,&epoch));
 r2_gm7_clock_publish(0,0,0,false);assert(!r2_gm7_clock_event(200,7,&tick));assert(!r2_gm7_clock_now(&now,&epoch));
 /* A late old writer cannot reopen a stopped epoch. */
 r2_gm7_clock_publish(500,500,7,true);gm7_fake_counter=516;assert(!r2_gm7_clock_now(&now,&epoch));
 r2_gm7_clock_begin(8);r2_gm7_clock_publish(100,100,8,true);gm7_fake_counter=116;
 assert(r2_gm7_clock_now(&now,&epoch)&&now==7&&epoch==8);assert(!r2_gm7_clock_event(116,7,&tick));
 r2_gm7_clock_publish(UINT64_MAX-100,100,8,true);gm7_fake_counter=201;assert(!r2_gm7_clock_now(&now,&epoch));
 puts("PASS actual GM7 HF clock reader: existing extension, wrap, LE1M40us/LE2M24us START coordinate, outward chain/quantization bounds, stale epoch/age, release/reset and overflow fences; no LF splice");return 0;
}
