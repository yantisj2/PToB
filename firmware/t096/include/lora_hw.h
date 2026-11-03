/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define R2_LORA_HW_BYTES 128u
#define R2_LORA_HW_LAUNCH_MARGIN_US 1000u
enum r2_lora_hw_kind {R2_LORA_HW_NONE,R2_LORA_HW_RX_DONE,R2_LORA_HW_TX_DONE};
/* kind reports validated completion; tick is TX BUSY preamble-start or RX
 * first HeaderValid, NOT the completion or callback time. */
struct r2_lora_hw_event {
 uint64_t tick,capture_epoch,local_us;
 uint32_t generation;
 enum r2_lora_hw_kind kind;
 int16_t rssi_half_dbm;
 int8_t snr_quarter_db;
 bool packet_status_valid;
 uint8_t length,frame[R2_LORA_HW_BYTES];
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
 /* TX DIO TxDone -> TIMER4 CAPTURE1, independent of callback service.
  * Relative to FEM timer enable, not a precision UTC/capture timestamp. */
 uint32_t end_since_enable_us;
 uint64_t end_local_us; /* Captured TxDone projected onto coarse monotonic time. */
#endif
};
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
enum r2_lora_passive_phy {R2_LORA_DTS=1,R2_LORA_FHSS=2
#ifdef R2_LORA_BW_EXPERIMENT
 ,R2_LORA_FHSS_BW500=3
#endif
};
/* Idle-only retune. DTS uses captured RxDone; FHSS keeps HeaderValid.
 * No power/FEM setting changes and no implicit transmit/rearm. */
int r2_lora_hw_passive_configure(enum r2_lora_passive_phy,unsigned channel);
/* Freeze the selected source's original capture-clock endpoint. Recheck its
 * epoch and whole frame/cutoff budget at command commit; never renew a staged
 * transaction from a later map. Ordinary unguarded TX is disabled here. */
int r2_lora_hw_tx_before_source(const uint8_t *,size_t,uint32_t,uint64_t latest_finish_us,
                              uint64_t source_until_tick,uint64_t source_capture_epoch);
/* Stage the packet in standby before its due time. No capture arm, SetTx or
 * FEM enable occurs here. Launch consumes that exact preparation once and
 * freshly validates source/finish bounds before arming the physical event.
 * A known stop/replaced incarnation cancels with ECANCELED; an invalid token
 * while healthy stays ESTALE. Actual hardware faults take precedence. */
int r2_lora_hw_prepare(const uint8_t *,size_t,uint32_t);
int r2_lora_hw_launch_before_source(uint32_t,uint64_t latest_finish_us,
                                  uint64_t source_until_tick,uint64_t source_capture_epoch);
/* Retained last rejection and per-cause counts; later cap refusals do not
 * erase it. no_rf describes that rejection; no_rf() separately exposes
 * synchronous proof only for the most recent call/gen.
 * Callback source/finish fences occur after SetTx bytes and never refund. */
enum r2_lora_reject_reason {R2_LORA_REJECT_SOURCE=1,R2_LORA_REJECT_FINISH=2};
struct r2_lora_hw_rejection {
 uint64_t at_us,until_tick,finish_us;
 uint32_t available_us,required_us,finish_room_us,source_count,finish_count,generation;
 uint8_t reason,point;bool no_rf;
};
/* Capture-only preflight, no SPI/RF. earliest_us is the planned start. */
int r2_lora_hw_source_preflight(uint32_t budget,uint32_t generation,uint64_t finish_us,
                              uint64_t until_tick,uint64_t epoch,uint64_t earliest_us);
void r2_lora_hw_rejection(struct r2_lora_hw_rejection *);
bool r2_lora_hw_no_rf(uint32_t generation);
#endif
struct r2_lora_hw_ack {
 bool dma_retired,radio_quiescent,fem_off,capture_released,fault;
};
struct r2_lora_hw_diag {
 uint32_t rx_done,tx_done,crc_bad,overflow,spi_ok,spi_error,cutoffs;
 uint32_t invalid_capture,launch_aborts;
 uint32_t capture_epoch,generation;
 int error;
 uint8_t phase,last_busy_op;
 uint16_t busy_aborts;
};
_Static_assert(sizeof(struct r2_lora_hw_diag)==52,"LoRa diagnostics reserve");
/* One low-priority thread owns all calls except stop_begin and diag. The
 * packet engine returns to STBY_RC on the first received packet. RX first
 * HeaderValid stays asserted/captured until same-packet CRC/RxDone; TX BUSY
 * is captured once and later TxDone validates it. FIFO remains frozen until
 * take, then explicit rearm.
 * Buffers and descriptors have permanent lifetime. Transport faults require
 * the durable watchdog's MCU reset; they never reuse an unretired DMA owner.
 */
int r2_lora_hw_start(uint32_t generation,bool receive);
int r2_lora_hw_tx(const uint8_t *,size_t,uint32_t tx_generation);
/* Physical FEM bound for this length, zero for an invalid size. The timing
 * frame retains its original bound; a longer diagnostic has its own bound. */
uint32_t r2_lora_hw_budget_us(size_t);
/* Opportunistic submission: after all SPI preparation, recheck the original
 * finish deadline immediately before FEM enable. EAGAIN leaves the idle
 * owner healthy with no SetTx or FEM emission. Zero deadline is unbounded.
 * EINTR means a reset-aborted software launch that MAY have transmitted:
 * consume its sequence and complete bounded stop/retirement before restart. */
int r2_lora_hw_tx_before(const uint8_t *,size_t,uint32_t,uint64_t latest_finish_us);
/* 0=no complete packet, 1=immutable event, negative=local radio fault. */
int r2_lora_hw_take(struct r2_lora_hw_event *);
/* Read-only physical edge, not a DONE/cause claim. A source publisher may
 * preserve its original map projection before turnover; only take() can
 * subsequently validate packet completion. No SPI, IRQ clear or rearm. */
struct r2_lora_hw_edge {uint64_t tick,capture_epoch;uint32_t generation;};
bool r2_lora_hw_tx_edge(struct r2_lora_hw_edge *);
/* Same provisional read for request ingress; take() alone proves RX_DONE. */
bool r2_lora_hw_rx_edge(struct r2_lora_hw_edge *);
int r2_lora_hw_rx_rearm(void);
/* Retire an idle RX window without releasing capture ownership. A latched
 * event wins and must be taken first. No active TX/DMA can be interrupted. */
int r2_lora_hw_idle(void);
/* Bounded, no wait/SPI, cancellation fence; immediately reset + inhibit FEM.
 * The worker must still poll actual retirement before declaring closure. */
void r2_lora_hw_stop_begin(uint64_t original_deadline_us);
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *);
void r2_lora_hw_diag(struct r2_lora_hw_diag *);
/* Out-of-band worker-stall fence; records before reset/FEM inhibition. */
void r2_lora_hw_watchdog_trip(int error);
