// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
class RadioFaultTimer {
 bool active_=false;uint64_t since_=0,last_=0,attempt_=0;bool attempted_=false;
 public:
 static constexpr uint64_t DelayUs=90000000;
 bool observe(uint64_t now,bool fault,bool enabled){
  if(now<last_||!fault||!enabled){active_=false;attempted_=false;}
  last_=now;
  if(!fault||!enabled)return false;
  if(!active_){active_=true;since_=now;}
  const auto base=attempted_?attempt_:since_;
  if(now-base<DelayUs)return false;
  attempted_=true;attempt_=now;return true;
 }
};
// Fixed journal records contain reason codes and identities, never credentials.
struct RadioFaultRecord {
 uint32_t version=1,sequence=0;uint64_t boot=0,at_us=0,parent=0,root=0;
 uint32_t mask=0,generation=0,capture_fault=0;int32_t rc=0,last_rc=0;
 uint64_t error_at_us=0,last_error_at_us=0,stop_us=0,deadline_us=0,completed_us=0;uint32_t build_tag=0,owner_flags=0,episode=0,capture_epoch=0,callback_epoch=0;
 uint64_t requested_parent=0,boot_first_at=0;int32_t boot_first_rc=0;
 uint16_t handle=0,interval=0,orphan_handle=0;uint32_t orphan_epoch=0;uint64_t orphan_at=0;uint8_t reason=0,expected_sid=0,observed_sid=0,address_match=0,boot_first_api=0;
 uint8_t radio=0,event=0,api=0,site=0,stage=0,relay=0,last_api=0,recovery_reason=0;
};
struct RadioJournalStatus {RadioFaultRecord latest[2]{};uint32_t saved=0,dropped=0;int32_t storage_error=0;uint32_t slot=0;bool slot_valid=false;};
void radio_journal_ready();
void radio_journal_stop();
bool radio_journal_idle();
void radio_journal_record(RadioFaultRecord record);
RadioJournalStatus radio_journal_status(unsigned slot=0);
}
