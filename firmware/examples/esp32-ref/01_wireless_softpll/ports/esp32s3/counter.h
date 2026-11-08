// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_COUNTER_PORT_H
#define ESP32_REF_COUNTER_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool ref_counter_port_begin(void);
bool ref_counter_port_now(uint64_t *tick);
bool ref_counter_port_stop(void);

#endif
