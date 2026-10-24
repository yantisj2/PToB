/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Exact production native controller body, fake HAL/ticker/LLL edges only. */
#pragma once
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gm7_controller.h"
#include "gm7_clock.h"
#include "../protocol/gm7_phase.h"
#define BUILD_ASSERT(x) _Static_assert(x, #x)
#define LL_ASSERT(x) assert(x)
#define RADIO_TXP_DEFAULT 0
#define RADIO_PKT_CONF_LENGTH_8BIT 8
#define PDU_AC_PAYLOAD_SIZE_MAX 255
#define RADIO_PKT_CONF_PHY(x) (x)
#define PDU_AC_ACCESS_ADDR 0x8e89bed6u
#define PDU_CRC_POLYNOMIAL 0x65bu
#define PDU_AC_CRC_IV 0x555555u
#define HAL_TICKER_CNTR_MASK 0xffffffu
#define HAL_TICKER_US_TO_TICKS(x) ((uint32_t)(x))
#define HAL_TICKER_US_TO_TICKS_CEIL(x) ((uint32_t)(x))
#define HAL_TICKER_TICKS_TO_US(x) ((uint32_t)(x))
#define HAL_TICKER_TICKS_TO_US_64BIT(x) ((uint64_t)(x))
#define EVENT_OVERHEAD_START_US 100u
#define EVENT_OVERHEAD_END_US 100u
#define EVENT_OVERHEAD_XTAL_US 500u
#define EVENT_OVERHEAD_PREEMPT_MIN_US 100u
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_BUSY 1
#define TICKER_STATUS_FAILURE 2
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_USER_ID_THREAD 0
#define TICKER_USER_ID_ULL_HIGH 1
#define TICKER_USER_ID_LLL 2
#define TICKER_ID_MAX 100
struct ull_hdr {
  uint32_t ticks_prepare_to_start, ticks_preempt_to_start, ticks_slot, refs;
};
struct lll_hdr {
  void *parent;
};
struct lll_prepare_param {
  uint32_t ticks_at_expire, remainder;
  uint16_t lazy;
  uint8_t force;
  void *param;
};
typedef int (*lll_prepare_cb_t)(struct lll_prepare_param *);
typedef struct {
  int unused;
} memq_link_t;
struct mayfly {
  uint8_t req, ack;
  memq_link_t *link;
  void *param;
  void (*fp)(void *);
};
static uint64_t fake_now = 1000000;
static uint32_t fake_epoch = 4;
static uint32_t fake_ticker_now = 1000000,
                fake_ticker_status = TICKER_STATUS_SUCCESS;
static void (*fake_isr)(void *);
static void *fake_isr_param;
static void *fake_tx_pdu;
static bool fake_clock_valid = true, fake_done = true, fake_capture_armed,
            fake_capture_busy;
static unsigned fake_phy, fake_channel, fake_disabled, fake_hf_refs,
    fake_cleanup, fake_arms;
