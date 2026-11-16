// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
// Queue worker-owned SX1262 recovery only after a physically verified OFF.
// A true result acknowledges the mailbox, never timing/source admission.
bool n30_lora_fault_recover();

// Bits: current generation-matched OFF, SPI retired, capture released, reset asserted.
// Historical receive faults do not invalidate physical retirement; cleanup faults do.
unsigned n30_lora_off_proof();
