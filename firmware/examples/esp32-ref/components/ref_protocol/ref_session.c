// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_session.h"
#include "ref_timebase.h"

void ref_session_select(RefSession *s, uint64_t self, uint64_t peer, uint64_t boot, uint64_t nonce)
{ *s = (RefSession){peer, boot, self, nonce, 0, 0, false}; }
void ref_session_challenge(RefSession *s, uint64_t nonce)
{ s->nonce = nonce; s->nonce_used = false; }
bool ref_session_accept(RefSession *s, const RefFrame *f, uint64_t now)
{
    if (!s->self || !s->peer || !s->boot || !s->nonce || f->kind != REF_BEACON ||
        f->sender != s->peer || f->session != s->boot || f->publication <= s->publication) return false;
    s->publication = f->publication;
    if (!s->nonce_used && f->reply_to == s->self && f->nonce == s->nonce &&
        now <= UINT64_MAX - REF_MS_TICKS(6000)) {
        s->until_tick = now + REF_MS_TICKS(6000);
        s->nonce_used = true;
    }
    return s->until_tick && now < s->until_tick;
}
