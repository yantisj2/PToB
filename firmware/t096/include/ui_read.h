/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#pragma once
#include <stdbool.h>
enum r2_ui_read_owner {R2_UI_READ_GM=1,R2_UI_READ_RC=2};
/* Ordinary-thread release witness only. No callback inside a spinlock.
 * before=false is immediately before mutex unlock; after=true may decline
 * all remaining optional work. No callback conveys timing authority. */
typedef bool (*r2_ui_read_boundary_fn)(void *,unsigned owner,bool after);
