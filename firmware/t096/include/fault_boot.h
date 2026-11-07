/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
struct r2_identity;
#ifdef R2_FAULT_DURABLE
/* One startup invocation only; before LoRa/platform/USB/BLE services start.
 * Uses the existing diagnostic512B package, then seals all write callbacks. */
void r2_fault_boot(const struct r2_identity *,uint32_t package[128]);
int r2_fault_cpu_result(void);
int r2_fault_watchdog_result(void);
unsigned r2_fault_cpu_slot(void);
unsigned r2_fault_watchdog_slot(void);
/* Explicit read-only128x64B selection; no writer, erase or address parameter. */
int r2_fault_flash_read(unsigned chunk,uint8_t out[64]);
#endif
