#include "cleanup_budget.h"
/* SPDX-License-Identifier: Apache-2.0
 * Backend dispatcher only. GM source/election/timing live in the unchanged
 * normal owner; RC7 has its own code, state, callbacks, queues and lifecycle.
 */
#include "rc_dynamic_service.h"
#include "rf_clock.h"
#include "rc_shadow.h"
#include "rc_unicast.h"
#include "runtime_profile.h"
#include "watchdog.h"
#include "ble_transport.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/atomic.h>
static struct r2_identity physical;
static struct r2_enrollment enrollment;
static atomic_t rc_active;
static bool configured, pending, target, resuming, running;
static bool rx_requested=true,rx_suspended;
static uint64_t until, incarnation, test_parent;
static uint32_t changes;
static int failure;
static uint64_t now_us(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }
bool r2_switch_rc_active(void) { return atomic_get(&rc_active) != 0; }
int r2_service_configure(const struct r2_identity *id,
                         const struct r2_enrollment *e) {
  if (configured || !id || !e || !e->rc_dynamic || !r2_group_valid(&e->group))
    return -EINVAL;
  if (e->rc7_boot_parent &&
      (!e->rc7_boot_rc || !e->rc7_exclude_direct_gm ||
       e->rc7_boot_parent == strtoull(id->silicon, NULL, 16)))
    return -EINVAL;
  physical = *id;
  enrollment = *e;
  incarnation = id->boot;
  test_parent = e->rc7_boot_parent;
  /* Retain the operator's common T096 boot defaults; the explicit flag also
   * selects RC for a diagnostic leaf or an otherwise unnamed simulator node. */
  target =
      e->rc7_boot_rc || r2_profile_default_rc(strtoull(id->silicon, NULL, 16));
  bool restored;
  if (r2_watchdog_restore_rc(&restored) && (!e->rc7_leaf || restored))
    target = restored;
  struct r2_enrollment gm = *e;
  gm.boundary_field = gm.field_profile = false;
  gm.local_capabilities = 3;
  int rc = r2_normal_service_configure(id, &gm);
  if (rc)
    return rc;
  if (target) {
    rc = r2_dynamic_service_configure(id, e);
    if (!rc)
      rc = r2_dynamic_service_parent_target(test_parent);
    if (rc)
      return rc;
    atomic_set(&rc_active, 1);
  }
  configured = true;
  return 0;
}
void r2_service_clock(const struct r2_clock *clock) {
  if (rx_suspended) {
    r2_service_supervise();
    return;
  }
  if (r2_switch_rc_active())
    r2_dynamic_service_clock(clock);
  else
    r2_normal_service_clock(clock);
  r2_service_supervise();
}
int r2_service_run(uint64_t at) {
  if(!rx_requested)return -EACCES;
  int rc = r2_switch_rc_active() ? r2_dynamic_service_run(at)
                                 : r2_normal_service_run(at);
  if (!rc)
    running = true;
  if (!rc)
    r2_watchdog_enable(true);
  return rc;
}
int r2_service_preflight(uint32_t seconds, uint64_t at) {
  if(!rx_requested)return -EACCES;
  if (pending || failure)
    return -EBUSY;
  return r2_switch_rc_active() ? r2_dynamic_service_preflight(seconds, at)
                               : r2_normal_service_preflight(seconds, at);
}
int r2_service_start(uint32_t seconds, uint64_t at) {
  if(seconds&&!rx_requested)return -EACCES;
  int rc = r2_switch_rc_active() ? r2_dynamic_service_start(seconds, at)
                               : r2_normal_service_start(seconds, at);
  if (!rc) {
    /* Preserve existing explicit controls; only automatic recovery is off. */
    r2_watchdog_enable(false);
  }
  return rc;
}
void r2_service_stop(void) {
  pending = resuming = false;
  running = false;
  r2_watchdog_enable(false);
  if (r2_switch_rc_active())
    r2_dynamic_service_stop();
  else
    r2_normal_service_stop();
}
int r2_service_close_result(void) {
  if(rx_suspended)return failure;
  return failure                 ? failure
         : r2_switch_rc_active() ? r2_dynamic_service_close_result()
                                 : r2_normal_service_close_result();
}
void r2_service_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now){
 if(!out)return;
 *out=(struct r2_rf_clock_sample){0};
 if(!configured||pending||failure||!rx_requested||rx_suspended)return;
 if(r2_switch_rc_active())r2_dynamic_service_rf_clock(out,at,now);
 else r2_normal_service_rf_clock(out,at,now);
}
static void ui_snapshot_profile(struct r2_service_status *out){
  out->profile_rc = r2_switch_rc_active();
  out->profile_target_rc = target;
  out->profile_changes = changes;
  out->profile_incarnation = incarnation;
  out->profile_pending = pending;
  if (pending || failure || !rx_requested) {
    out->serving = out->follower = out->follower_clock_valid = out->hot_ready =
        out->standby_authorized = false;
    out->precision.map_valid = out->precision.authorized =
        out->precision.locked = out->precision.rms_valid = false;
    out->service_until = out->authority_until = out->role_until = 0;
    out->source_eligible=out->gm_cold=false;
  }
  if (failure)
    out->error = failure;
  if(rx_suspended){out->normal=running;out->active=out->closing=false;out->closed=true;}
}
void r2_service_snapshot(struct r2_service_status *out){

  if (r2_switch_rc_active())
    r2_dynamic_service_snapshot(out);
  else
    r2_normal_service_snapshot(out);
  ui_snapshot_profile(out);
}
bool r2_service_snapshot_try(struct r2_service_status *out,r2_ui_read_boundary_fn boundary,void *context){
 bool ok=r2_switch_rc_active()?r2_dynamic_service_snapshot_try(out,boundary,context):r2_normal_service_snapshot_try(out,boundary,context);
 if(ok)ui_snapshot_profile(out);
 return ok;
}

