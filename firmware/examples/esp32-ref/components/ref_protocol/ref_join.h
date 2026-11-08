// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_JOIN_H
#define ESP32_REF_JOIN_H
#include "ref_softpll.h"
#include "ref_wire.h"

typedef struct {
    uint64_t tick;
    uint32_t publication;
    uint16_t event;
    uint8_t channel;
    bool occupied, consumed;
    RefPrediction prediction;
} RefOriginal;
typedef struct { RefOriginal rows[16]; uint32_t missing, duplicate, full; } RefJoin;
bool ref_join_original(RefJoin *join, uint32_t publication, uint16_t event,
                       uint8_t channel, uint64_t tick, RefPrediction prediction);
bool ref_join_correction(RefJoin *join, const RefCorrection *correction,
                         uint64_t now, RefOriginal *original);
#endif
