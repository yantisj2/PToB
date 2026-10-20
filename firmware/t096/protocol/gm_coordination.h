/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
/* A negative election constraint, never a clock, source permit, membership or
 * HOT grant. Boot zero means authenticated timing with unresolved companion.
 * until is the ORIGINAL local physical-event hold, never query/metadata time. */
struct r2_gm_coordination {uint64_t root,boot,until;uint32_t epoch;bool direct;};
static inline bool r2_gm_coordination_live(const struct r2_gm_coordination *c,uint64_t self,uint64_t now)
{return c&&c->root&&c->root!=self&&c->epoch&&now<c->until;}
static inline bool r2_gm_coordination_matches(const struct r2_gm_coordination *c,uint64_t root,uint64_t boot,uint32_t epoch)
{return c&&c->root==root&&(!c->boot||c->boot==boot)&&c->epoch==epoch;}
