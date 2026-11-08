// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_RADIO_H
#define ESP32_REF_RADIO_H
#include "ref_wire.h"
#include "radio_math.h"
#include "counter.h"

typedef struct {
    uint8_t bytes[REF_WIRE_BYTES];
    uint64_t tick, available_tick;
    uint32_t generation;
    uint16_t event;
    uint8_t channel;
    bool transmit;
} RefRadioEvent;
bool ref_radio_port_install(void);
bool ref_radio_port_stop(void);
void ref_radio_port_scope(void);
bool ref_radio_port_pop(RefRadioEvent *event);
bool ref_radio_port_fault(void);
uint32_t ref_radio_port_dropped(void);
uint32_t ref_radio_port_generation(void);
#endif
