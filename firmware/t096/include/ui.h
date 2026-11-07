/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_UI_H
#define R2_UI_H
#include "clock.h"
#ifdef R2_TZ_WIRE
#include "../protocol/time_zone.h"
#endif
#include "../protocol/election_policy.h"
#include "radio_precision.h"
#include "link_quality.h"
#include <stdint.h>
#include <stdbool.h>
#include "ui_framebuffer.h"
#include "ui_menu.h"
#define R2_UI_IDLE_MS UINT64_C(300000)
/* UI-local high bit; the low three GM7 known bits remain duration validity. */
#define R2_UI_GM7_OWNER_FAULT 0x80u
#define R2_UI_SAVER_MOVE_MS UINT64_C(30000)
/* Whole 92px watch assembly moves within equal4px top/bottom margins. */
#define R2_UI_SAVER_HEIGHT 92
static inline unsigned r2_ui_saver_top(unsigned position){return 4+30*(position%3);}
static inline unsigned r2_ui_saver_next(unsigned current,uint32_t random){return (current%3+1+(random&1u))%3;}
#include "ui_pages.h"
#include "shadow_session.h"
#ifdef R2_LORA
/* Presentation-only same-HF-tick comparison; original source deadlines bound it. */
enum {R2_UI_RF_DELTA=1,R2_UI_RF_RATE=2,R2_UI_RF_ESTIMATE=4};
struct r2_ui_rf_comparison {
 uint64_t sampled_us,until_us;int64_t delta_ns,drift_ppb;uint8_t flags;
};
_Static_assert(sizeof(struct r2_ui_rf_comparison)==40,"Bounded RF comparison view");
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
 struct r2_ui_rf_comparison rf_comparison;
#endif
#ifdef R2_TZ_WIRE
 /* Coarse mesh metadata expiry in kernel uptime; presentation only. */
 struct r2_tz tz;uint64_t tz_until_us;
 /* Last learned presentation only; never source/metadata authority. */
 struct r2_tz display_tz;uint8_t display_tz_source;
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
 bool ble_rx_supported,ble_rx_requested;
 bool platform_ready,bt_ready,scope_active,authenticated_gm,follower_pps_valid;
 bool gm_source_fresh,ble_time_selected,sync_attempt;
 uint32_t sync_age_ms,rx,authenticated;
 unsigned root,coordinator,standby;
 unsigned relay_upstream;
 bool relay_selected;uint8_t time_relay_hops; /* Actual selected authenticated route depth. */
 bool boundary_clock,rc_source_held,ble_local_holdover;uint32_t rc_update_age_ms,rc_source_age_ms;
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
 /* Presentation label ownership; uses existing tail padding on native GM/RC. */
 bool upstream_is_lora;
};
#if defined(CONFIG_ARM) && defined(R2_GM7_NATIVE) && defined(R2_LORA) && defined(R2_TZ_WIRE)
_Static_assert(sizeof(struct r2_ui_model)==752,"UI label tag must use existing padding");
_Static_assert(offsetof(struct r2_ui_model,upstream_is_lora)==750,"Native label padding offset");
#endif
#ifdef R2_TZ_WIRE
/* Low bits identify the learned producer; remaining bits describe this draw. */
enum {R2_UI_TZ_NETWORK=1,R2_UI_TZ_LORA=2,R2_UI_TZ_LOCAL=3,R2_UI_TZ_SOURCE_MASK=3,
 R2_UI_TZ_CACHED=4,R2_UI_TZ_LOCAL_VALID=8,R2_UI_TZ_FOREIGN=16,
 R2_UI_TZ_OBSERVED=32,R2_UI_TZ_CALENDAR=64};
