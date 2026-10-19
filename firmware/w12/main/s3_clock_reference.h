#pragma once
#include "driver/gptimer.h"
#if defined(PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC) && PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC
#include "driver/mcpwm_cap.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t half_slots,half_us; } w12_ble_time_t;
// Exact pinned driver-owned latch; ROM runs with no GPTimer lock held.
esp_err_t w12_s3_ble_time_bracket(gptimer_handle_t,uint64_t*,uint64_t*,w12_ble_time_t*);
#if defined(PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC) && PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC
// Diagnostic task-context bracket. Uses the same pinned driver-owned latch;
// no ROM/controller query and no GPTimer lock held across MCPWM's public call.
esp_err_t w12_s3_gnss_map_bracket(gptimer_handle_t,mcpwm_cap_channel_handle_t,uint64_t*,uint64_t*);
#endif
#ifdef __cplusplus
}
#endif
