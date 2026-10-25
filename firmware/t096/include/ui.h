/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_UI_H
#define R2_UI_H
#include "clock.h"
#include "../protocol/election_policy.h"
#include "radio_precision.h"
#include "link_quality.h"
#include <stdint.h>
#include <stdbool.h>
#include "ui_framebuffer.h"
#define R2_UI_IDLE_MS UINT64_C(300000)
#define R2_UI_SAVER_MOVE_MS UINT64_C(30000)
/* Whole 92px watch assembly moves within equal4px top/bottom margins. */
#define R2_UI_SAVER_HEIGHT 92
static inline unsigned r2_ui_saver_top(unsigned position){return 4+30*(position%3);}
static inline unsigned r2_ui_saver_next(unsigned current,uint32_t random){return (current%3+1+(random&1u))%3;}
#include "ui_pages.h"
#include "shadow_session.h"
#ifdef R2_LORA
#include "lora_service.h"
#endif
/* Filled only by the owning service, never from advertised names or UI controls. */
enum r2_ui_role { R2_UI_NO_ROLE, R2_UI_GM, R2_UI_FOLLOWER };
enum r2_ui_reset {R2_UI_RESET_UNKNOWN,R2_UI_RESET_PIN,R2_UI_RESET_SOFTWARE,
 R2_UI_RESET_POWER,R2_UI_RESET_BROWNOUT,R2_UI_RESET_WATCHDOG,R2_UI_RESET_LOCKUP,
 R2_UI_RESET_WAKE,R2_UI_RESET_DEBUG,R2_UI_RESET_MULTIPLE,R2_UI_RESET_WD_PROFILE,
 R2_UI_RESET_WD_CORE,R2_UI_RESET_WD_FAULT,R2_UI_RESET_WD_RECOVERY};