#endif
/* Presentation cache only. No timing owner consumes this 500ms loss delay. */
struct r2_ui_sync_display {int64_t utc_us;uint64_t at_ms,loss_ms;};
/* Monotonic seconds plus one; zero means unavailable. Presentation only. */
struct r2_ui_holds {uint32_t pps_since,hot_since,active_since;};
/* Selected/drawn pages are bounded by R2_UI_PAGES before storage. */
_Static_assert(R2_UI_PAGES<=UINT8_MAX,"UI page indices must fit their stored type");
struct r2_ui_status {
#ifdef R2_TZ_WIRE
 struct r2_tz drawn_tz;uint8_t drawn_tz_flags;
#endif
 uint8_t drawn_lamps[8]; uint64_t profile_flash_until;bool profile_target_rc;uint32_t profile_toggles; uint64_t drawn_model_ms;unsigned drawn_role;uint32_t poll_max_ms;bool drawn_scope,drawn_ble,drawn_pps,drawn_map;bool drawn_time,drawn_coarse,drawn_sync_grace;uint8_t logic_flags;int64_t drawn_utc_us;uint32_t frames,missed,buttons,max_frame_ms,last_frame_ms; int error; uint8_t page,drawn_page; bool ready,stopped,analog,help,poweroff; bool screensaver,drawn_screensaver;uint8_t screensaver_position,drawn_screensaver_position,brightness_percent; int16_t temperature_qc; bool temperature_valid; struct r2_shadow_session_view shadow; struct r2_ui_sync_display sync_display; struct r2_ui_holds holds;uint32_t logic_age_ms; struct r2_ui_menu menu;uint64_t menu_result_until;int menu_result;uint8_t menu_result_item;bool button_down,drawn_menu; };
uint8_t r2_ui_menu_enabled(const struct r2_ui_model *);
/* Volatile, bounded 120ms tap /2200ms hold for exact-device console checks.
 * Uses the physical debounce/gesture path; zero cancels pending injection. */
