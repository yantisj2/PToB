#pragma once
#include <stdint.h>
#include <stdbool.h>
#define TICKS_PER_SECOND 16000000u
typedef enum {CLOCK_NO_TIME, CLOCK_ACQUIRING, CLOCK_LOCKED, CLOCK_HOLDOVER} clock_state_t;
#include "ptob_ui_role.h"

#define UI_WIDTH 80u
#define UI_HEIGHT 160u
#ifndef UI_FRAME_BYTES
#define UI_FRAME_BYTES (UI_WIDTH * UI_HEIGHT * 2u)
#endif
#include "ui_rf_live.h"
#define UI_PAGE_COUNT 9u
enum { UI_PAGE_CLOCK, UI_PAGE_RF_LIVE, UI_PAGE_SYNC, UI_PAGE_PPS,
       UI_PAGE_DRIFT, UI_PAGE_BLE, UI_PAGE_SYSTEM, UI_PAGE_NET, UI_PAGE_LORA };
/* Upcoming timing image stays at 200; following release opts in to 750. */
#ifndef UI_DIAGNOSTIC_PULSE_MS
#define UI_DIAGNOSTIC_PULSE_MS 200u
#endif

typedef struct {
    uint8_t page;
    bool rf_help;
    bool raw_pressed;
    bool stable_pressed;
    uint64_t raw_changed_tick;
} ui_button_t;

typedef enum {
    UI_PULSE_OFF, UI_PULSE_PPS, UI_PULSE_DIAGNOSTIC
} ui_pulse_t;
enum ui_native_reason { UI_NATIVE_NONE, UI_NATIVE_LOCAL_COARSE, UI_NATIVE_NO_TIME,
    UI_NATIVE_AUTH_WAIT, UI_NATIVE_NO_GM, UI_NATIVE_SOURCE_STALE,
    UI_NATIVE_PPS_UNASSOCIATED, UI_NATIVE_CONTROL_ONLY, UI_NATIVE_STORAGE_PAUSE };

typedef struct {
    bool rf_help;
    uint8_t rf_lamps[UI_RF_RADIOS][UI_RF_KINDS];
    bool rf_ble_tx_mode;
    ui_rf_comparison_t rf_sources;
    const char *unit_name;
    ptob_ui_role_t ui_role;
    bool ble_mode_known,ble_mode_enabled;
    bool field_relay_known,field_relay_requested,field_relay_ready;
    bool mesh_profile,mesh_available,mesh_fresh,mesh_utc,mesh_metric_valid,mesh_rssi_valid,mesh_original_rf;
    bool native_served_time; /* Current functional coarse time; never precision lock. */
    bool native_analog_eligible; /* Functional serving GM or fresh authenticated BLE follower. */
    char native_rms[16];
    bool native_rms_valid, native_pll_locked;
    bool native_boundary_clock,native_boundary_held;
    bool native_profile_supported,native_profile_rc,native_rc_serving_clients;uint8_t native_time_relay_hops;
    bool native_hot_standby; /* Current local hot standby, never mere selection. */
    bool native_ble_time; /* Selected network UTC; local GPS remains diagnostic only. */
    uint8_t native_rx_bars;
    bool native_rx_known;
    uint8_t native_reason; /* Functional panel reason; no precision-error inference. */
    bool source_qualified,source_provisional;uint8_t source_quality_class,source_n;uint32_t source_hold_s,source_scatter_ppb;int32_t source_drift_ppb,mesh_bias_ns;
    uint32_t mesh_received,mesh_reconstructed,mesh_conflicts,mesh_expired;
    uint32_t mesh_rms_ns,mesh_p95_ns,mesh_p99_ns;
    uint8_t mesh_metric_n;int8_t mesh_rssi_dbm;
    uint32_t mesh_second,mesh_provider,mesh_root,mesh_age_ms;
    uint8_t mesh_depth;
    bool lora_tx_capable,lora_active;
    bool lora_only,lora_available,lora_seen,lora_rssi_valid,lora_metric_valid,lora_tx_mode,lora_time_override;
    int8_t lora_rssi_dbm;
    uint32_t lora_second,lora_rms_ns,lora_origin,lora_emitter;
    uint8_t lora_quarter;
    uint16_t battery_mv;
    uint8_t battery_percent;
    bool battery_present, battery_valid, usb_power;
    bool basic_profile;
    bool copy_profile;
    uint32_t copy_event;
    uint8_t basic_state;
    uint32_t basic_session, basic_updates, basic_failures, basic_error;
    uint8_t page;
    ui_pulse_t pulse_indicator;
    bool screensaver;
    uint8_t screensaver_position;
    uint32_t utc_second;
    uint32_t boot_elapsed_seconds;
    uint8_t subsecond_quarter;
    clock_state_t state;
    bool utc_valid;
    bool display_aligning;
    int32_t display_error_ms;
    bool cached_time_valid;
    bool acquiring_first_lock;
    bool ble_tx_active;
    bool pps_seen;
    uint32_t holdover_seconds;
    uint32_t estimated_tps;
    uint64_t estimated_tps_q16;
    int16_t die_temperature_c;
    bool die_temperature_valid;
    uint32_t clock_prediction_peak_ns;
    uint8_t clock_prediction_samples;
    int32_t clock_drift_ppb;
    int32_t clock_trim_ppb;
    int32_t clock_resid_ppb;
    uint32_t clock_fit_rms_ns;
    uint16_t clock_fit_samples;
    bool clock_fit_valid;
    bool clock_trim_saved;
    bool pair_active, pair_metric_valid, pair_locked;
    uint32_t pair_rms_ns;
    bool pair_correction_valid, pair_zero_settled;
    uint32_t pair_correction_ns;
    uint32_t beat_hundredth_ppb_average;
    uint16_t beat_ppb_samples;
    uint32_t beat_deviation_peak_ticks;
    uint8_t beat_phase_samples;
    uint16_t address_delay_ticks;
    uint8_t active_ble_slots;
    uint16_t uncertainty_100ns;
    uint64_t pps_count;
    int32_t last_pps_error_ticks;
    int32_t min_pps_error_ticks;
    int32_t max_pps_error_ticks;
    uint32_t pps_age_ms;
    uint32_t rmc_age_ms;
    uint64_t missed_pps;
    uint64_t gnss_bytes_seen;
    uint64_t gnss_sentences_seen;
    uint32_t gnss_rx_age_ms;
    uint64_t rmc_accepted;
    uint64_t uart_overruns;
    uint8_t gnss_fix_quality;
    uint8_t gnss_sats_used;
    uint8_t gnss_sats_visible;
    uint8_t gnss_sv_seen;
    uint8_t gnss_cn0_peak;
    uint8_t gnss_pulse_hz;
    uint8_t gnss_pulse_flags;
    uint8_t gnss_nav_hz;
    uint16_t gnss_hdop_tenths;
    uint16_t gnss_fix_age_s;
    uint16_t gnss_signal_age_s;
    uint32_t gnss_bad_checksum;
    bool gnss_native_health; /* Native snapshot has explicit sentence availability. */
    bool gnss_rmc_status_known, gnss_rmc_fix_active;
    bool gnss_product_seen;
    bool gnss_tp_known;
    bool gnss_nav_known;
    bool gnss_sys_known;
    bool gnss_dyn_known;
    bool gnss_static_model;
    bool gnss_l1l5_enabled;
    bool gnss_pulse_safe;
    uint16_t last_tx_sequence;
    bool tx_seen;
    uint64_t tx_address_tick;
    uint64_t failed_tx;
    uint64_t missed_slots;
    uint64_t beats_sent;
    uint64_t companions_sent;
    bool management_enabled, is_grandmaster, management_follower, diagnostics_enabled;
    bool network_time;
    bool election_challenge, diagnostic_active, diagnostic_result;
    uint32_t root_id, diagnostic_age_s, management_rx, management_tx;
    uint32_t diagnostic_delay_ns, diagnostic_bound_ns;
    int32_t diagnostic_offset_ns;
    uint8_t relay_hops, diagnostic_status;
    bool lora_enabled, lora_ready, lora_busy, lora_locked;
    bool lora_lt3, lora_tx_enabled;
    uint32_t lora_received;
    uint8_t lora_error;
    uint32_t lora_sent, lora_failed, lora_skipped, lora_sequence, lora_age_ms;
} ui_snapshot_t;

