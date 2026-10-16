/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "rc_boundary.h"
struct bt_conn;
struct r2_uc_clock {
  struct rc5_map map;
  struct rc5_origin origin;
  uint64_t until_tick;
  uint32_t capture_epoch;
  uint8_t depth;
  uint8_t scope[16];
  bool valid;
};
int r2_uc_start(const struct r2_group_config *, uint64_t, uint64_t);
int r2_uc_poll(const struct r2_uc_clock *,bool advertise_allowed);
bool r2_uc_owns(struct bt_conn *);
bool r2_uc_close(void);
/* No RC advertiser, client reference, notification submission or queued work. */
bool r2_uc_quiescent(void);
/* Bounded main-thread diagnostic handoff, never a callback flash write. */
bool r2_uc_trace(uint32_t out[3]);
void r2_uc_stats(uint32_t out[8]);
bool r2_uc_serving_clients(void); /* Connected, subscribed, authenticated live grants only. */
bool r2_uc_advertising(void); /* Owner/controller API state, not an RF witness. */
