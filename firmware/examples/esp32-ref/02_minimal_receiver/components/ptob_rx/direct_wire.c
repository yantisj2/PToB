// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "direct_wire.h"
#include <string.h>

static uint64_t read_le(const uint8_t *p, size_t size)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static void write_le(uint8_t *p, size_t size, uint64_t value)
{
    for (size_t i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

// The mesh ID is discovered on air and included in the MAC context.
// A locator can trigger an exchange. It grants no time or permission.
static bool sign(const PtobTrust *trust, uint64_t mesh, const char domain[8],
                 const uint8_t *body, size_t len, uint8_t tag[16])
{
    uint8_t input[21 + 174], mac[32];
    if (!mesh || !trust->epoch || len > 174) return false;
    memcpy(input, domain, 8);
    input[8] = 1;
    write_le(input + 9, 8, mesh);
    write_le(input + 17, 4, trust->epoch);
    memcpy(input + 21, body, len);
    if (!ptob_rx_hmac(trust->psk, input, 21 + len, mac)) return false;
    memcpy(tag, mac, 16);
    return true;
}

static bool authentic(const PtobTrust *trust, uint64_t mesh, const char domain[8],
                      const uint8_t *body, size_t len, const uint8_t tag[16])
{
    uint8_t wanted[16], different = 0;
    if (!sign(trust, mesh, domain, body, len, wanted)) return false;
    for (size_t i = 0; i < sizeof wanted; ++i) different |= wanted[i] ^ tag[i];
    return different == 0;
}

bool ptob_join_address(const PtobTrust *trust, uint64_t mesh, uint64_t id, uint8_t out[6])
{
    uint8_t ids[16], tag[16];
    write_le(ids, 8, id);
    write_le(ids + 8, 8, id);
    if (!id || !sign(trust, mesh, "PTOBG1K1", ids, sizeof ids, tag)) return false;
    memcpy(out, tag, 6);
    out[5] |= 0xc0;
    return true;
}

bool ptob_locator(const PtobTrust *trust, const uint8_t *ad, size_t len,
                  const uint8_t address[6], PtobLocator *out)
{
    for (size_t at = 0; at < len;) {
        const uint8_t size = ad[at];
        if (!size || size >= len - at) return false;
        const uint8_t *field = ad + at + 2;
        if (size == 24 && ad[at + 1] == 255 && field[0] == 'G' && field[1] == '1') {
            if ((field[2] & 127) != 1 || read_le(field + 11, 4) != trust->epoch) return false;
            PtobLocator candidate = {.mesh = read_le(field + 3, 8), .root = read_le(field + 15, 8)};
            if (!ptob_join_address(trust, candidate.mesh, candidate.root, candidate.address))
                return false;
            if (memcmp(candidate.address, address, 6)) return false;
            *out = candidate;
            return true;
        }
        at += (size_t)size + 1;
    }
    return false;
}

// JOIN5 is a bounded exchange for coarse UTC. GR3 still supplies every phase
// observation. The fixed offsets follow the public JOIN and TZ wire contracts.
enum { JOIN_REQUEST = 1, JOIN_CHALLENGE = 2, JOIN_PROOF = 3, JOIN_ACCEPT = 4, JOIN_TAG = 152 };

bool ptob_join_send(const PtobTrust *trust, const PtobLocator *gm, PtobJoin *join,
                    uint64_t now_us, uint8_t out[PTOB_JOIN_BYTES])
{
    const bool proof = join->peer_nonce != 0;
    if (!now_us || !join->client || join->client == gm->root || !join->boot || !join->nonce ||
        (proof ? now_us <= join->request_us || join->proof_us : join->request_us)) return false;
    memset(out, 0, PTOB_JOIN_BYTES);
    out[0] = 14;
    out[1] = proof ? JOIN_PROOF : JOIN_REQUEST;
    out[2] = 5;
    write_le(out + 4, 4, trust->epoch);
    write_le(out + 8, 8, join->client);
    write_le(out + 16, 8, gm->root);
    write_le(out + 24, 8, join->boot);
    write_le(out + 32, 8, join->peer_boot);
    write_le(out + 40, 8, join->nonce);
    write_le(out + 48, 8, join->peer_nonce);
    write_le(out + 56, 8, proof ? 2 : 1);
    write_le(out + 64, 8, now_us);
    write_le(out + 72, 8, join->challenge_us);
    out[117] = 9;
    memcpy(out + 118, "ESP32-REF", 9);
    out[151] = 0x80; // Unknown time zone, UTC stays primary.
    if (!sign(trust, gm->mesh, "PTOBTZJ5", out, JOIN_TAG, out + JOIN_TAG)) return false;
    if (proof) join->proof_us = now_us;
    else join->request_us = now_us;
    return true;
}

static bool join_reply(const PtobTrust *trust, const PtobLocator *gm, const PtobJoin *join,
                       const uint8_t *wire, uint8_t kind, uint64_t echo_us)
{
    return echo_us && wire[0] == 14 && wire[1] == kind && wire[2] == 5 && wire[3] <= 7 &&
        read_le(wire + 4, 4) == trust->epoch && read_le(wire + 8, 8) == gm->root &&
        read_le(wire + 16, 8) == join->client && read_le(wire + 24, 8) &&
        read_le(wire + 32, 8) == join->boot && read_le(wire + 40, 8) == join->nonce &&
        read_le(wire + 48, 8) && read_le(wire + 56, 8) > join->peer_sequence &&
        read_le(wire + 64, 8) && read_le(wire + 72, 8) == echo_us && !wire[142] && !wire[143] &&
        authentic(trust, gm->mesh, "PTOBTZJ5", wire, JOIN_TAG, wire + JOIN_TAG);
}

bool ptob_join_challenge(const PtobTrust *trust, const PtobLocator *gm, PtobJoin *join,
                         const uint8_t wire[PTOB_JOIN_BYTES])
{
    if (join->peer_nonce || wire[116] || !join_reply(trust, gm, join, wire, JOIN_CHALLENGE, join->request_us))
        return false;
    join->peer_boot = read_le(wire + 24, 8);
    join->peer_nonce = read_le(wire + 48, 8);
    join->peer_sequence = read_le(wire + 56, 8);
    join->challenge_us = read_le(wire + 64, 8);
    return true;
}

bool ptob_join_accept(const PtobTrust *trust, const PtobLocator *gm, const PtobJoin *join,
                      const uint8_t wire[PTOB_JOIN_BYTES], uint64_t now_us, PtobGmAnchor *out)
{
    if (now_us < join->proof_us || !join_reply(trust, gm, join, wire, JOIN_ACCEPT, join->proof_us) ||
        read_le(wire + 24, 8) != join->peer_boot || read_le(wire + 48, 8) != join->peer_nonce ||
        read_le(wire + 64, 8) < join->challenge_us ||
        wire[116] != 1 || read_le(wire + 80, 8) != gm->root || read_le(wire + 88, 8) != join->peer_boot)
        return false;
    const uint64_t round_us = now_us - join->proof_us;
    const uint64_t utc_us = read_le(wire + 104, 8);
    const uint32_t remaining = (uint32_t)read_le(wire + 112, 4);
    const uint32_t bound = (uint32_t)read_le(wire + 144, 4);
    const uint32_t epoch = (uint32_t)read_le(wire + 96, 4), train = (uint32_t)read_le(wire + 100, 4);
    if (round_us >= 250000 || utc_us < UINT64_C(946684800000000) || utc_us >= UINT64_C(4102444800000000) ||
        !epoch || !train || !bound || bound > 50000 || remaining > 1500000) return false;
    // The GM sampled UTC during the causal round trip. Use half the round for
    // its age estimate and subtract the whole round from source permission.
    const uint32_t margin = (uint32_t)((round_us + 499) / 500) + 64;
    const uint64_t guard = round_us + bound + margin;
    if (remaining <= guard || now_us > UINT64_MAX - remaining) return false;
    *out = (PtobGmAnchor){.utc_ns = (utc_us + round_us / 2) * 1000,
        .received_us = now_us, .bootstrap_until_us = now_us + remaining - guard,
        .boot = join->peer_boot, .uncertainty_us = (uint32_t)((round_us + 1) / 2) + bound + margin,
        .epoch = epoch, .train = train};
    return true;
}

bool ptob_gr3_read(const PtobTrust *trust, const PtobLocator *gm,
                   const uint8_t *w, size_t len, PtobFrame *out)
{
    if (len != PTOB_GR3_BYTES || w[0] != 189 || w[1] != 255 || w[2] != 255 || w[3] != 255 ||
        w[4] != 'G' || w[5] != 'R' || w[6] != 3 || w[7] > 2 || read_le(w + 8, 2) != 50 ||
        read_le(w + 10, 4) != trust->epoch || read_le(w + 14, 8) != gm->root ||
        read_le(w + 22, 8) || !read_le(w + 30, 8) ||
        !read_le(w + 38, 4) || read_le(w + 90, 8) != gm->root ||
        read_le(w + 98, 8) != gm->root || w[106] || w[107] ||
        read_le(w + 108, 8) != read_le(w + 30, 8) || !read_le(w + 116, 4) ||
        read_le(w + 128, 4) != read_le(w + 116, 4)) return false;
    if (!authentic(trust, gm->mesh, "PTOBG3O3", w + 14, 60, w + 74) ||
        !authentic(trust, gm->mesh, "PTOBG3R3", w, 174, w + 174)) return false;

    PtobFrame f = {.created_ns = read_le(w + 42, 8), .expires_ns = read_le(w + 50, 8),
        .utc_offset_ns = read_le(w + 66, 8), .boot = read_le(w + 30, 8),
        .epoch = (uint32_t)read_le(w + 38, 4), .train = (uint32_t)read_le(w + 116, 4),
        .publication = (uint32_t)read_le(w + 120, 4),
        .capture_epoch = (uint32_t)read_le(w + 124, 4), .count = w[7]};
    const uint64_t frame = read_le(w + 58, 8);
    if (!f.publication || !f.capture_epoch || f.expires_ns <= f.created_ns ||
        f.expires_ns > INT64_MAX || f.expires_ns - f.created_ns > 1650000000 ||
        !f.created_ns || frame > f.created_ns || (!frame && f.count) ||
        f.utc_offset_ns > INT64_MAX - f.expires_ns) return false;
    for (unsigned i = 0; i < f.count; ++i) {
        const uint8_t *c = w + 136 + 19 * i;
        f.correction[i] = (PtobCorrection){.publication = (uint32_t)read_le(c, 4),
            .event = (uint16_t)read_le(c + 4, 2), .channel = c[6], .ticks = read_le(c + 7, 8)};
        if (!f.correction[i].publication || c[6] > 36 || read_le(c + 15, 4) != f.train || !f.correction[i].ticks ||
            f.correction[i].ticks > (uint64_t)INT64_MAX / 125) return false;
        const uint64_t at = f.correction[i].ticks * 125 / 2;
        if (at < f.created_ns || at >= f.expires_ns) return false;
    }
    for (unsigned i = f.count * 19; i < 38; ++i)
        if (w[136 + i]) return false;
    *out = f;
    return true;
}