void ui_format_count(char out[8], uint64_t value);
void ui_format_ppb(char out[16], uint32_t hundredths);
void ui_button_init(ui_button_t *button, bool pressed, uint64_t now);
bool ui_button_update(ui_button_t *button, bool pressed, uint64_t now);
ui_pulse_t ui_pps_pulse(bool seen, uint32_t age_ms, bool diagnostic_second);
bool ui_full_frame_phase_safe(bool seen, uint64_t age_ticks, bool diagnostic_second);
void ui_render_pulse(uint8_t framebuffer[UI_FRAME_BYTES], ui_pulse_t pulse);
bool display_present_pulse(const uint8_t framebuffer[UI_FRAME_BYTES]);
void ui_render(uint8_t framebuffer[UI_FRAME_BYTES], const ui_snapshot_t *snapshot);
void ui_render_rf_lamp(uint8_t framebuffer[UI_FRAME_BYTES], unsigned radio,
                       unsigned kind, uint8_t activity, bool help);
bool display_present_rf_lamp(const uint8_t framebuffer[UI_FRAME_BYTES],
                             unsigned radio, unsigned kind, bool help);

bool display_init(void);
bool display_present(const uint8_t framebuffer[UI_FRAME_BYTES]);
bool hw_button_pressed(void);

/* Idle policy uses boot seconds; brightness uses fixed CDT (UTC-5). */
typedef struct {
    bool active, initialized, requested;
    uint32_t idle_since, started;
    uint64_t failed_tx, missed_slots, missed_pps, uart_overruns;
    uint32_t bad_checksum;
} ui_saver_t;
void ui_saver_update(ui_saver_t *saver, ui_snapshot_t *snapshot, bool activity);
uint8_t ui_brightness_percent(bool utc_valid, uint32_t utc_second);
void display_set_brightness(uint8_t percent);
int ui_saver_center(uint8_t position);
void ui_render_pulse_at(uint8_t framebuffer[UI_FRAME_BYTES], ui_pulse_t pulse,
                        unsigned x, unsigned y);
bool display_present_pulse_at(const uint8_t framebuffer[UI_FRAME_BYTES],
                              unsigned x, unsigned y);

typedef enum { UI_CLICK_NONE, UI_CLICK_PAGE, UI_CLICK_SAVER, UI_CLICK_WAKE, UI_CLICK_LORA, UI_CLICK_BLE, UI_CLICK_HELP } ui_click_t;
typedef struct { bool pending; uint64_t first_tick; } ui_click_state_t;
ui_click_t ui_click_update(ui_click_state_t *click, bool press_event,
                           bool saver_active, uint64_t now);

/* Consumes a hold on awake LORA/BLE TX pages; short clicks keep navigation. */
typedef struct { bool holding, fired; uint64_t started; } ui_lora_hold_t;
ui_click_t ui_gesture_update(ui_lora_hold_t *hold, ui_click_state_t *click,
    bool press_event, bool pressed, uint8_t page, bool saver_active, uint64_t now);

bool display_present_rows(const uint8_t framebuffer[UI_FRAME_BYTES],unsigned first,unsigned count);
