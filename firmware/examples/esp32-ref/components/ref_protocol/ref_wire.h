// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_WIRE_H
#define ESP32_REF_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One constant-size Service Data AD record. UUID identifies the example family,
// never the selected local mesh. The packet has no UTC, GNSS or NTP fields.
#define REF_WIRE_BYTES 190
#define REF_WIRE_AUTH_BYTES 174
#define REF_WIRE_SID 3
enum { REF_BEACON = 1, REF_CHALLENGE = 2 };
typedef struct {
    uint32_t publication;
    uint16_t event;
    uint8_t channel;
    uint8_t estimated_tx;
    int64_t time_ns;
} RefCorrection;
typedef struct {
    uint8_t kind, count;
    uint64_t sender, session;
    uint32_t publication;
    uint64_t reply_to, nonce;
    RefCorrection corrections[2];
} RefFrame;

// The port supplies HMAC-SHA256 truncated to 16 bytes. Tests may use a clearly
// labeled fixture callback; production uses the SDK cryptographic library.
typedef bool (*RefSign)(const uint8_t *data, size_t length, uint8_t tag[16]);
bool ref_wire_encode(const RefFrame *frame, RefSign sign, uint8_t out[REF_WIRE_BYTES]);
bool ref_wire_decode(const uint8_t *bytes, size_t length, RefSign sign, RefFrame *frame);
bool ref_wire_shape(const uint8_t *bytes, size_t length);

#endif
