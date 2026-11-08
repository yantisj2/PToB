// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_RECEIVE_STAMP_H
#define PTOB_RX_RECEIVE_STAMP_H
#include "receiver_clock.h"

bool ptob_stamp_install(uint32_t max_bridge_us);
bool ptob_stamp_stop(void);
bool ptob_stamp_fault(void);
void ptob_stamp_flush(void);
bool ptob_stamp_match(const uint8_t bytes[PTOB_GR3_BYTES], uint64_t now, PtobCapture *out);
#endif
