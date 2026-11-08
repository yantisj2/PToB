// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_wire.h"
#include <string.h>

static const uint8_t UUID[16] = {
    0x31,0x8b,0x78,0xcd,0xb6,0xe1,0x4f,0x3a,0x9e,0x59,0x7d,0x0c,0x01,0x52,0x46,0xe0
};
static void put(uint8_t *p, uint64_t value, unsigned n)
{ for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(value >> (8 * i)); }
static uint64_t get(const uint8_t *p, unsigned n)
{ uint64_t value = 0; for (unsigned i = 0; i < n; ++i) value |= (uint64_t)p[i] << (8 * i); return value; }

bool ref_wire_shape(const uint8_t *p, size_t n)
{
    return p && n == REF_WIRE_BYTES && p[0] == 189 && p[1] == 0x21 &&
        memcmp(p + 2, UUID, 16) == 0 && memcmp(p + 18, "REF1", 4) == 0 && p[22] == 1;
}

bool ref_wire_encode(const RefFrame *f, RefSign sign, uint8_t out[REF_WIRE_BYTES])
{
    if (!f || !sign || !out || !f->sender || !f->session || !f->publication ||
        (f->kind != REF_BEACON && f->kind != REF_CHALLENGE) || f->count > 2 ||
        (f->kind == REF_CHALLENGE && (f->count || !f->nonce || f->reply_to))) return false;
    uint8_t p[REF_WIRE_BYTES] = {0};
    p[0] = 189; p[1] = 0x21; memcpy(p + 2, UUID, 16); memcpy(p + 18, "REF1", 4);
    p[22] = 1; p[23] = f->kind; p[25] = f->count;
    put(p + 26, f->sender, 8); put(p + 34, f->session, 8); put(p + 42, f->publication, 4);
    put(p + 46, f->reply_to, 8); put(p + 54, f->nonce, 8);
    for (unsigned i = 0; i < f->count; ++i) {
        const RefCorrection *c = &f->corrections[i];
        if (!c->publication || c->publication >= f->publication || c->channel > 36 ||
            c->estimated_tx != 1 || c->time_ns < 0) return false;
        uint8_t *q = p + 62 + 16 * i;
        put(q, c->publication, 4); put(q + 4, c->event, 2);
        q[6] = c->channel; q[7] = c->estimated_tx; put(q + 8, (uint64_t)c->time_ns, 8);
    }
    if (!sign(p, REF_WIRE_AUTH_BYTES, p + REF_WIRE_AUTH_BYTES)) return false;
    memcpy(out, p, sizeof(p));
    return true;
}

bool ref_wire_decode(const uint8_t *p, size_t n, RefSign sign, RefFrame *frame)
{
    if (!frame || !sign || !ref_wire_shape(p, n)) return false;
    uint8_t expected[16];
    if (!sign(p, REF_WIRE_AUTH_BYTES, expected)) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 16; ++i) difference |= expected[i] ^ p[REF_WIRE_AUTH_BYTES + i];
    if (difference || p[24] || p[25] > 2) return false;
    RefFrame f = {0};
    f.kind = p[23]; f.count = p[25]; f.sender = get(p + 26, 8); f.session = get(p + 34, 8);
    f.publication = (uint32_t)get(p + 42, 4); f.reply_to = get(p + 46, 8); f.nonce = get(p + 54, 8);
    if (!f.sender || !f.session || !f.publication ||
        (f.kind != REF_BEACON && f.kind != REF_CHALLENGE) ||
        (f.kind == REF_CHALLENGE && (f.count || !f.nonce || f.reply_to))) return false;
    for (unsigned i = 0; i < f.count; ++i) {
        const uint8_t *q = p + 62 + 16 * i;
        const uint64_t time = get(q + 8, 8);
        f.corrections[i] = (RefCorrection){(uint32_t)get(q, 4), (uint16_t)get(q + 4, 2), q[6], q[7], 0};
        if (!f.corrections[i].publication || f.corrections[i].publication >= f.publication ||
            q[6] > 36 || q[7] != 1 || time > INT64_MAX) return false;
        f.corrections[i].time_ns = (int64_t)time;
    }
    // Unused correction slots and reserved bytes are canonical zeros.
    for (size_t i = 62 + 16 * f.count; i < REF_WIRE_AUTH_BYTES; ++i) if (p[i]) return false;
    *frame = f;
    return true;
}