void r2_service_ui_logic(struct r2_ui_model *m,const struct r2_service_status *s) {
  m->ble_local_holdover=false;
  if(r2_switch_rc_active())r2_dynamic_service_ui_logic(m,s);
  else r2_normal_service_ui_logic(m,s);
  m->profile_supported = true;
  m->profile_rc = r2_switch_rc_active();
  m->profile_switching = pending;
  m->ble_rx_supported=true;m->ble_rx_requested=rx_requested;
  if (pending || failure || !rx_requested) {
    m->scope_active = m->authenticated_gm = m->gm_source_fresh =
        m->ble_time_selected = false;
    m->clock.valid=m->coarse_valid=m->gm_cold=false;
    m->role = R2_UI_NO_ROLE;
    m->precision.map_valid = m->precision.authorized = m->precision.locked =
        false;
    m->ble_active=false;
    m->sync_attempt=m->local_hot_standby=false;
    m->precision_beat_hz=0;
#ifdef R2_GM7_NATIVE
    m->gm7_known=0;
    m->gm7_peer_valid=m->gm7_peer_current=m->gm7_peer_fresh=
        m->gm7_peer_eligible_claim=false;
#endif
  }
}
void r2_service_ui_details(struct r2_ui_model *m,const struct r2_service_status *s){
  if(r2_switch_rc_active())r2_dynamic_service_ui_details(m,s);
  else r2_normal_service_ui_details(m,s);
}
void r2_service_ui(struct r2_ui_model *m) {
  struct r2_service_status s;r2_service_snapshot(&s);
  r2_service_ui_logic(m,&s);r2_service_ui_details(m,&s);
}
static int transition(bool next, uint64_t arrival) {
  uint64_t now = now_us();
  if (arrival > now / 1000 || now / 1000 - arrival >= R2_REPLY_MS)
    return -EINVAL;
  if (!running || pending || failure || changes >= 65535)
    return -EBUSY;
  if (enrollment.rc7_leaf && !next)
    return -ENOTSUP;
  target = next;
  pending = true;
  until = now + R2_CLEANUP_US;
  r2_shadow_normal_withdraw();
  if (r2_switch_rc_active())
    r2_dynamic_service_stop();
  else
    r2_normal_service_stop();
  return 0;
}
int r2_service_profile(unsigned mode, uint64_t arrival) {
  uint64_t now = now_us();
  if (mode > R2_PROFILE_TOGGLE || arrival > now / 1000 ||
      now / 1000 - arrival >= R2_REPLY_MS)
    return -EINVAL;
  if (!running || pending || failure || changes >= 65535)
    return -EBUSY;
  bool next = mode == R2_PROFILE_TOGGLE ? !r2_switch_rc_active()
                                        : mode == R2_PROFILE_RC;
  if (next == r2_switch_rc_active())
    return pending || failure ? -EBUSY : 0;
  if(rx_suspended){
    if(enrollment.rc7_leaf&&!next)return -ENOTSUP;
    target=next;atomic_set(&rc_active,next);++changes;
    r2_watchdog_enable(false);
    return 0; /* Select the next profile without starting either BLE owner. */
  }
  int rc = transition(next, arrival);
  if (!rc)
    r2_watchdog_enable(true); /* An explicit mode change resumes normal service. */
  return rc;
}
int r2_service_restart(uint64_t arrival) {
  /* Separate from the idempotent public mode selector. Same profile, fresh
   * scope; the existing asynchronous close/join/PT-ack gates remain intact. */
  if(!rx_requested)return -EACCES;
  return transition(r2_switch_rc_active(), arrival);
}
int r2_service_ble_rx(bool enabled,uint64_t arrival){
 uint64_t now=now_us();
 if(arrival>now/1000||now/1000-arrival>=R2_REPLY_MS)return -EINVAL;
 if(!running||pending||failure)return -EBUSY;
 if(enabled==rx_requested)return 0;
 int rc=transition(r2_switch_rc_active(),arrival);
 if(!rc){rx_requested=enabled;r2_watchdog_enable(false);}
 return rc;
}
bool r2_service_ble_rx_requested(void){return rx_requested;}
int r2_service_test_parent(uint64_t parent, uint64_t arrival) {
  if (!r2_switch_rc_active() ||
      (parent && parent == strtoull(physical.silicon, NULL, 16)))
    return -EINVAL;
  int rc = transition(true, arrival);
  if (!rc)
    test_parent = parent;
  return rc;
}
void r2_service_test_parent_status(uint64_t *parent, bool *relay_only) {
  *parent = test_parent;
  *relay_only = enrollment.rc7_exclude_direct_gm;
}
void r2_service_test_transport_status(uint32_t out[13]) {
  if (r2_switch_rc_active())
    r2_dynamic_service_transport_snapshot(out);
  else
    for (unsigned i = 0; i < 13; ++i)
      out[i] = 0;
}
void r2_service_profile_status(uint32_t out[5]) {
  out[0] = r2_switch_rc_active();
  out[1] = pending;
  out[2] = target;
  out[3] = pending ? 1 : 0;
  out[4] = changes;
}
void r2_service_test_acquisition_status(uint32_t out[20]) {
  if (r2_switch_rc_active())
    r2_dynamic_service_acquisition_snapshot(out);
  else
    memset(out, 0, 20 * sizeof *out);
}
void r2_service_test_gm_admission_status(uint32_t out[7]) {
  if (r2_switch_rc_active())
    r2_dynamic_service_gm_admission_snapshot(out);
  else
    memset(out, 0, 7 * sizeof *out);
}
void r2_service_test_connection_status(uint32_t out[20]) {
  if (r2_switch_rc_active())
    r2_dynamic_service_connection_snapshot(out);
  else
    memset(out, 0, 20 * sizeof *out);
}
void r2_service_supervise(void) {
  if(rx_suspended&&!pending)return;
  if (!rx_suspended&&r2_switch_rc_active())
    r2_dynamic_service_supervise();
  else if(!rx_suspended)
    r2_normal_service_supervise();
  if (!pending)
    return;
  if (resuming) {
    struct r2_service_status s;
    r2_normal_service_snapshot(&s);
    if (s.error) {
      failure = s.error;
      pending = resuming = false;
      return;
    }
    if (s.profile_pending || !s.active || s.closed) {
      if (now_us() < until)
        return;
      failure = -ETIMEDOUT;
      pending = resuming = false;
      r2_normal_service_stop();
      return;
    }
    incarnation = s.profile_incarnation;
    /* Boot-only diagnostic. Clear only after the GM owner actually resumed;
     * reboot reloads immutable enrollment, including an optional boot selector.
     * Harness overrides remain RAM-only.
     */
    enrollment.rc7_exclude_direct_gm = false;
    test_parent = 0;
    ++changes;
    pending = resuming = false;
    rx_suspended=false;r2_watchdog_enable(true);
    return;
  }
  int rc = rx_suspended ? 0 : r2_switch_rc_active() ? r2_dynamic_service_close_result()
                                 : r2_normal_service_close_result();
  if (rc == -EAGAIN) {
    if (now_us() < until)
      return;
    rc = -ETIMEDOUT;
  }
  if (rc || now_us() >= until) {
    failure = rc ? rc : -ETIMEDOUT;
    pending = false;
    return;
  }
  /* The old owner has joined. RC-only resources must be fully absent before
   * either profile can acquire its next scope, including same-profile retry.
   * This check cannot extend the original two-second drain deadline. */
  if (!r2_uc_quiescent()) {
    failure = -EBUSY;
    pending = false;
    return;
  }
  /* Deliberate OFF joins the old owner and stays parked. In particular, do
   * not resume GM election, configure RC, advance incarnation or restart the
   * UI/time/LoRa owners. Re-enable alone requests fresh authenticated scope. */
  if(!rx_requested){
    rc=r2_ble_rx_policy(false);
    if(rc){failure=rc;pending=false;return;}
    rx_suspended=true;pending=false;r2_watchdog_enable(false);
    return;
  }
  uint64_t next = r2_profile_incarnation(physical.boot, changes + 1);
  if (!next) {
    failure = -EOVERFLOW;
    pending = false;
    return;
  }
  /* The receive policy is immutable throughout each active incarnation.
   * A context change first drains every old owner under its original bound.
   * Resumption cannot reuse the prior admission, PA map or mesh selection. */
  rc=r2_ble_rx_policy(rx_requested);
  if(rc){failure=rc;pending=false;return;}
  rx_suspended=false;
  if (target) {
    struct r2_identity id = physical;
    id.boot = next;
    rc = r2_dynamic_service_configure(&id, &enrollment);
    if (!rc)
      rc = r2_dynamic_service_parent_target(test_parent);
    if (!rc) {
      atomic_set(&rc_active, 1);
      rc = r2_dynamic_service_run(now_us() / 1000);
    }
  } else {
    rx_suspended=false;
    atomic_clear(&rc_active);
    rc = r2_normal_service_resume_gm(next, changes);
    if (!rc) {
      resuming = true;
      until = now_us() + R2_CLEANUP_US;
      return;
    }
  }
  if (rc) {
    failure = rc;
    pending = false;
    return;
  }
  incarnation = next;
  ++changes;
  pending = false;
  r2_watchdog_enable(!rx_suspended);
}
int r2_service_name(const char *name) {
  return r2_switch_rc_active() ? r2_dynamic_service_name(name)
                               : r2_normal_service_name(name);
}
int r2_service_relay(uint64_t session, uint32_t seconds, uint64_t at) {
  (void)session;
  (void)seconds;
  (void)at;
  return -ENOTSUP;
}
int r2_service_relay_cancel(uint64_t session) {
  (void)session;
  return -ENOTSUP;
}
uint32_t r2_service_stack_unused(void) {
  return r2_switch_rc_active() ? r2_dynamic_service_stack_unused()
                               : r2_normal_service_stack_unused();
}

bool r2_service_ui_details_try(struct r2_ui_model *m,const struct r2_service_status *out,
 r2_ui_read_boundary_fn boundary,void *context){
 return r2_switch_rc_active()?r2_dynamic_service_ui_details_try(m,out,boundary,context):
  r2_normal_service_ui_details_try(m,out,boundary,context);
}
bool r2_service_ui_lock_probe(unsigned owner){
 return owner==R2_UI_READ_GM?r2_normal_service_ui_lock_probe():
  owner==R2_UI_READ_RC?r2_dynamic_service_ui_lock_probe():false;
}
bool r2_service_rf_clock_try(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now,
 r2_ui_read_boundary_fn boundary,void *context){
 if(!out)return false;
 if(!configured||pending||failure||!rx_requested||rx_suspended){*out=(struct r2_rf_clock_sample){0};return true;}
 return r2_switch_rc_active()?r2_dynamic_service_rf_clock_try(out,at,now,boundary,context):
  r2_normal_service_rf_clock_try(out,at,now,boundary,context);
}
