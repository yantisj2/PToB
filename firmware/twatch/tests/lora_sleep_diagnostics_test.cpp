// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_shadow.hpp"
#include "bounded_log.hpp"
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
watch::LoRaShadow::View watch_lora_shadow_snapshot(){
 watch::LoRaShadow::View s{};
 s.next_sync=s.next_meta=s.rx_us=s.sleep_us=s.unknown_us=s.inactive_us=s.last_us=UINT64_MAX;
 s.pairs=s.qualified=s.covered=s.outside=s.misses=s.resets=s.meta_seen=UINT32_MAX;
 s.reason=255;s.tracking=s.meta_phase=s.would_rx=s.active=true;return s;
}
watch::LoRaSleepView watch_lora_sleep_snapshot(){
 watch::LoRaSleepView s{};s.sleep_us=s.rx_us=s.at=s.last_transition=UINT64_MAX;
 s.sleeps=s.wakes=s.refused=UINT32_MAX;s.sleeping=s.active=true;return s;
}
// A bounded formatting sink; the production diagnostics implementation is linked.
bool v12::ConsoleFrame::format(const char* fmt,...){
 va_list args;va_start(args,fmt);int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);
 if(n<0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}
 for(int i=0;i<n;i++)rows+=bytes[length+i]=='\n';length+=n;return true;
}
extern void watch_lora_shadow_diagnostics(v12::ConsoleFrame&);
int main(){
 v12::ConsoleFrame f;watch_lora_shadow_diagnostics(f);
 assert(!f.failed&&f.rows==2&&f.length<1400);
 assert(std::strstr(f.bytes,"TWATCH_LORA_SHADOW version=2 shadow_only=0"));
 assert(std::strstr(f.bytes,"TWATCH_LORA_SLEEP version=1 enabled=1 sleeping=1"));
 std::printf("PASS actual sleep diagnostic formatter: two complete rows, maximum counters, %u bytes\n",f.length);
}
