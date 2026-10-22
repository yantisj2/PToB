// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {
// N30-only observer extension; the shared W12 LogStatus ABI is unchanged.
struct N30TxCaptureStatus {bool selected=false;uint32_t filtered=0;};
bool n30_capture_tx_begin(uint64_t id,uint32_t duration_ms);
N30TxCaptureStatus n30_capture_tx_status();
}
