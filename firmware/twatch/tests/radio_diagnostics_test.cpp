// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "radio_diagnostics.hpp"
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
struct Frame {
 std::string bytes;
 void format(const char* fmt,...){char buf[4096];va_list args;va_start(args,fmt);const int n=vsnprintf(buf,sizeof buf,fmt,args);va_end(args);assert(n>0&&n<int(sizeof buf));bytes+=buf;}
};
struct Recovery {uint32_t attempts=UINT32_MAX,recovered=UINT32_MAX,refused=UINT32_MAX;bool pending=true;uint8_t reason=255;};
int main(){
 watch::RadioJournalStatus s;for(auto& r:s.latest){
  r.orphan_epoch=r.episode=r.capture_epoch=r.callback_epoch=r.sequence=r.mask=r.generation=r.capture_fault=r.build_tag=r.owner_flags=UINT32_MAX;
  r.orphan_at=r.requested_parent=r.boot_first_at=r.boot=r.at_us=r.parent=r.root=r.error_at_us=r.last_error_at_us=r.stop_us=r.deadline_us=r.completed_us=UINT64_MAX;
  r.boot_first_rc=r.rc=r.last_rc=INT32_MIN;r.orphan_handle=r.handle=r.interval=UINT16_MAX;r.recovery_reason=r.reason=r.expected_sid=r.observed_sid=r.address_match=r.boot_first_api=r.event=r.api=r.site=r.stage=r.relay=r.last_api=255;
 }
 s.saved=s.dropped=UINT32_MAX;s.storage_error=INT32_MIN;
 Frame f;watch::radio_fault_fields(f,s,Recovery{});
 assert(f.bytes.size()<4000);assert(f.bytes.find('\n')==std::string::npos);
 assert(f.bytes.find(" ble_fault_seq=")!=std::string::npos);
 assert(f.bytes.find(" lor_fault_seq=")!=std::string::npos);
 assert(f.bytes.find("rf_reset=0")!=std::string::npos);
}
