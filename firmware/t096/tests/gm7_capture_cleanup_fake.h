/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define NRFX_SUCCESS 0x0bad0000u
struct fake_radio { uintptr_t PACKETPTR; };
extern struct fake_radio fake_radio;
#define NRF_RADIO (&fake_radio)
#define NRF_TIMER3 ((void *)2)
#define NRF_RADIO_EVENT_ADDRESS 1
#define NRF_RADIO_EVENT_END 2
#define NRF_TIMER_CC_CHANNEL1 1
#define NRF_TIMER_CC_CHANNEL5 5
typedef unsigned nrf_ppi_channel_t;
unsigned nrfx_ppi_channel_enable(unsigned);
unsigned nrfx_ppi_channel_disable(unsigned);
bool nrf_radio_event_check(void *,unsigned);
uint32_t nrf_timer_cc_get(void *,unsigned);
static inline unsigned irq_lock(void){return 0;}
static inline void irq_unlock(unsigned k){(void)k;}
