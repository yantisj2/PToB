// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_SESSION_H
#define ESP32_REF_SESSION_H
#include "ref_wire.h"
typedef struct {
    uint64_t peer, boot, self, nonce, until_tick;
    uint32_t publication;
    bool nonce_used;
} RefSession;
void ref_session_select(RefSession *session, uint64_t self, uint64_t peer,
                         uint64_t boot, uint64_t fresh_nonce);
void ref_session_challenge(RefSession *session, uint64_t fresh_nonce);
// Call only after HMAC verification. An echoed nonce renews admission ONCE;
// repeated copies of that echo cannot extend the original six-second deadline.
bool ref_session_accept(RefSession *session, const RefFrame *frame, uint64_t now);
#endif