static struct mayfly *fake_mayfly;
static uint32_t sys_cpu_to_le32(uint32_t x) { return x; }
static void ull_hdr_init(struct ull_hdr *u) { memset(u, 0, sizeof *u); }
static void lll_hdr_init(void *l, void *p) {
  ((struct lll_hdr *)l)->parent = p;
}
static uint32_t lll_event_offset_get(struct ull_hdr *u) {
  return u->ticks_prepare_to_start;
}
static void ull_ref_inc(struct ull_hdr *u) { ++u->refs; }
static unsigned ull_ref_get(struct ull_hdr *u) { return u->refs; }
static int lll_hfclock_on(void) {
  ++fake_hf_refs;
  return 0;
}
static int lll_hfclock_off(void) {
  assert(fake_hf_refs);
  --fake_hf_refs;
  return 0;
}
static int lll_is_abort_cb(void *a, void *b, lll_prepare_cb_t *c) {
  (void)a;
  (void)b;
  (void)c;
  return 0;
}
static int lll_prepare(int (*a)(void *, void *, lll_prepare_cb_t *),
                       void (*b)(struct lll_prepare_param *, void *),
                       int (*c)(struct lll_prepare_param *), int p, void *v) {
  (void)a;
  (void)b;
  (void)p;
  return c(v);
}
static void lll_done(void *p) { (void)p; }
static int lll_prepare_done(void *p) {
  (void)p;
  return 0;
}
static void lll_isr_status_reset(void) {}
static void lll_isr_tx_status_reset(void) {}
static int ull_disable(void *l) {
  struct ull_hdr *u = ((struct lll_hdr *)l)->parent;
  u->refs = 0;
  return 0;
}
static void lll_isr_cleanup(void *l) {
  struct ull_hdr *u = ((struct lll_hdr *)l)->parent;
  u->refs = 0;
  ++fake_cleanup;
  assert(fake_hf_refs);
  --fake_hf_refs;
}
static uint32_t mayfly_enqueue(unsigned a, unsigned b, unsigned c,
                               struct mayfly *m) {
  (void)a;
  (void)b;
  (void)c;
  fake_mayfly = m;
  return 0;
}
static uint32_t ticker_ticks_now_get(void) { return fake_ticker_now; }
static void ull_ticker_status_give(uint32_t status, void *p) {
  *(volatile uint32_t *)p = status;
}
static uint32_t ull_ticker_status_take(uint32_t status, volatile uint32_t *p) {
  (void)p;
  return status;
}
static uint32_t
ticker_start(unsigned a, unsigned b, unsigned c, uint32_t d, uint32_t e,
             uint32_t f, uint32_t g, uint32_t h, uint32_t i,
             void (*j)(uint32_t, uint32_t, uint32_t, uint16_t, uint8_t, void *),
             void *k, void (*l)(uint32_t, void *), void *m) {
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  (void)e;
  (void)f;
  (void)g;
  (void)h;
  (void)i;
  (void)j;
  (void)k;
  (void)l;
  (void)m;
  return fake_ticker_status;
}
static uint32_t ticker_stop(unsigned a, unsigned b, unsigned c,
                            void (*d)(uint32_t, void *), void *e) {
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  (void)e;
  return TICKER_STATUS_SUCCESS;
}
static void radio_isr_set(void (*f)(void *), void *p) {
  fake_isr = f;
  fake_isr_param = p;
}
static void radio_disable(void) { ++fake_disabled; }
static void radio_reset(void) {}
static void radio_tx_power_set(unsigned p) { (void)p; }
static void radio_phy_set(unsigned p, unsigned f) {
  (void)f;
  fake_phy = p;
}
static void radio_pkt_configure(unsigned a, unsigned b, unsigned c) {
  (void)a;
  (void)b;
  (void)c;
}
static void radio_aa_set(uint8_t *a) { (void)a; }
static void radio_crc_configure(uint32_t a, uint32_t b) {
  (void)a;
  (void)b;
}
static void lll_chan_set(unsigned c) { fake_channel = c; }
static void radio_pkt_tx_set(void *p) { fake_tx_pdu = p; }
static void radio_tmr_tifs_set(unsigned n) { (void)n; }
static void radio_switch_complete_and_b2b_tx(unsigned a, unsigned b, unsigned c,
                                             unsigned d) {
  (void)a;
  (void)b;
  (void)c;
  (void)d;
}
static void radio_switch_complete_and_b2b_tx_disable(void) {}
static uint32_t radio_tmr_start(unsigned a, uint32_t b, uint32_t c) {
  (void)a;
  (void)b;
  (void)c;
  return 0;
}
static void radio_tmr_end_capture(void) {}
static bool radio_is_done(void) { return fake_done; }
static unsigned radio_tx_ready_delay_get(unsigned p, unsigned f) {
  (void)p;
  (void)f;
  return 40;
}
static unsigned radio_tx_chain_delay_get(unsigned p, unsigned f) {
  (void)p;
  (void)f;
  return 1;
}
