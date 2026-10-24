/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/gm7_air.h"
/* One bounded controller event, not an HCI advertising interval. The caller
 * owns each immutable job until both controller refcount and mayfly retire.
 * API calls are serialized on the GM service owner; callbacks never format,
 * authenticate, allocate, wait or call a public Bluetooth API. */
int r2_gm7_controller_submit(unsigned slot, struct gm7_scheduler *,
                             const uint8_t address[6], uint64_t now);
int r2_gm7_controller_drain(unsigned slot, uint64_t now);
bool r2_gm7_controller_idle(void);
void r2_gm7_controller_source(uint64_t original_until, bool live);
void r2_gm7_controller_cancel(uint64_t now);
/* Capture owner supplies an exclusive END→TIMER3 CC5 route. CC1 remains the
 * existing exclusive ADDRESS route; no controller TIMER0/1 or PPI is borrowed.
 */
int r2_gm7_capture_enable(uint32_t epoch, unsigned address_route,
                          unsigned end_route);
int r2_gm7_capture_disable(void);
bool r2_gm7_capture_busy(void);
bool r2_gm7_capture_arm(void *owner, bool transmit, bool auxiliary);
void r2_gm7_capture_done(void *owner, bool done, bool crc, const void *pdu);
void r2_gm7_capture_abort(void *owner);
struct r2_gm7_rx {
  uint32_t raw, epoch;
  uint16_t length;
  uint8_t address[6], ad[GM7_MAX_AD_BYTES];
};
bool r2_gm7_capture_pop(struct r2_gm7_rx *);
uint32_t r2_gm7_capture_overflows(void);
/* Native captured endpoints, converted after the ISR by the real capture
 * owner. An unavailable/old capture domain never substitutes callback time. */
struct r2_gm7_tx {
  uint64_t ticket;
  uint32_t generation, buffer_version, epoch, primary_address, aux_address,
      aux_end;
  uint8_t slot;
  bool complete,exposed;
};
bool r2_gm7_controller_result(struct r2_gm7_tx *);
bool r2_gm7_controller_exposed(unsigned slot,uint64_t ticket);
/* Stock scanner hooks: only passive primary discovery is channel-pinned.
 * AUX follow and ordinary G1/ACL scanner behavior retain their own owner. */
void r2_gm7_controller_scan(bool enabled, bool phase_known,
                            uint64_t frame_start);
bool r2_gm7_controller_scan37(void);

/* Capture callback: immutable active slot/ticket ownership only. */
void r2_gm7_controller_capture(void *, bool auxiliary, uint32_t address_raw,
                               uint32_t end_raw, uint32_t epoch, bool complete);

bool r2_gm7_controller_receiving(void);
void r2_gm7_capture_receive_arm(const void *pdu);

/* Scoped own G1 ticker anchor; inactive in normal RC operation. */
void r2_gm7_controller_precision(bool enable,uint32_t hf_phase,uint64_t first_end_deadline,uint32_t train);
void r2_gm7_controller_precision_bind(uint16_t handle,uint32_t train);
bool r2_gm7_precision_anchor(uint16_t handle,void *ull_header,uint32_t *ticker_anchor,uint32_t interval_us);
