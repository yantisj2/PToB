// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_DIRECT_WIRE_H
#define PTOB_RX_DIRECT_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PTOB_GR3_BYTES = 190, PTOB_GM_SID = 12, PTOB_JOIN_BYTES = 168 };
typedef struct {
    uint8_t psk[32];
    uint32_t epoch;
} PtobTrust;
typedef struct {
    uint64_t mesh, root;
    uint8_t address[6]; // NimBLE byte order.
} PtobLocator;
typedef struct {
    uint64_t ticks;
    uint32_t publication;
    uint16_t event;
    uint8_t channel;
} PtobCorrection;
typedef struct {
    uint64_t created_ns, expires_ns, utc_offset_ns;
    uint64_t boot;
    uint32_t epoch, train, publication, capture_epoch;
    PtobCorrection correction[2];
    uint8_t count;
} PtobFrame;

typedef struct {
    uint64_t client, boot, nonce;
    uint64_t request_us, proof_us, peer_boot, peer_nonce, challenge_us, peer_sequence;
} PtobJoin;

typedef struct {
    uint64_t utc_ns, received_us, bootstrap_until_us, boot;
    uint32_t uncertainty_us, epoch, train;
} PtobGmAnchor;

// The crypto provider writes a 32-byte HMAC-SHA256 result. Called in task context.
bool ptob_rx_hmac(const uint8_t key[32], const uint8_t *data, size_t len, uint8_t mac[32]);
bool ptob_locator(const PtobTrust *trust, const uint8_t *ad, size_t len,
                  const uint8_t address[6], PtobLocator *out);
bool ptob_gr3_read(const PtobTrust *trust, const PtobLocator *gm,
                   const uint8_t *ad, size_t len, PtobFrame *out);
bool ptob_join_address(const PtobTrust *trust, uint64_t mesh, uint64_t id, uint8_t out[6]);
bool ptob_join_send(const PtobTrust *trust, const PtobLocator *gm, PtobJoin *join,
                    uint64_t now_us, uint8_t out[PTOB_JOIN_BYTES]);
bool ptob_join_challenge(const PtobTrust *trust, const PtobLocator *gm, PtobJoin *join,
                         const uint8_t wire[PTOB_JOIN_BYTES]);
bool ptob_join_accept(const PtobTrust *trust, const PtobLocator *gm, const PtobJoin *join,
                      const uint8_t wire[PTOB_JOIN_BYTES], uint64_t now_us, PtobGmAnchor *out);
#endif