int r2_ui_press(unsigned kind);
bool r2_ui_take_ble_toggle(void);
void r2_ui_menu_result(unsigned item,int result);
void r2_ui_holds_update(struct r2_ui_holds *,const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_witness_page(const struct r2_ui_model *,unsigned page);
void r2_ui_identity_name(char out[25],uint64_t identity,const char *authenticated_name);
void r2_ui_select_ble_time(struct r2_ui_model *m,uint64_t utc,uint64_t sampled_us,uint64_t now_us,uint64_t valid_until,bool valid);
bool r2_ui_ble_current(const struct r2_ui_model *m,uint64_t now_ms);
bool r2_ui_face_locked(const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_saver_allowed(const struct r2_ui_model *,uint64_t now_ms);
bool r2_ui_analog(const struct r2_ui_model *m,uint64_t now);
bool r2_ui_display_time(const struct r2_ui_model *,uint64_t now_ms,int64_t *utc_us,bool *coarse);
#ifdef R2_TZ_WIRE
void r2_ui_display_tz_update(struct r2_ui_model *,struct r2_tz previous,uint8_t source,
 bool fresh);
struct r2_tz r2_ui_display_zone(const struct r2_ui_model *,uint64_t now_ms,int64_t utc_us,uint8_t *flags);
#endif
#ifdef R2_LORA
/* Watch presentation only; independent RF clocks keep their own data. */
bool r2_ui_lora_primary(const struct r2_ui_model *,uint64_t now_ms);
/* Current learned baseline: positive excess, or zero inside its noise floor. */
bool r2_ui_lora_tof_ns(const struct r2_ui_model *,uint64_t now_ms,uint32_t *);
#endif
#ifdef R2_TZ_WIRE
const char *r2_ui_clock_zone(const struct r2_ui_model *,uint64_t now_ms,int64_t utc_us,int64_t *shown_us);
#endif
void r2_ui_sync_display_update(struct r2_ui_sync_display *,const struct r2_ui_model *,uint64_t now_ms,bool analog_drawn);
bool r2_ui_sync_display_time(const struct r2_ui_sync_display *,const struct r2_ui_model *,uint64_t now_ms,int64_t *utc_us);
void r2_ui_reception(struct r2_ui_model *,const struct r2_link_quality_view *,uint64_t now_us);
/* Recent actual activity blinks4Hz on the8Hz RF page; no RF-rate claim. */
void r2_ui_rf_lamps(uint8_t out[8],const struct r2_ui_model *,uint64_t now_ms);
/* Both renderer and display consume R2_UI_FRAME_BYTES palette indices. */
void r2_ui_render(uint8_t *fb,const struct r2_ui_model *m,const struct r2_ui_status *s,uint64_t now);
/* One UI caller; at most80 attempted pixels between ordinary-thread checks.
 * false aborts pure scratch drawing and propagates to the owner immediately. */
enum r2_ui_render_stage {R2_UI_RENDER_OPTIONAL,R2_UI_RENDER_PIXELS};
typedef bool (*r2_ui_render_checkpoint_fn)(void *context,enum r2_ui_render_stage);
bool r2_ui_render_try(uint8_t *,const struct r2_ui_model *,
 const struct r2_ui_status *,uint64_t,r2_ui_render_checkpoint_fn,void *);
enum r2_ui_tile {R2_UI_TILE_TIME,R2_UI_TILE_PPS,R2_UI_TILE_LOSS,R2_UI_TILE_RF,
  R2_UI_TILE_LORA,R2_UI_TILE_CLEAR};
struct r2_ui_rect {uint8_t x,y,w,h;};
/* Prevalidated calendar offset, never a timing/TZ permission. */
struct r2_ui_zone_cache {uint64_t first_us,until_us;struct r2_tz zone;uint8_t flags;};
struct r2_ui_tile_view {int64_t utc_us;uint32_t edge;bool valid,coarse,pulse,analog;};
void r2_ui_zone_cache_update(struct r2_ui_zone_cache *,const struct r2_ui_model *,uint64_t);
bool r2_ui_tile_render(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,
 uint64_t,enum r2_ui_tile,const struct r2_ui_zone_cache *,struct r2_ui_tile_view *,
 r2_ui_render_checkpoint_fn,void *);
bool r2_ui_tile_render_rect(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,
  uint64_t,enum r2_ui_tile,const struct r2_ui_zone_cache *,struct r2_ui_tile_view *,
  r2_ui_render_checkpoint_fn,void *,struct r2_ui_rect);
bool r2_ui_local_pulse(const struct r2_ui_model *,uint64_t);
/* On demand only. pps populations: normal0/saver1; fields: eligible on/off,
 * committed on/off, missed on/off, unknown/cancelled. Latencies are successful
 * SPI submit/completion evidence, never optical scan or independent accuracy. */
struct r2_ui_critical_status {
 uint32_t observed_edges,unknown_edges,unknown_seconds,resets;
 uint32_t pps[2][9],pps_max_ms[2][4],seconds[4],seconds_max_ms[2],late_gt100_ms[5];
 uint32_t jobs[8]; /* critical/background attempts, critical/background commits,
                    errors, deadline/budget declines, stale submits, aborts */
 /* Last returned attempt: elapsed_ms[31:16], stage[15:8],
  * background[7], result[6:0]. Stages1 before driver,2 window,3 prepare,
  * 4 expand,5 submit,8 last raster checkpoint. Results0 success,1 budget,
  * 2 obsolete,3 expired,4 physical errno,5 post-return fence refusal.
  * Diagnostic only; not a committed tile ACK or CPU attribution. */
 uint32_t attempt,generation,context,edge;
 int64_t rendered_utc_us;
 uint64_t render_ms,submit_ms,retire_ms;
 int64_t digital_utc_us;uint64_t digital_render_ms,digital_submit_ms,digital_retire_ms;
 uint32_t fence[6]; /* last context/control/policy, digital context/control/policy */
 uint8_t page,population,tile,pending;bool pulse,valid,digital_valid;
};
void r2_ui_critical_snapshot(struct r2_ui_critical_status *);
/* Sole main clock-query owner only; bounded serialization scratch is static
 * and nonreentrant. The snapshot getter above remains thread-safe. */
bool r2_ui_critical_append(char *,size_t);
/* Published at the existing UI summary cadence. Cursor bounds alone cannot
 * reconstruct records whose reference association arrived after consumption. */
struct r2_ui_shadow_window {
 uint64_t start_tick,epoch;
 uint32_t session,start_sequence,cursor,samples,context,hint,control;
};

/* Main-owned source summary: no projection, calendar or blocking UI lock.
 * Revision fences obsolete visuals; it never supplies timing authority. */
struct r2_ui_context {
 uint64_t incarnation,root,boot;
 uint32_t source_epoch,train;
 uint8_t flags;
};
enum {R2_UI_CONTEXT_RC=1,R2_UI_CONTEXT_SWITCH=2,R2_UI_CONTEXT_STOP=4,R2_UI_CONTEXT_BLE_FAULT=8,R2_UI_CONTEXT_SERVING=16,R2_UI_CONTEXT_LIVE=32};
void r2_ui_context_update(const struct r2_ui_context *);
enum {R2_UI_HINT_KNOWN=1,R2_UI_HINT_LOCAL=2,R2_UI_HINT_BLE_VALID=4,R2_UI_HINT_LOCAL_VALID=8};
void r2_ui_source_hint(unsigned);
/* Same functional qualification used by the ordinary GM clock publisher.
 * Physical GNSS/PPS may be usable while legacy association remains unknown. */
static inline bool r2_ui_local_source_current(bool available,bool valid,bool fix,bool pps){
 return available||(valid&&fix&&pps);
}
bool r2_ui_model_requested(uint64_t now_ms);
/* Queued owner work is normal at main publication; execution/backlog defer UI. */
static inline bool r2_ui_work_allowed(bool idle,bool switching,bool closing,
 unsigned owner_state,uint32_t backlog,bool relay_pending,bool takeover_pending){
 return idle&&!switching&&!closing&&owner_state<=1&&!backlog&&!relay_pending&&!takeover_pending;
}
#ifdef R2_TZ_WIRE
void r2_ui_learn_tz(struct r2_tz,uint8_t source);
#endif
void r2_ui_publish(const struct r2_ui_model *m);
struct r2_ui_label_ids {uint64_t root,upstream,coordinator;};
void r2_ui_publish_owned(const struct r2_ui_model *,uint64_t lora_emitter,const struct r2_ui_label_ids *);
enum {R2_UI_LOGIC_VALID=1,R2_UI_LOGIC_BLE=2,R2_UI_LOGIC_LORA=4,
 R2_UI_LOGIC_GRACE=8,R2_UI_LOGIC_FRESH=16,R2_UI_LOGIC_GM=32,
 R2_UI_LOGIC_HOLD=64,R2_UI_LOGIC_MAINTAINED=128};
/* Main-owned, independent of LCD/render demand. The owner deadline is only
 * used to request a new owned snapshot; it never renews old copied permission. */
bool r2_ui_logic_due(uint64_t now_ms,uint64_t owner_until_us,bool owner_valid);

void r2_ui_start(void);
void r2_ui_input_tick(uint64_t now_ms);
void r2_ui_stop(void);
bool r2_ui_poweroff_requested(void);
int r2_ui_close_result(void);
int r2_ui_page(unsigned page);
void r2_ui_snapshot(struct r2_ui_status *out);
/* Same locked fault/frame counters, without optional logical-status projection. */
void r2_ui_diag_snapshot(int *error,uint32_t *max_frame_ms,uint32_t *missed);
bool r2_ui_shadow_requested(void);
bool r2_ui_rf_requested(void);
bool r2_ui_take_profile_toggle(void);
#ifdef R2_LORA
bool r2_ui_take_lora_toggle(void);
#endif
void r2_ui_profile_flash(bool rc);
uint32_t r2_ui_stack_unused(void);
int r2_display_init(void);
int r2_display_frame(const uint8_t *fb);
/* One completed command/row at a time. Callbacks own no live SPI DMA.
 * PREPARE may refresh palette scratch; SUBMIT must accept that exact instant.
 * Every attempt shares the supplied absolute deadline; no hidden renewal. */
enum r2_display_stage {R2_DISPLAY_WINDOW,R2_DISPLAY_PREPARE,
 R2_DISPLAY_EXPAND,R2_DISPLAY_SUBMIT,R2_DISPLAY_RETIRED};
/* Every negative GPIO/SPI errno stays a real failure. */
enum r2_display_result {R2_DISPLAY_OK=0,R2_DISPLAY_BUDGET_DECLINED=1,
 R2_DISPLAY_OBSOLETE=2,R2_DISPLAY_EXPIRED=3};
typedef bool (*r2_display_checkpoint_fn)(void *,enum r2_display_stage);
int r2_display_region(const uint8_t *,unsigned x,unsigned y,unsigned w,unsigned h,
 uint64_t deadline_ms,r2_display_checkpoint_fn,void *);
int r2_display_button(void);
int r2_display_off(void);
int r2_display_brightness(uint8_t percent);
int r2_display_recover(void);
int r2_display_temperature(int16_t *quarter_c);
#endif

const char *r2_ui_servo_state(const struct r2_ui_model *,uint64_t now_ms);
