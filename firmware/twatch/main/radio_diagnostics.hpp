// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "radio_watchdog.hpp"
#include <cinttypes>
namespace watch {
// Appended to TWATCH_SOURCE, retaining the existing five-row dual envelope.
// On-demand USB reads only; no continuous UART log or flash polling writes.
template<class Frame,class Recovery>
void radio_fault_fields(Frame& out,const RadioJournalStatus& journal,const Recovery& recovery){
 out.format(" rf_version=1 rf_slot=%u rf_slot_valid=%u rf_delay_s=90 rf_reset=0 rf_saved=%" PRIu32 " rf_dropped=%" PRIu32 " rf_storage_rc=%" PRId32
   " ble_recovery=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%u/%u",
   unsigned(journal.slot),unsigned(journal.slot_valid),journal.saved,journal.dropped,journal.storage_error,recovery.attempts,recovery.recovered,recovery.refused,unsigned(recovery.pending),unsigned(recovery.reason));
 for(unsigned i=0;i<2;++i){const auto& f=journal.latest[i];const char* p=i?"lor_fault":"ble_fault";
  out.format(" %s_seq=%" PRIu32 " %s_boot=%016" PRIx64 " %s_at=%" PRIu64 " %s_event=%u %s_mask=%" PRIu32
    " %s_api=%u %s_rc=%" PRId32 " %s_site=%u %s_stage=%u %s_parent=%016" PRIx64 " %s_root=%016" PRIx64
    " %s_relay=%u %s_generation=%" PRIu32 " %s_capture=%" PRIu32 " %s_cause_at=%" PRIu64
    " %s_owner=%" PRIu32 " %s_stop=%" PRIu64 " %s_deadline=%" PRIu64 " %s_completed=%" PRIu64 " %s_last_api=%u %s_last_rc=%" PRId32 " %s_last_at=%" PRIu64 " %s_build=%08" PRIx32,
    p,f.sequence,p,f.boot,p,f.at_us,p,unsigned(f.event),p,f.mask,p,unsigned(f.api),p,f.rc,p,unsigned(f.site),p,unsigned(f.stage),
    p,f.parent,p,f.root,p,unsigned(f.relay),p,f.generation,p,f.capture_fault,p,f.error_at_us,p,f.owner_flags,p,f.stop_us,p,f.deadline_us,p,f.completed_us,p,unsigned(f.last_api),p,f.last_rc,p,f.last_error_at_us,p,f.build_tag);
  out.format(" %s_orphan_handle=%u %s_orphan_epoch=%" PRIu32 " %s_orphan_at=%" PRIu64 " %s_recovery_reason=%u %s_reason=%u %s_episode=%" PRIu32 " %s_epoch=%" PRIu32 " %s_callback_epoch=%" PRIu32
    " %s_handle=%u %s_interval=%u %s_expected_sid=%u %s_observed_sid=%u %s_address_match=%u %s_requested_parent=%016" PRIx64
    " %s_boot_first_api=%u %s_boot_first_rc=%" PRId32 " %s_boot_first_at=%" PRIu64,
    p,unsigned(f.orphan_handle),p,f.orphan_epoch,p,f.orphan_at,p,unsigned(f.recovery_reason),p,unsigned(f.reason),p,f.episode,p,f.capture_epoch,p,f.callback_epoch,p,unsigned(f.handle),p,unsigned(f.interval),
    p,unsigned(f.expected_sid),p,unsigned(f.observed_sid),p,unsigned(f.address_match),p,f.requested_parent,p,unsigned(f.boot_first_api),p,f.boot_first_rc,p,f.boot_first_at);
 }
}
}
