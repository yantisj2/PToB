/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
struct k_spinlock {unsigned held;};
typedef unsigned k_spinlock_key_t;
extern uint32_t holdover_test_uptime;
static inline uint32_t k_uptime_get_32(void){return holdover_test_uptime;}
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *p){assert(!p->held);p->held=1;return 0;}
static inline void k_spin_unlock(struct k_spinlock *p,k_spinlock_key_t k){(void)k;assert(p->held);p->held=0;}
