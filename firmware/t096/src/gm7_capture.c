/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_controller.h"
#include "radio_capture.h"
#include <errno.h>
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <nrfx_ppi.h>
#include <string.h>
#include <zephyr/irq.h>
static nrf_ppi_channel_t address_route, end_route;
static uint32_t enabled, capture_epoch, head, tail, overflows;
/* Two complete unchained AUX observations. Saturation fences positive work;
 * no overwrite, receive-time refresh, HCI/callback timestamp substitution. */
static struct r2_gm7_rx received[2];
static struct {
  void *owner;
  bool transmit, auxiliary, armed, full, address_enabled, end_enabled;
} active;
int r2_gm7_capture_enable(uint32_t epoch, unsigned address, unsigned end) {
  if (!epoch || address >= 32 || end >= 32 || address == end)
    return -EINVAL;
  if (enabled || active.armed)
    return -EBUSY;
  address_route = (nrf_ppi_channel_t)address;
  end_route = (nrf_ppi_channel_t)end;
  capture_epoch = epoch;
  head = tail = 0;
  __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
  return 0;
}
bool r2_gm7_capture_busy(void) { return active.armed; }
int r2_gm7_capture_disable(void) {
  __atomic_store_n(&enabled, 0, __ATOMIC_RELEASE);
  if (active.armed)
    return -EBUSY;
  head = tail = 0;
  return 0;
}
static bool disarm(void) {
  if (active.address_enabled &&
      nrfx_ppi_channel_disable(address_route) == NRFX_SUCCESS)
    active.address_enabled = false;
  if (active.end_enabled && nrfx_ppi_channel_disable(end_route) == NRFX_SUCCESS)
    active.end_enabled = false;
  active.armed = active.address_enabled || active.end_enabled;
  if (active.armed)
    active.full = false;
  return !active.armed;
}
bool r2_gm7_capture_arm(void *owner, bool transmit, bool auxiliary) {
  if ((!transmit && !r2_gm7_controller_receiving()) || !owner ||
      !__atomic_load_n(&enabled, __ATOMIC_ACQUIRE) || active.armed ||
      !r2_radio_capture_available())
    return false;
  active.owner = owner;
  active.transmit = transmit;
  active.auxiliary = auxiliary;
  active.full = false;
  if (nrfx_ppi_channel_enable(end_route) != NRFX_SUCCESS)
    return false;
  active.end_enabled = active.armed = true;
  if (nrfx_ppi_channel_enable(address_route) != NRFX_SUCCESS) {
    if (!disarm())
      r2_gm7_controller_cancel(0);
    return false;
  }
  active.address_enabled = active.full = true;
  return true;
}
void r2_gm7_capture_abort(void *owner) {
  if (!active.armed || active.owner != owner)
    return;
  if (active.transmit)
    r2_gm7_controller_capture(owner, active.auxiliary, 0, 0, capture_epoch,
                              false);
  (void)disarm();
}
void r2_gm7_capture_done(void *owner, bool done, bool crc, const void *pdu) {
  if (!active.armed || active.owner != owner)
    return;
  bool address = nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_ADDRESS);
  bool end = nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_END);
  uint32_t a = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL1),
           e = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL5);
  bool full = active.full;
  if (!disarm()) {
    r2_gm7_controller_cancel(0);
    return;
  }
  bool complete = full && done && address && end && (active.transmit || crc);
  if (active.transmit) {
    r2_gm7_controller_capture(owner, active.auxiliary, a, e, capture_epoch,
                              complete);
    return;
  }
  if (!complete || !pdu || !__atomic_load_n(&enabled, __ATOMIC_ACQUIRE))
    return;
  const uint8_t *ad;
  size_t length;
  uint8_t remote[6];
  const uint8_t *packet = pdu;
  if (gm7_air_received(packet, (size_t)packet[1] + 2, &ad, &length, remote))
    return;
  uint32_t h = __atomic_load_n(&head, __ATOMIC_RELAXED),
           t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
  if (h - t >= 2) {
    if (overflows < UINT32_MAX)
      ++overflows;
    r2_gm7_controller_cancel(0);
    return;
  }
  struct r2_gm7_rx *r = &received[h % 2];
  r->raw = e;
  r->epoch = capture_epoch;
  r->length = (uint16_t)length;
  memcpy(r->address, remote, 6);
  memcpy(r->ad, ad, length);
  __atomic_store_n(&head, h + 1, __ATOMIC_RELEASE);
}
bool r2_gm7_capture_pop(struct r2_gm7_rx *out) {
  if (!out)
    return false;
  uint32_t t = __atomic_load_n(&tail, __ATOMIC_RELAXED),
           h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
  if (t == h)
    return false;
  *out = received[t % 2];
  __atomic_store_n(&tail, t + 1, __ATOMIC_RELEASE);
  return true;
}
uint32_t r2_gm7_capture_overflows(void) {
  return __atomic_load_n(&overflows, __ATOMIC_ACQUIRE);
}

void r2_gm7_capture_receive_arm(const void *pdu) {
  if (pdu)
    (void)r2_gm7_capture_arm((void *)pdu, false, true);
}
