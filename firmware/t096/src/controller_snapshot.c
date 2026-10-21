/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <soc.h>
#include <string.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#include "hal/cpu.h"
#include "hal/ecb.h"
#include "hal/ccm.h"
#include "hal/ticker.h"

#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mfifo.h"
#include "util/mayfly.h"
#include "util/dbuf.h"

#include "ticker/ticker.h"

#include "pdu_df.h"
#include "lll/pdu_vendor.h"
#include "pdu.h"

#include "lll.h"
#include "lll_clock.h"
#include "lll/lll_df_types.h"
#include "lll_conn.h"
#include "lll_conn_iso.h"
#include "lll/lll_vendor.h"

#include "ll_sw/ull_tx_queue.h"

#include "isoal.h"
#include "ull_iso_types.h"
#include "ull_conn_types.h"
#include "ull_conn_iso_types.h"

#if defined(CONFIG_BT_CTLR_USER_EXT)
#include "ull_vendor.h"
#endif /* CONFIG_BT_CTLR_USER_EXT */

#include "ull_internal.h"
#include "ull_llcp_internal.h"
#include "ull_sched_internal.h"
#include "ull_chan_internal.h"
#include "ull_conn_internal.h"
#include "ull_peripheral_internal.h"
#include "ull_central_internal.h"

#include "ull_iso_internal.h"
#include "ull_conn_iso_internal.h"
#include "ull_peripheral_iso_internal.h"
#include "lll/lll_adv_types.h"
#include "lll_adv.h"
#include "ull_adv_types.h"
#include "ull_adv_internal.h"
#include "lll_sync.h"
#include "lll_sync_iso.h"
#include "ull_sync_types.h"
#include "lll_scan.h"
#include "ull_scan_types.h"
#include "ull_sync_internal.h"

#include "ll.h"
#include "ll_feat.h"
#include "ll_settings.h"

#include "ll_sw/ull_llcp.h"
#include "ll_sw/ull_llcp_features.h"

#include "hal/debug.h"


#include "controller_snapshot.h"
#include "health.h"
BUILD_ASSERT(!IS_ENABLED(CONFIG_SMP));
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZERO_LATENCY_IRQS));
BUILD_ASSERT(CONFIG_BT_MAX_CONN == 6);
BUILD_ASSERT(PROC_TERMINATE == 10);
BUILD_ASSERT(sizeof(int) == sizeof(uint32_t));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_LOW_LAT));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_LOW_LAT_ULL));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE));
BUILD_ASSERT(TICKER_ID_CONN_BASE + CONFIG_BT_MAX_CONN <= UINT8_MAX);
BUILD_ASSERT(sizeof(((struct lll_conn *)0)->access_addr) == 4);
BUILD_ASSERT(sizeof(((struct lll_conn *)0)->event_counter) == 2);
BUILD_ASSERT(sizeof(((struct lll_conn *)0)->latency_event) == 2);
BUILD_ASSERT(sizeof(((struct ull_hdr *)0)->ref) == 1);
#include "controller_snapshot_body.inc"
