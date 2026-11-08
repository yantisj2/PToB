// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_JOIN_NIMBLE_H
#define PTOB_RX_JOIN_NIMBLE_H
#include "direct_wire.h"

// Registration runs before NimBLE starts. All other calls belong to its host.
bool ptob_bootstrap_register(void);
bool ptob_bootstrap_start(const PtobTrust *trust, const PtobLocator *gm, uint64_t now_us);
bool ptob_bootstrap_busy(void);
bool ptob_bootstrap_poll(uint64_t now_us, PtobGmAnchor *anchor);
bool ptob_bootstrap_stop(void);
void ptob_bootstrap_reset(void);
#endif
