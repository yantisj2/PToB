/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Fault-only reads of a host object retained by the existing transport owner.
 * Host/controller scalars share an IRQ fence; work flags are separate samples.
 * None is a completed-cleanup assertion.
 */
#include "disconnect_diag.h"
#include "timing_events.h"
#include "controller_snapshot.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include "conn_internal.h"
_Static_assert(K_WORK_MASK==31,"Review work-mask packing against the pinned SDK");
uint16_t r2_disconnect_snapshot(struct bt_conn *conn,uint64_t peer,uint32_t incarnation)
{
 if(!conn)return 0;
 /* No application lock, extra reference, allocation, wait or state change.
  * State is written DISCONNECTED before TX flush/deferred L2CAP cleanup.
  * Each work getter briefly takes its existing kernel lock, without waiting
  * for that work to complete. Flags may change between these samples. */
 /* On this reviewed UP/no-zero-latency target, capture the host handle/state
  * and current controller scalars in one short IRQ fence. No pointers escape.
  * This is current-handle evidence, not a controller incarnation guarantee. */
 struct ptob_controller_snapshot controller;
 unsigned key=irq_lock();
 unsigned raw=conn->state,state=5;uint16_t handle=conn->handle;
 ptob_controller_snapshot_locked(handle,&controller);
 irq_unlock(key);
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
  handle|((tx&31u)<<16)|((deferred&31u)<<21));
 /* Same first-fault/incarnation, existing bounded journal only. PRT and
  * expiry fields are event counts; inactive slot bytes may be stale. */
 r2_timing_event(R2_TIMING_GM_CONTROL,controller.lll_handle,peer,26,incarnation,
  controller.handle_in_range|(controller.handle_matches<<1)|(controller.head_present<<2)|
  ((!!controller.head_done)<<3)|((uint32_t)controller.local_state<<8)|
  ((uint32_t)controller.remote_state<<12)|((uint32_t)controller.head_proc<<16)|
  ((uint32_t)controller.head_state<<24));
 r2_timing_event(R2_TIMING_GM_CONTROL,controller.reason_final,peer,27,incarnation,
  controller.local_prt|((uint32_t)controller.remote_prt<<16));
 r2_timing_event(R2_TIMING_GM_CONTROL,controller.prt_reload,peer,28,incarnation,
  controller.supervision_expire|((uint32_t)controller.connect_expire<<16));
 return (uint16_t)((state<<3)|((tx&31u)<<6)|((deferred&31u)<<11));
}