const char *r2_ui_reset_text(unsigned reason);
void r2_ui_uptime_text(char out[16],uint64_t uptime_ms);
struct r2_ui_model {
#ifdef R2_GM7_NATIVE
 uint64_t gm7_at_ms,gm7_peer_id;
 uint32_t gm7_hot_ms,gm7_active_ms,gm7_pps_ms,gm7_peer_hot_ms,gm7_peer_active_ms,gm7_peer_pps_ms;
 uint8_t gm7_known,gm7_peer_role,gm7_peer_known;
 bool gm7_owner,gm7_peer_valid,gm7_peer_current,gm7_peer_fresh,gm7_peer_eligible_claim;
#endif
#ifdef R2_LORA
 struct r2_lora_view lora;
#endif
 struct r2_clock_view clock;
 struct r2_radio_precision_view precision;
 struct r2_pps_precision_view pps_precision;
 uint64_t sampled_ms,ble_valid_until_us;
 /* Display-only hold, never consumed by timing, election or output. */
 uint64_t coarse_utc_us,coarse_at_us;bool coarse_valid;
 uint64_t face_lock_until_us;uint32_t face_rms_ns;
 uint32_t queue_drops;
 unsigned reset_kind;
 char unit[6];
 char root_name[25],upstream_name[25],coordinator_name[25];
 unsigned role;
 bool profile_supported,profile_rc,profile_switching;
 bool platform_ready,bt_ready,scope_active,authenticated_gm,follower_pps_valid;
 bool gm_source_fresh,ble_time_selected,sync_attempt;
 uint32_t sync_age_ms,rx,authenticated;
 unsigned root,coordinator,standby;
 unsigned relay_upstream;
 bool relay_selected;uint8_t time_relay_hops; /* Actual selected authenticated route depth. */
 bool boundary_clock,rc_source_held;uint32_t rc_update_age_ms,rc_source_age_ms;
 struct r2_clock_quality quality;
 uint32_t admission_ms,hot_refusal;
 bool source_eligible,gm_capable,hot_ready,local_hot_standby,gm_cold;
 uint8_t precision_beat_hz,lamps[8];
 bool ble_active;
 int16_t temperature_qc;
 bool temperature_valid;
 uint16_t ble_delivery_per_mille;
 uint32_t ble_rx_age_ms;
 bool ble_reception_valid;
 bool rc_serving_clients; /* Display-only actual downstream service, never GM authority. */
};
/* Presentation cache only. No timing owner consumes this 500ms loss delay. */
struct r2_ui_sync_display {int64_t utc_us;uint64_t at_ms,loss_ms;};
/* Monotonic seconds plus one; zero means unavailable. Presentation only. */
struct r2_ui_holds {uint32_t pps_since,hot_since,active_since;};
/* Selected/drawn pages are bounded by R2_UI_PAGES before storage. */
_Static_assert(R2_UI_PAGES<=UINT8_MAX,"UI page indices must fit their stored type");
struct r2_ui_status { uint8_t drawn_lamps[8]; uint64_t profile_flash_until;bool profile_target_rc;uint32_t profile_toggles; uint64_t drawn_model_ms;unsigned drawn_role;uint32_t poll_max_ms;bool drawn_scope,drawn_ble,drawn_pps,drawn_map;bool drawn_time,drawn_coarse,drawn_sync_grace;int64_t drawn_utc_us;uint32_t frames,missed,buttons,max_frame_ms,last_frame_ms; int error; uint8_t page,drawn_page; bool ready,stopped,analog,help,poweroff; bool screensaver,drawn_screensaver;uint8_t screensaver_position,drawn_screensaver_position,brightness_percent; int16_t temperature_qc; bool temperature_valid; struct r2_shadow_session_view shadow; struct r2_ui_sync_display sync_display; struct r2_ui_holds holds; };
void r2_ui_holds_update(struct r2_ui_holds *,const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_witness_page(const struct r2_ui_model *,unsigned page);
void r2_ui_identity_name(char out[25],uint64_t identity,const char *authenticated_name);
void r2_ui_select_ble_time(struct r2_ui_model *m,uint64_t utc,uint64_t sampled_us,uint64_t now_us,uint64_t valid_until,bool valid);
bool r2_ui_ble_current(const struct r2_ui_model *m,uint64_t now_ms);
bool r2_ui_face_locked(const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_saver_allowed(const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_analog(const struct r2_ui_model *m,uint64_t now);
bool r2_ui_display_time(const struct r2_ui_model *,uint64_t now_ms,int64_t *utc_us,bool *coarse);
void r2_ui_sync_display_update(struct r2_ui_sync_display *,const struct r2_ui_model *,uint64_t now_ms,bool analog_drawn);
bool r2_ui_sync_display_time(const struct r2_ui_sync_display *,const struct r2_ui_model *,uint64_t now_ms,int64_t *utc_us);
void r2_ui_reception(struct r2_ui_model *,const struct r2_link_quality_view *,uint64_t now_us);
/* Recent actual activity blinks4Hz on the8Hz RF page; no RF-rate claim. */
void r2_ui_rf_lamps(uint8_t out[8],const struct r2_ui_model *,uint64_t now_ms);
/* Both renderer and display consume R2_UI_FRAME_BYTES palette indices. */
void r2_ui_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now);
void r2_ui_publish(const struct r2_ui_model *m);
void r2_ui_start(void);
void r2_ui_stop(void);
bool r2_ui_poweroff_requested(void);
int r2_ui_close_result(void);
int r2_ui_page(unsigned page);
void r2_ui_snapshot(struct r2_ui_status *out);
bool r2_ui_shadow_requested(void);
bool r2_ui_take_profile_toggle(void);
#ifdef R2_LORA
bool r2_ui_take_lora_toggle(void);
#endif
void r2_ui_profile_flash(bool rc);
uint32_t r2_ui_stack_unused(void);
int r2_display_init(void);
int r2_display_frame(const uint8_t *fb);
int r2_display_button(void);
int r2_display_off(void);
int r2_display_brightness(uint8_t percent);
int r2_display_recover(void);
int r2_display_temperature(int16_t *quarter_c);
#endif

const char *r2_ui_servo_state(const struct r2_ui_model *,uint64_t now_ms);
