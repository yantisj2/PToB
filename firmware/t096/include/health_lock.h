/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_HEALTH_LOCK_H
#define R2_HEALTH_LOCK_H
/* Included after kernel headers. Only application call sites are instrumented;
 * stock kernel/controller locking and interrupt priorities are unchanged. */
#if defined(CONFIG_ARM)
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include "health.h"
static inline k_spinlock_key_t r2_health_spin_take(struct k_spinlock *l,unsigned site)
{
 k_spinlock_key_t k=k_spin_lock(l);r2_health_lock_enter(site,(unsigned)k.key);return k;
}
static inline void r2_health_spin_drop(struct k_spinlock *l,k_spinlock_key_t k)
{
 r2_health_lock_leave((unsigned)k.key);k_spin_unlock(l,k);
}
static inline unsigned r2_health_irq_take(unsigned site)
{
 unsigned k=irq_lock();r2_health_lock_enter(site,k);return k;
}
static inline void r2_health_irq_drop(unsigned k)
{
 r2_health_lock_leave(k);irq_unlock(k);
}
#define k_spin_lock(l) r2_health_spin_take((l),(R2_LOCK_UNIT<<16)|__LINE__)
#define k_spin_unlock(l,k) r2_health_spin_drop((l),(k))
#undef irq_lock
#undef irq_unlock
#define irq_lock() r2_health_irq_take((R2_LOCK_UNIT<<16)|__LINE__)
#define irq_unlock(k) r2_health_irq_drop(k)
#endif
#endif
