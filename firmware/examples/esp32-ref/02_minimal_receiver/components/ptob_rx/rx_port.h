// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_PORT_H
#define PTOB_RX_PORT_H
#include "receiver_clock.h"
#include "ptob_rx.h"

bool ptob_port_init(void);
void ptob_port_start(void);
void ptob_port_stop(void);
void ptob_port_reset(void);
// The port owns discovery, JOIN, periodic sync, and radio/counter teardown.
void ptob_rx_port_state(PtobRxState state);
void ptob_rx_port_capture(const PtobLocator *gm, PtobCapture capture,
                          const uint8_t bytes[PTOB_GR3_BYTES]);
void ptob_rx_port_set_anchor(const PtobGmAnchor *anchor);
bool ptob_rx_port_refresh_anchor(const PtobGmAnchor *anchor);
void ptob_rx_port_note_refresh_attempt(void);
void ptob_rx_port_clear_anchor(void);
bool ptob_rx_port_anchor_live(void);
bool ptob_rx_port_reacquire(void);
uint64_t ptob_rx_port_last_pair_us(void);
bool ptob_rx_port_fault(void);
void ptob_rx_counter_available(bool available);
extern PtobTrust ptob_rx_trust;
#endif
