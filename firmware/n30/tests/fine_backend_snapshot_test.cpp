// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#define PTOB_N30_FINE_PINS 1
#define PTOB_N30_FINE_RW 1
#define PTOB_N30_FINE_BACKEND 1
#ifndef FINE_OWNER_SOURCE
#define FINE_OWNER_SOURCE "../main/fine_capture.cpp"
#endif
#include FINE_OWNER_SOURCE
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}length+=uint16_t(n);return true;}
}
void arrive_after_current(int channel){if(channel==2){fake_radio_capture[0]=fake_raw+40;fake_device.int_raw.val|=1u<<27;fake_capture_read_inject=nullptr;}}
int main(int argc,char** argv){
 assert(argc==2);const bool wrap=!std::strcmp(argv[1],"wrap"),stale=!std::strcmp(argv[1],"stale");
 if(wrap)fake_raw=UINT32_MAX-500;
 assert(n30_fine_backend_start(2,PTOB_N30_FINE_FACTORY,fake_now));const auto origin=first_raw32;uint64_t tick=0;
 if(stale){
  fake_now+=300000;fake_raw+=300000*80;fake_radio_capture[0]=origin;fake_device.int_raw.val|=1u<<27;
  assert(!n30_fine_backend_current(&tick)&&contained&&error==-26&&!active);
 }else{
  if(wrap)fake_raw=UINT32_MAX-40;
  // The next CAP2 read injects a real newer CAP0 after the CURRENT latch.
  // Old current-then-CAP code falsely contains here. The saved pre-latch
  // snapshot ignores that new edge until a subsequent CURRENT can anchor it.
  fake_capture_read_inject=arrive_after_current;
  assert(n30_fine_backend_current(&tick)&&active&&!error&&!contained);
  const auto first=tick;assert(fake_device.int_raw.val&(1u<<27));
  assert(n30_fine_backend_current(&tick)&&tick>first&&active&&!error);
  if(wrap)assert(wraps==1&&latest_raw32<1000);
 }
 n30_fine_stop();assert(n30_fine_released());
 std::puts("PASS actual owner pending-before-CURRENT snapshot / future arrival / wrap / true stale refusal");
}
