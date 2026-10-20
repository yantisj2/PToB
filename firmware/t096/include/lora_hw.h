/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define R2_LORA_HW_BYTES 128u
#define R2_LORA_HW_LAUNCH_MARGIN_US 1000u
enum r2_lora_hw_kind {R2_LORA_HW_NONE,R2_LORA_HW_RX_DONE,R2_LORA_HW_TX_DONE};
struct r2_lora_hw_event {
 uint64_t tick,capture_epoch,local_us;
 uint32_t generation;
 enum r2_lora_hw_kind kind;
 uint8_t length,frame[R2_LORA_HW_BYTES];
};
struct r2_lora_hw_ack {
 bool dma_retired,radio_quiescent,fem_off,capture_released,fault;
};
struct r2_lora_hw_diag {
 uint32_t rx_done,tx_done,crc_bad,overflow,spi_ok,spi_error,cutoffs;
 uint32_t invalid_capture;
 uint32_t capture_epoch,generation;
 int error;
 uint8_t phase;
};
/* One low-priority thread owns all calls except stop_begin and diag. The
 * packet engine returns to STBY_RC on the first received packet: the FIFO
 * and physical DIO1 capture remain frozen until take, then explicit rearm.
 * Buffers and descriptors have permanent lifetime. Faults require reboot.
 */
int r2_lora_hw_start(uint32_t generation,bool receive);
int r2_lora_hw_tx(const uint8_t *,size_t,uint32_t tx_generation);
/* Physical FEM bound for this length, zero for an invalid size. The timing
 * frame retains its original bound; a longer diagnostic has its own bound. */
uint32_t r2_lora_hw_budget_us(size_t);
/* Opportunistic submission: after all SPI preparation, recheck the original
 * finish deadline immediately before FEM enable. EAGAIN leaves the idle
 * owner healthy with no SetTx or FEM emission. Zero deadline is unbounded. */
int r2_lora_hw_tx_before(const uint8_t *,size_t,uint32_t,uint64_t latest_finish_us);
/* 0=no complete packet, 1=immutable event, negative=local radio fault. */
int r2_lora_hw_take(struct r2_lora_hw_event *);
int r2_lora_hw_rx_rearm(void);
/* Bounded, no wait/SPI, cancellation fence; immediately reset + inhibit FEM.
 * The worker must still poll actual retirement before declaring closure. */
void r2_lora_hw_stop_begin(uint64_t original_deadline_us);
void r2_lora_hw_stop_poll(struct r2_lora_hw_ack *);
void r2_lora_hw_diag(struct r2_lora_hw_diag *);
