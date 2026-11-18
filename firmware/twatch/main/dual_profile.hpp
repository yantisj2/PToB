// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watchdual {
// Build-bound calibration applicability. A changed authenticated path cannot
// inherit a confirmed correction. These fields never restrict receiver source
// selection or shared-clock availability. Roots use their wire namespaces.
struct Profile {
 bool enabled=false;
 uint64_t ble_root=0,ble_parent=0,lora_emitter=0,lora_root=0;
 uint8_t ble_depth=0;
 int32_t ble_incremental_ns=0,lora_correction_ns=0,ble_existing_ns=0;
 // Reviewed experiment only: carry fixed engineering priors onto unmatched
 // authenticated paths without asserting that their calibration is valid.
 bool study_fixed_priors=false;
};
}
