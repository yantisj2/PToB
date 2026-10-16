#pragma once
#include <stdbool.h>
#include <stdint.h>

#define UI_RF_RADIOS 2u
#define UI_RF_KINDS 8u
#define UI_RF_REFRESH_TICKS 400000u /* best-effort 40 Hz at 16 MHz */
#define UI_RF_PULSE_TICKS 1440000u /* 90 ms; repeated packets coalesce */
enum { UI_RF_BLE, UI_RF_LORA };
enum { UI_RF_PPS, UI_RF_TIME, UI_RF_COPY, UI_RF_CORRECTION,
       UI_RF_DIAGNOSTIC, UI_RF_GM, UI_RF_MESH, UI_RF_OTHER };
enum { UI_RF_OFF, UI_RF_RX, UI_RF_TX };
typedef struct { uint64_t tick[UI_RF_RADIOS][2][UI_RF_KINDS]; } ui_rf_activity_t;
typedef struct {
    bool available, fresh, utc, rate_valid;
    uint32_t second, root, session;
    int32_t rate_ppb;
    int64_t time_ns;
} ui_rf_source_t;
typedef struct {
    ui_rf_source_t ble, lora;
    bool delta_valid, drift_valid;
    int64_t delta_ns, drift_ppb; /* BLE minus LoRa, same local observation tick */
} ui_rf_comparison_t;

/* Main-loop observations only. No queue, allocation, ISR work or RF traffic. */
static inline void ui_rf_note(ui_rf_activity_t *a, unsigned radio,
                               unsigned kind, bool tx, uint64_t tick) {
    if (radio < UI_RF_RADIOS && kind < UI_RF_KINDS)
        a->tick[radio][tx ? 1 : 0][kind] = tick;
}
static inline uint8_t ui_rf_lamp(const ui_rf_activity_t *a, unsigned radio,
                                 unsigned kind, uint64_t now) {
    if (radio >= UI_RF_RADIOS || kind >= UI_RF_KINDS) return UI_RF_OFF;
    uint64_t rx = a->tick[radio][0][kind], tx = a->tick[radio][1][kind];
    bool r = rx && now >= rx && now - rx < UI_RF_PULSE_TICKS;
    bool t = tx && now >= tx && now - tx < UI_RF_PULSE_TICKS;
    return t && (!r || tx >= rx) ? UI_RF_TX : r ? UI_RF_RX : UI_RF_OFF;
}
static inline unsigned ui_rf_lamp_x(unsigned kind) { return 4u + 9u * kind; }
static inline unsigned ui_rf_lamp_y(unsigned radio, bool help) {
    return help ? (radio ? 83u : 31u) : (radio ? 54u : 30u);
}
static inline void ui_rf_compare(ui_rf_comparison_t *c) {
    c->delta_valid = c->drift_valid = false;
    /* Compare explicit fresh UTC maps only. Relative/held/unavailable maps
     * cannot become a numeric delta. Each source retains its own identity. */
    if (!c->ble.available || !c->lora.available || !c->ble.fresh ||
        !c->lora.fresh || !c->ble.utc || !c->lora.utc ||
        !c->ble.root || !c->lora.root || !c->ble.session || !c->lora.session) return;
    if ((c->lora.time_ns < 0 && c->ble.time_ns > INT64_MAX + c->lora.time_ns) ||
        (c->lora.time_ns > 0 && c->ble.time_ns < INT64_MIN + c->lora.time_ns)) return;
    c->delta_ns = c->ble.time_ns - c->lora.time_ns;
    c->delta_valid = true;
    c->drift_valid = c->ble.rate_valid && c->lora.rate_valid;
    c->drift_ppb = (int64_t)c->ble.rate_ppb - c->lora.rate_ppb;
}
