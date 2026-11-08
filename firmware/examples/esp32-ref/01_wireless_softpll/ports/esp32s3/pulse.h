// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_PULSE_H
#define ESP32_REF_PULSE_H
#include "ref_clock.h"
bool ref_pulse_begin(void);
void ref_pulse_model(const RefClock *clock, bool qualified);
void ref_pulse_withdraw(void);
bool ref_pulse_stop(void);
#endif
