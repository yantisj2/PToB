/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Actual pinned primary scan abort path. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_BT_CENTRAL 1
#define CONFIG_BT_CTLR_ADV_EXT 1
#define BT_LE_SCAN_TYPE_PASSIVE 0
#define unlikely(x) (x)
#define LL_ASSERT(x) assert(x)
struct bt_le_scan_param {unsigned type,interval,window;};
struct lll_prepare_param {int unused;};
typedef int (*lll_prepare_cb_t)(struct lll_prepare_param *);
struct lll_conn {struct {bool initiated;} central;};
struct lll_scan {struct lll_conn *conn;unsigned duration_reload,duration_expire,ticks_window,state,is_aux_sched;};
static unsigned clocks;
static int lll_hfclock_on(void){++clocks;return 0;}
static int resume_prepare_cb(struct lll_prepare_param *p){(void)p;return 0;}
static void isr_done_cleanup(void *p){(void)p;}
static void isr_window(void *p){(void)p;}
static void radio_isr_set(void(*fn)(void*),void *p){(void)fn;(void)p;}
static void radio_disable(void){}
#include "actual_scan.h"
int main(void){
 for(unsigned synced=0;synced<2;synced++){
  struct bt_le_scan_param q=actual_scan(synced);
  assert(q.type==BT_LE_SCAN_TYPE_PASSIVE&&q.interval==32&&q.window<q.interval);
  assert(q.window==(synced?8u:16u));
  /* The pinned ULL retains a nonzero window when window+start overhead fits
   * the interval. Ten ms of gap exceeds its sub-ms start overhead. */
  struct lll_scan scan={.ticks_window=q.window};lll_prepare_cb_t resume=NULL;
  assert(is_abort_cb((void*)1,&scan,&resume)==-ECANCELED&&!resume&&!clocks);
 }
 struct lll_scan continuous={0};lll_prepare_cb_t resume=NULL;
 assert(is_abort_cb((void*)1,&continuous,&resume)==-EAGAIN&&resume==resume_prepare_cb&&clocks==1);
 struct lll_conn conn={.central={.initiated=true}};continuous.conn=&conn;resume=NULL;
 assert(!is_abort_cb((void*)1,&continuous,&resume)&&!resume);
 puts("PASS actual finite RC search/watch + pinned preemption; continuous negative path resumes; initiated connection remains protected; not hardware queue reproduction");
}
