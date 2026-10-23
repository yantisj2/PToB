// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "native_r2.hpp"
#include "bounded_log.hpp"
#include <cinttypes>
[[gnu::noinline]] inline void n30_rc9_diagnostics(v12::ConsoleFrame& f){
 const auto s=v12::r2_rc9_status();
 f.format("N30_RC9_STATUS sampled_us=%" PRIu64 " UTC_us=%" PRIu64 " until_us=%" PRIu64 " valid=%u following=%u fault=%u\n",s.sampled_us,s.utc_us,s.until_us,unsigned(s.valid),unsigned(s.following),unsigned(s.fault));
 f.format("N30_RC9_ORIGIN server=%016" PRIx64 " server_boot=%016" PRIx64 " root=%016" PRIx64 " root_boot=%016" PRIx64 " source_epoch=%" PRIu32 " emitter=%016" PRIx64 " emitter_boot=%016" PRIx64 "\n",s.server,s.server_boot,s.root,s.root_boot,s.source_epoch,s.emitter,s.emitter_boot);
 f.format("N30_RC9_QUALITY assurance=%u authority=%u metadata_known=%u upstream_link_rms_ns=%" PRIu32 " bound=UNKNOWN\n",unsigned(s.assurance),unsigned(s.authority),unsigned(s.metadata_known),s.rms_ns);
 f.format("N30_RC9_COUNTERS sequence=%" PRIu32 " accepted=%" PRIu32 " rejected=%" PRIu32 "\n",s.sequence,s.accepted,s.rejected);
 f.format("N30_RC9_LEASE until_us=%" PRIu64 " clients=%u serving=%u\n",s.client_until_us,unsigned(s.clients),unsigned(s.serving));
 f.format("N30_RC9_CONTRACT wire=1 source=RC8 hops=1 precision=UNKNOWN BLE_training=NONE relay=NONE GM=NONE RX_ONLY=1\n");
}
