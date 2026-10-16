#pragma once
#include "driver/gptimer.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t half_slots,half_us; } w12_ble_time_t;
// Exact pinned driver-owned latch; ROM runs with no GPTimer lock held.
esp_err_t w12_s3_ble_time_bracket(gptimer_handle_t,uint64_t*,uint64_t*,w12_ble_time_t*);
#ifdef __cplusplus
}
#endif
