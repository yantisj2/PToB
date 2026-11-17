// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {
void log_usb_power(bool present); // Existing UI PMU observation; no extra poller.
uint32_t log_usb_generation();
}
