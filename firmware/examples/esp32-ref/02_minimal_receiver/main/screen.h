// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_SCREEN_H
#define PTOB_RX_SCREEN_H
#include "ptob_rx.h"
typedef struct {
    PtobRxStatus receiver;
    const char *alias, *source;
    unsigned tier;
    bool info, power_off, released, power_fault;
} ReceiverScreen;
void receiver_screen(const ReceiverScreen *screen, uint8_t fb[1024]);
#endif
