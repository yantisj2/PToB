/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Fault-only reads of a host object retained by the existing transport owner.
 * These three samples are nontransactional, not a completed-cleanup assertion.
 */
#include "disconnect_diag.h"
#include "timing_events.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include "conn_internal.h"
_Static_assert(K_WORK_MASK==31,"Review work-mask packing against the pinned SDK");
uint16_t r2_disconnect_snapshot(struct bt_conn *conn,uint64_t peer,uint32_t incarnation)
{
 if(!conn)return 0;
 /* No application lock, extra reference, allocation, wait or controller read.
  * State is written DISCONNECTED before TX flush/deferred L2CAP cleanup.
  * Each work getter briefly takes its existing kernel lock, without waiting
  * for that work to complete. Flags may change between these samples. */
 unsigned raw=conn->state,state=5;
 switch(raw){
 case BT_CONN_CONNECTED:state=1;break;
 case BT_CONN_DISCONNECTING:state=2;break;
 case BT_CONN_DISCONNECT_COMPLETE:state=3;break;
 case BT_CONN_DISCONNECTED:state=4;break;
 default:break;
 }
 unsigned tx=0;
#if defined(CONFIG_BT_CONN_TX)
 tx=(unsigned)k_work_busy_get(&conn->tx_complete_work);
#endif
 unsigned deferred=(unsigned)k_work_delayable_busy_get(&conn->deferred_work);
 r2_timing_event(R2_TIMING_GM_CONTROL,(int)raw,peer,25,incarnation,
  conn->handle|((tx&31u)<<16)|((deferred&31u)<<21));
 return (uint16_t)((state<<3)|((tx&31u)<<6)|((deferred&31u)<<11));
}
