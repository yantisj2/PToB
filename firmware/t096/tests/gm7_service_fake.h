/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Only kernel/physical platform boundaries. Production owner/service included.
 */
#pragma once
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define K_FOREVER (-1)
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define ARG_UNUSED(x) ((void)(x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define BUILD_ASSERT(x) _Static_assert(x, #x)
struct k_mutex {
  int unused;
};
#define K_MUTEX_DEFINE(n) struct k_mutex n
static int k_mutex_lock(struct k_mutex *m, int t) {
  (void)m;
  (void)t;
  return 0;
}
static int k_mutex_unlock(struct k_mutex *m) {
  (void)m;
  return 0;
}
static unsigned fake_irq_key;
static unsigned irq_lock(void) {
  unsigned prior = fake_irq_key;
  fake_irq_key = 1;
  return prior;
}
static void irq_unlock(unsigned prior) { fake_irq_key = prior; }
typedef int atomic_t;
static int atomic_get(atomic_t *p) { return *p; }
static void atomic_set(atomic_t *p, int v) { *p = v; }
static void atomic_clear(atomic_t *p) { *p = 0; }
static bool atomic_cas(atomic_t *p, int old, int v) {
  if (*p != old)
    return false;
  *p = v;
  return true;
}
struct k_thread {
  int unused;
};
struct k_work {
  void (*handler)(struct k_work *);
};
struct k_work_q {
  struct k_thread thread;
};
struct k_timer {
  int unused;
};
struct k_sem {
  int unused;
};
#define K_WORK_DEFINE(n, f) struct k_work n = {f}
#define K_TIMER_DEFINE(n, f, g) struct k_timer n
#define K_SEM_DEFINE(n, a, b) struct k_sem n
#define K_THREAD_STACK_DEFINE(n, s) char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
static struct k_work *fake_work;
static int k_work_submit_to_queue(struct k_work_q *q, struct k_work *w) {
  (void)q;
  fake_work = w;
  return 1;
}
static void k_work_queue_start(struct k_work_q *q, char *s, size_t n, int p,
                               void *v) {
  (void)q;
  (void)s;
  (void)n;
  (void)p;
  (void)v;
}
static struct k_thread *k_work_queue_thread_get(struct k_work_q *q) {
  return &q->thread;
}
static void k_thread_create(struct k_thread *t, char *s, size_t n,
                            void (*f)(void *, void *, void *), void *a, void *b,
                            void *c, int p, int o, int d) {
  (void)t;
  (void)s;
  (void)n;
  (void)f;
  (void)a;
  (void)b;
  (void)c;
  (void)p;
  (void)o;
  (void)d;
}
static void k_thread_name_set(struct k_thread *t, const char *n) {
  (void)t;
  (void)n;
}
static int k_thread_stack_space_get(struct k_thread *t, size_t *n) {
  (void)t;
  *n = 4096;
  return 0;
}
static int k_sem_take(struct k_sem *s, int t) {
  (void)s;
  (void)t;
  return 0;
}
static void k_sem_give(struct k_sem *s) { (void)s; }
static void k_timer_start(struct k_timer *t, int a, int b) {
  (void)t;
  (void)a;
  (void)b;
}
static void k_timer_stop(struct k_timer *t) { (void)t; }
static uint64_t fake_kernel = 100000000, fake_hf = 1000000;
static int64_t k_uptime_get(void) { return fake_kernel / 1000; }
static uint64_t k_ticks_to_us_floor64(uint64_t t) { return t; }
static uint64_t k_uptime_ticks(void) { return fake_kernel; }
static int sys_csrand_get(void *out, size_t n) {
  static uint64_t next = 1;
  uint8_t *p = out;
  for (size_t i = 0; i < n; i++)
    p[i] = (uint8_t)(next++);
  return 0;
}
typedef struct {
  uint8_t val[6];
} bt_addr_t;
typedef struct {
  uint8_t type;
  bt_addr_t a;
} bt_addr_le_t;
#define BT_ADDR_LE_RANDOM 1
static int bt_id_create(const bt_addr_le_t *a, const void *b) {
  (void)a;
  (void)b;
  return 0;
}
