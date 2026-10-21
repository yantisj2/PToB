/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Existing first-fault boundary only; no controller calls or recurring work.
 */
#pragma once
#include <stdint.h>
struct bt_conn;
#ifdef R2_DISCONNECT_DIAGNOSTICS
uint16_t r2_disconnect_snapshot(struct bt_conn *,uint64_t peer,uint32_t incarnation);
#else
static inline uint16_t r2_disconnect_snapshot(struct bt_conn *c,uint64_t p,uint32_t i)
{(void)c;(void)p;(void)i;return 0;}
#endif
