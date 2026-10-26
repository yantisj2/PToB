/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_service_fake.h"
/* Include production only after the public platform boundary. */
#include "../src/gm7_service.c"
#include "gm7_promote.h"

static bool fake_ble_closed = true, fake_pt_closed = true,
            fake_relay_closed = true, fake_hf_valid = true;
static unsigned fake_submit_count, fake_completed_count, fake_cancel_count;
static bool fake_native_live[GM7_JOB_SLOTS], fake_native_exposed[GM7_JOB_SLOTS];
static struct r2_gm7_tx fake_result[GM7_JOB_SLOTS];
static unsigned fake_result_count;
static struct r2_gm7_timing fake_timing;
static uint64_t fake_direct_contact[4];
void r2_radio_service_gm7_contact(uint64_t *root,uint64_t *boot,uint64_t *tick,uint64_t *epoch){
 if(fake_direct_contact[0]){*root=fake_direct_contact[0];*boot=fake_direct_contact[1];*tick=fake_direct_contact[2];*epoch=fake_direct_contact[3];return;}
 *root=fake_timing.scope.root_identity;*boot=fake_timing.scope.boot;
 *tick=fake_timing.last_correction;*epoch=fake_timing.counter_epoch;
}
static struct r2_source_clock fake_raw;
static bool fake_promote_ready;
static bool fake_handoff_enforced, fake_handoff_valid;
static bool fake_submit_refusal;
static bool fake_submit_current_lead;
static uint32_t fake_submit_advance;
static uint32_t fake_poll_advance;
static int fake_capture_error, fake_gnss_error, fake_group_start_result;
static int fake_reset_result, fake_relay_reset_result;
static uint32_t fake_capture_advance, fake_clock_epoch = 4;
static bool fake_stop_on_reset;
int r2_capture_error(void) { return fake_capture_error; }
int r2_gnss_input_error(void) { return fake_gnss_error; }
static struct r2_radio_scope fake_scope;
static struct r2_enrollment fake_path;
static struct r2_gm7_admission_source fake_join;
static struct r2_group_client fake_client;
static int fake_ble_error, fake_pt_error, fake_pt_supervise;
int r2_ble_group_start(uint64_t id, uint64_t g, uint32_t e,
                       const uint8_t a[6]) {
  (void)id;
  (void)g;
  (void)e;
  (void)a;
  if (fake_group_start_result)
    return fake_group_start_result;
  fake_ble_closed = false;
  return 0;
}
void r2_ble_gm7_server(bool enabled) { (void)enabled; }
int r2_ble_gm7_scan(bool aligned) {
  (void)aligned;
  return 0;
}
int r2_ble_owner_bind(struct k_thread *t) {
  (void)t;
  return 0;
}
void r2_ble_owner_deadline(uint64_t until) { (void)until; }
void r2_ble_poll(uint64_t now) {
  (void)now;
  fake_hf += fake_poll_advance;
  fake_kernel += fake_poll_advance;
  fake_poll_advance = 0;
}
void r2_ble_stop(void) { fake_ble_closed = true; }
bool r2_ble_closed(void) { return fake_ble_closed; }
int r2_ble_stop_error(void) { return fake_ble_error; }
uint32_t r2_ble_backlog(void) { return 0; }
uint32_t r2_ble_drops(void) { return 0; }
uint32_t r2_ble_receipts(void) { return 0; }
uint32_t r2_ble_errors(void) { return 0; }
unsigned r2_ble_admission_peer(void) { return 4; }
bool r2_ble_receive(struct r2_ble_event *e) {
  (void)e;
  return false;
}
void r2_ble_fault_snapshot(struct r2_transport_fault *s) {
  memset(s, 0, sizeof *s);
}
void r2_ble_lamps(uint8_t o[8], uint64_t now) {
  (void)now;
  memset(o, 0, 8);
}
void r2_ble_timing_lamps(bool tx, bool corr, uint64_t now) {
  (void)tx;
  (void)corr;
  (void)now;
}
void r2_ble_serving(bool serving) { (void)serving; }
bool r2_capture_now(struct r2_capture *c) {
  fake_hf += fake_capture_advance;
  fake_kernel += fake_capture_advance;
  fake_capture_advance = 0;
  *c = (struct r2_capture){fake_hf * 16, fake_clock_epoch, R2_CAPTURE_NOW};
  return fake_hf_valid;
}
#ifndef R2_GM7_REAL_CLOCK
bool r2_gm7_clock_now(uint64_t *us, uint32_t *epoch) {
  *us = fake_hf;
  *epoch = fake_clock_epoch;
  return fake_hf_valid;
}
bool r2_gm7_clock_bounds(uint32_t raw, uint32_t epoch, unsigned prefix,
                         unsigned chain, uint64_t *lo, uint64_t *hi) {
  if (!fake_hf_valid || epoch != 4)
    return false;
  uint64_t captured=raw;
#ifdef R2_GM7_COHORT
  /* Long cohort runs cross TIMER32 wraps. The model supplies the actual
   * completed capture near now; production clock tests verify its resolver. */
  captured=(fake_hf*16&~UINT64_C(0xffffffff))|raw;
  if(captured>fake_hf*16)captured-=UINT64_C(1)<<32;
#endif
  if(captured<prefix*16+chain*16+1)return false;
  *lo = captured - prefix * 16 - chain * 16 - 1;
  *hi = captured - prefix * 16 + chain * 16 + 1;
  return true;
}
#endif
static uint32_t fake_overflow;
uint32_t r2_gm7_capture_overflows(void) { return fake_overflow; }
static struct r2_gm7_rx fake_rx[3];
static unsigned fake_rx_count;
bool r2_gm7_capture_pop(struct r2_gm7_rx *r) {
  if (!fake_rx_count)
    return false;
  *r = fake_rx[--fake_rx_count];
  return true;
}
void r2_gm7_controller_scan(bool e, bool p, uint64_t f) {
  (void)e;
  (void)p;
  (void)f;
}
void r2_gm7_controller_source(uint64_t until, bool live) {
  (void)until;
  (void)live;
}
void r2_gm7_controller_cancel(uint64_t now) {
  (void)now;
  ++fake_cancel_count;
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    if (scheduler.job[i].positive)
      fake_native_live[i] = false;
}
static bool fake_precision_enabled;
static uint32_t fake_precision_phase,fake_precision_train;
static uint64_t fake_precision_until;
void r2_gm7_controller_precision(bool e, uint32_t p, uint64_t d, uint32_t t) {
  fake_precision_enabled=e;fake_precision_phase=p;fake_precision_until=d;fake_precision_train=t;
}
int r2_gm7_controller_submit(unsigned slot, struct gm7_scheduler *s,
                             const uint8_t a[6], uint64_t now) {
  (void)s;
  (void)a;
  (void)now;
  assert(!fake_native_live[slot]);
  fake_hf += fake_submit_advance;
  fake_kernel += fake_submit_advance;
  fake_submit_advance = 0;
  if (fake_submit_current_lead && s->job[slot].window.nominal < fake_hf + 3000)
    return -ETIMEDOUT;
  if (fake_submit_refusal)
    return -EBUSY;
  fake_native_live[slot] = true;
  s->job[slot].state = GM7_JOB_DISPATCHED;
  ++fake_submit_count;
  return 0;
}
int r2_gm7_controller_drain(unsigned slot, uint64_t now) {
  (void)now;
  return fake_native_live[slot] ? -EAGAIN : 0;
}
bool r2_gm7_controller_idle(void) {
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    if (fake_native_live[i])
      return false;
  return true;
}
bool r2_gm7_controller_exposed(unsigned slot, uint64_t ticket) {
  (void)ticket;
  return fake_native_exposed[slot];
}
bool r2_gm7_controller_result(struct r2_gm7_tx *out) {
  if (!fake_result_count)
    return false;
  *out = fake_result[--fake_result_count];
  return true;
}
void r2_group_admission_reset(void) {}
void r2_group_admission_gm7_tick(const struct r2_gm7_admission_source *s,
                                 uint64_t now) {
  (void)now;
  fake_join = *s;
}
void r2_group_admission_gm7_receive(const struct r2_gm7_admission_source *s,
                                    const struct r2_ble_event *e,
                                    uint64_t now) {
  (void)s;
  (void)e;
  (void)now;
}
void r2_lora_origin(bool serving, uint64_t b, uint32_t e) {
  (void)serving;
  (void)b;
  (void)e;
}
int r2_radio_service_init(unsigned self, const struct r2_enrollment *e) {
  (void)self;
  fake_path = *e;
  return 0;
}
void r2_radio_service_continuous(bool e) { (void)e; }
static unsigned fake_stop_calls,fake_activate_calls,fake_handoff_calls;
static uint64_t fake_handoff_root,fake_handoff_boot;
static uint32_t fake_handoff_epoch;
void r2_radio_service_activate(void) { ++fake_activate_calls;fake_pt_closed = false; }
void r2_radio_service_stop(uint64_t now) {
  ++fake_stop_calls;
  (void)now;
  fake_pt_closed = true;
}
void r2_radio_service_withdraw(void) { fake_scope.gm = false; }
bool r2_radio_service_closed(void) { return fake_pt_closed; }
int r2_radio_service_error(void) { return fake_pt_error; }
int r2_radio_service_supervise(uint64_t now) {
  (void)now;
  return fake_pt_supervise;
}
bool r2_radio_service_serving(uint64_t now) {
  return fake_scope.gm && now < fake_scope.until_us;
}
int r2_radio_service_reset(uint32_t g) {
  (void)g;
  if (fake_stop_on_reset) {
    fake_stop_on_reset = false;
    r2_service_stop();
  }
  return fake_reset_result ? fake_reset_result : fake_pt_closed ? 0 : -EAGAIN;
}
void r2_radio_service_link_quality(struct r2_link_quality_view *v) {
  memset(v, 0, sizeof *v);
}
void r2_radio_service_snapshot(struct r2_radio_precision_view *v,
                               struct r2_group_client *t,
                               struct r2_group_client *c, uint64_t *f,
                               uint64_t n) {
  (void)n;
  memset(v, 0, sizeof *v);
  memset(t, 0, sizeof *t);
  memset(c, 0, sizeof *c);
  *f = 0;
}
void r2_radio_service_source(const struct r2_source_clock *s, bool ready,
                             uint32_t e) {
  (void)s;
  (void)ready;
  (void)e;
}
void r2_radio_service_client(const struct r2_group_client *a,
                             const struct r2_enrollment *p, uint64_t now) {
  fake_client = *a;
  (void)now;
  fake_path = *p;
}
void r2_radio_service_role(const struct r2_role_record *r,
                           const struct r2_group_config *g) {
  (void)g;
  assert(!(r->flags & ~R2_ROLE_SERVING));
}
void r2_radio_service_tick_reference(struct r2_radio_scope s,
                                     const struct r2_source_clock *c, int64_t u,
                                     uint64_t n,
                                     const struct r2_gnss_record *g) {
  (void)c;
  (void)u;
  (void)n;
  (void)g;
  fake_scope = s;
}
static uint64_t fake_resume_until;
static void (*fake_after_publish)(void);
void r2_radio_service_gm7_publish(struct r2_radio_scope s,const struct r2_group_client *c,
 const struct r2_enrollment *p,int64_t utc,uint64_t at,uint64_t resume_until){
 fake_scope=s;fake_client=*c;fake_path=*p;fake_resume_until=resume_until;
 (void)utc;(void)at;
}
void r2_radio_service_gm7_tick(uint64_t now,const struct r2_gnss_record *g){
 (void)now;(void)g;if(fake_after_publish)fake_after_publish();
}
void r2_radio_service_gm7_state(bool *stopped,bool *managed,bool *binding){
 *stopped=false;*managed=true;*binding=fake_scope.gm||fake_scope.follower;
}
void r2_radio_service_handoff_request(uint64_t r, uint64_t b, uint32_t e) {
  ++fake_handoff_calls;fake_handoff_root=r;fake_handoff_boot=b;fake_handoff_epoch=e;
  (void)r;
  (void)b;
  (void)e;
}
bool r2_radio_service_handoff_ready(uint64_t r, uint64_t b, uint32_t e) {
  (void)r;
  (void)b;
  (void)e;
  return fake_pt_closed && (!fake_handoff_enforced || fake_handoff_valid);
}
void r2_radio_service_gm7_follow_diagnostic(struct r2_gm7_follow_diagnostic *d,uint64_t now){(void)now;memset(d,0,sizeof *d);}
void r2_radio_service_gm7_timing(struct r2_gm7_timing *t, uint64_t now) {
  (void)now;
  *t = fake_timing;
}
int r2_radio_service_gm7_promote(struct r2_source_clock *c, uint64_t r,
                                 uint64_t b, uint32_t e, uint32_t t, uint32_t s,
                                 unsigned a, struct r2_capture n) {
  (void)c;
  (void)r;
  (void)b;
  (void)e;
  (void)t;
  (void)s;
  (void)a;
  (void)n;
#ifdef R2_GM7_COHORT
  extern int gm7_cohort_promote(struct r2_source_clock *,uint64_t,uint64_t,uint32_t,uint32_t,uint32_t,unsigned,struct r2_capture);
  return gm7_cohort_promote(c,r,b,e,t,s,a,n);
#endif
  if (fake_promote_ready)
    return gm7_promote_clock(c, &fake_timing.map, fake_timing.utc_offset_ns, n,
                             s, a);
  return -EAGAIN;
}
int r2_radio_service_gm7_phase_step(int step) {
  (void)step;
  return 0;
}
int r2_relay_service_init(unsigned self, const struct r2_enrollment *r,
                          uint64_t b, void (*wake)(void)) {
  (void)self;
  (void)r;
  (void)b;
  (void)wake;
  return 0;
}
int r2_relay_service_profile_reset(uint64_t b) {
  (void)b;
  return fake_relay_reset_result ? fake_relay_reset_result
         : fake_relay_closed     ? 0
                                 : -EAGAIN;
}
void r2_relay_service_stop(uint64_t now) {
  (void)now;
  fake_relay_closed = true;
}
bool r2_relay_service_closed(void) { return fake_relay_closed; }
void r2_time_source_snapshot(struct r2_source_clock *s) { *s = fake_raw; }
static struct gm7_source source_at(uint64_t t) {
  return (struct gm7_source){.utc_us = UINT64_C(1792800000000000) + t,
                             .sampled_us = t,
                             .until_us = t + 1500000,
                             .pps_at = t - t % 1000000,
                             .source_gen = 3,
                             .capture_gen = 4,
                             .association_gen = 1,
                             .quality = {128, 2, 255, 128, 65535},
                             .gnss_healthy = true,
                             .pps_healthy = true,
                             .utc_available = true,
                             .physical_ready = true};
}
static void tick_to(uint64_t t) {
  assert(t >= fake_hf);
  fake_kernel += t - fake_hf;
  fake_hf = t;
}
static void setup(bool serving) {
  memset(fake_direct_contact,0,sizeof fake_direct_contact);
  memset(&fake_join,0,sizeof fake_join);
  memset(&fake_scope,0,sizeof fake_scope);
  memset(&fake_client,0,sizeof fake_client);
  memset(&fake_timing,0,sizeof fake_timing);
  memset(&owner, 0, sizeof owner);
  memset(&scheduler, 0, sizeof scheduler);
  memset(&status, 0, sizeof status);
  memset(&diagnostic, 0, sizeof diagnostic);
  memset(&yield_request,0,sizeof yield_request);
  memset(peer_display, 0, sizeof peer_display);
  memset(&event_evidence, 0, sizeof event_evidence);
  memset(fake_native_live, 0, sizeof fake_native_live);
  memset(fake_native_exposed, 0, sizeof fake_native_exposed);
  fake_result_count = fake_submit_count = fake_completed_count = fake_overflow =
      overflow_seen = 0;
  fake_hf_valid = true;
  fake_clock_epoch = 4;
  fake_ble_closed = fake_pt_closed = false;
  fake_relay_closed = true;
  fake_hf = 66000000;
  fake_kernel = 500000000;
  clock_epoch = 4;
  identity = 17;
  boot = 33;
  group = (struct r2_group_config){
      .id = UINT64_C(0x4752503753545542), .epoch = 7, .psk = {1}};
  roster.map_bound_us = 40000;
  roster.identity[0] = identity;
  assert(!gm7_owner_init(&owner, &group, identity, boot, true, 0));
  struct gm7_preparation p = {0};
  for (uint64_t t = 0; t <= 66000000; t += 1000000) {
    struct gm7_source src = source_at(t);
    gm7_owner_update(&owner, &src, &p, t);
  }
  /* Downstream service cases start after a matured election prerequisite.
   * Actual unseeded startup/quiet timing is covered by gm7_test and cohort. */
  owner.quiet_since=fake_hf-GM7_QUIET_US-500000;
  struct gm7_source ready=source_at(fake_hf);
  gm7_owner_update(&owner,&ready,&p,fake_hf);
  if (serving)
    assert(!gm7_owner_activate(&owner, fake_hf, fake_hf + 4000, fake_hf));
  gm7_scheduler_init(&scheduler, fake_hf);
  gm7_owner_reservation_guard(&owner, reservation_fence, NULL);
  configured = started = used = true;
  status.configured = status.active = true;
  status.normal = true;
  status.root = serving ? 0 : 255;
  status.serving = serving;
  stop_begun = transport_stopped = resource_fault = scan_aligned = resuming =
      false;
  fake_submit_refusal = false;
  fake_submit_current_lead = false;
  fake_submit_advance = 0;
  fake_poll_advance = 0;
  fake_capture_error = fake_gnss_error = fake_group_start_result = 0;
  fake_reset_result = fake_relay_reset_result = 0;
  fake_capture_advance = 0;
  fake_stop_on_reset = false;
  reset_generation = 0;
  event_evidence.phase_sample_ms = (uint32_t)(fake_hf / 1000);
  atomic_clear(&stopping);
  atomic_clear(&busy);
  phase_known = true;
  last_phase = 0;
  last_enclosure = 500;
  event_generation = owner.event_generation;
  strcpy(name, "GM7 fixture");
}
static void finish(unsigned slot, bool complete, bool exposed) {
  struct gm7_job *j = &scheduler.job[slot];
  uint64_t nominal = j->window.nominal;
  assert(fake_native_live[slot]);
  if(fake_hf<nominal+1500)tick_to(nominal+1500);
  fake_native_live[slot] = false;
  fake_native_exposed[slot] = exposed;
  fake_result[fake_result_count++] =
      (struct r2_gm7_tx){.ticket = j->ticket,
                         .generation = j->generation,
                         .buffer_version = j->buffer_version,
                         .epoch = 4,
                         .primary_address = (uint32_t)((nominal + 42) * 16),
                         .aux_address = (uint32_t)((nominal + 551) * 16),
                         .aux_end = (uint32_t)((nominal + 1400) * 16),
                         .slot = slot,
                         .complete = complete,
                         .exposed = exposed};
  struct gm7_bridge b;
  assert(bridge(&b));
  results(b);
  if (complete)
    ++fake_completed_count;
}
static void shutdown_copies(unsigned completed_before_stop) {
  setup(true);
  tick_to(66033000);
  if (completed_before_stop) {
    gm7_owner_withdraw(&owner, GM7_R_SOURCE, fake_hf);
    struct gm7_bridge b;
    assert(bridge(&b));
    assert(!gm7_owner_event_align(&owner, 66038000, fake_hf));
    assert(!queue(0, &owner.event.packet, window(66038000, 500),
                  owner.event.deadline, b));
    owner.event.sent = 1;
    finish(0, true, true);
  }
  r2_service_stop();
  assert(!status.serving && atomic_get(&stopping));
  uint64_t sequence =
      owner.event.valid ? owner.event.packet.body.control.sequence : 0;
  for (unsigned i = completed_before_stop; i < 3; i++) {
    tick_to(UINT64_C(66033000) + (uint64_t)i * 250000);
    cycle(&service_work);
    assert(owner.event.valid && owner.event.negative && !owner.serving);
    if (!sequence)
      sequence = owner.event.packet.body.control.sequence;
    assert(sequence == owner.event.packet.body.control.sequence);
    assert(fake_native_live[0]);
    finish(0, true, true);
  }
  tick_to(fake_hf + 1000);
  cycle(&service_work);
  assert(status.closed && r2_service_close_result() == 0 &&
         owner.event.sent == 3);
  assert(fake_submit_count == 3 && fake_completed_count == 3);
  assert(event_evidence.queued == 3 && event_evidence.completed == 3 &&
         !event_evidence.failed);
  puts("PASS actual GM service immediate output stop and three immutable "
       "next-primary WITHDRAW copies before acknowledged final drain");
}
static void provisional_exposure(void) {
  for (unsigned exposed = 0; exposed < 2; exposed++) {
    setup(false);
    struct gm7_bridge b;
    assert(bridge(&b));
    assert(
        !gm7_owner_reserve(&owner, fake_hf + 10000, fake_hf + 14000, fake_hf));
    struct gm7_packet p;
    assert(!gm7_owner_planned_control(&owner, &p, fake_hf));
    assert(!queue(0, &p, window(fake_hf + 10000, 500), 0, b));
    uint64_t birth = owner.reserved_scope.birth;
    fake_native_exposed[0] = exposed;
    gm7_owner_reservation_cancel(&owner, fake_hf);
    assert(!owner.reserved && !owner.serving && !fake_native_live[0]);
    if (exposed) {
      assert(owner.scope.birth == birth && owner.event.negative &&
             owner.event.packet.kind == GM7_WITHDRAW);
    } else
      assert(!owner.scope.birth);
  }
  setup(false);
  struct gm7_bridge b;
  assert(bridge(&b));
  assert(!gm7_owner_reserve(&owner, fake_hf + 10000, fake_hf + 14000, fake_hf));
  struct gm7_packet p;
  assert(!gm7_owner_planned_control(&owner, &p, fake_hf));
  assert(!queue(0, &p, window(fake_hf + 10000, 500), 0, b));
  finish(0, true, true);
  assert(owner.serving && owner.active_since == fake_hf &&
         owner.first_announce_end < owner.active_since);
  assert(!fake_join.serving); /* admission is not enabled before verified owner
                                 commit */
  puts("PASS actual service reserve/cancel immutable controller ticket, "
       "unexposed discard, AUX-exposed retained lineage/WITHDRAW, measured "
       "complete first ANN and unbackdated ACTIVE");
}
static void diagnostics_and_remap(void) {
  setup(true);
  owner.armed = true;
  owner.armed_since = fake_hf - 2300000;
  owner.serving = false;
  owner.pps_since = fake_hf - 5000000;
  struct gm7_bridge b;
  assert(bridge(&b));
  publish(b);
  assert(diagnostic.at == fake_kernel && diagnostic.hot_ms == 2300 &&
         diagnostic.pps_ms == 5000);
  struct r2_ui_model m = {0};
  r2_service_ui(&m);
  assert(m.gm7_owner && m.gm7_at_ms == fake_kernel / 1000 &&
         m.gm7_hot_ms == 2300);
  m.sampled_ms = fake_kernel / 1000;
  m.local_hot_standby = true;
  m.clock.pps_present = true;
  m.clock.pps_age_us = 0;
  struct r2_ui_holds holds = {0};
  r2_ui_holds_update(&holds, &m, fake_kernel / 1000);
  assert(holds.hot_since && fake_kernel / 1000000 - (holds.hot_since - 1) == 3);
  char text[2048];
  for (unsigned page = 0; page <= GM7_PEERS; page++) {
    assert(r2_gm7_diagnostic_format(text, sizeof text, page, "TEST-SOURCE",
                                    0xabc, true) > 0);
    assert(strstr(text, "\"source\":\"TEST-SOURCE\"") &&
           strstr(text, "\"boot\":\"0000000000000abc\"") &&
           strstr(text, "\"page\":"));
    assert(r2_gm7_diagnostic_format(text, sizeof text, page, "TEST-SOURCE",
                                    0xabc, false) > 0);
    assert(strstr(text, "\"available\":false"));
  }
  assert(r2_gm7_diagnostic_format(text, sizeof text, GM7_PEERS + 2, "TEST", 1,
                                  true) == -EINVAL);
  assert(r2_gm7_diagnostic_format(text,sizeof text,9,"TEST",1,true)>0&&
      strstr(text,"GM7_FOLLOWER_ESTIMATOR")&&strstr(text,"\"authority\":false"));
  assert(r2_gm7_diagnostic_format(text,8,9,"TEST",1,true)==-ENOSPC);
  assert(gm7_duration_ms(UINT64_C(4294967295000), 0) == UINT32_MAX - 1 &&
         gm7_duration_ms(UINT64_MAX, 0) == UINT32_MAX - 1 &&
         gm7_duration_ms(0, 1) == UINT32_MAX);
  owner.armed = false;
  owner.scope = (struct gm7_scope){.birth = UINT64_C(1792800000000000),
                                   .founder = 99,
                                   .active = UINT64_C(0xfeed123400000099),
                                   .boot = 987,
                                   .term = 1,
                                   .timing_epoch = 22,
                                   .train = 23,
                                   .source_gen=3,.capture_gen = 4};
  struct gm7_peer *peer = &owner.peer[0];
  peer->used = true;
  peer->identity = owner.scope.active;
  peer->boot = 987;
  peer->kind=GM7_ANNOUNCE;
  peer->received.control = (struct gm7_control){.sequence=1,.max_age_us=1000000,.sender=peer->identity,
      .boot=peer->boot, .active=peer->identity, .active_boot=peer->boot,
      .birth_us=owner.scope.birth, .founder=owner.scope.founder,
      .term=owner.scope.term, .source_gen=3, .capture_gen=4, .train_gen=23,
      .source_valid_us=1000000,.timing_epoch=22};
  peer->received.control.sent_utc_us = UINT64_C(1792800000000000) + fake_hf;
  peer->received.control.sent_mono_us = fake_hf;
  peer->received.control_until = fake_hf + 1000000;
  struct gm7_source src = source_at(fake_hf);
  struct r2_source_clock map = {0};
  struct r2_clock_view cv = {0};
  timing(b, &src, &map, &cv);
  assert(fake_path.identity[1] == peer->identity &&
         fake_scope.root_identity == peer->identity && fake_scope.root == 1 &&
         fake_scope.until_us == INT64_MAX && !fake_client.source);
  puts("PASS actual service separate HF/kernel timer origins, finite duration "
       "saturation, all diagnostic source/boot/page bindings, RC unavailable "
       "view and selected full64 identity outside enrolled roster");
}
static void healthy_input(void) {
  struct r2_pps_precision pps;
  r2_pps_precision_init(&pps);
  r2_source_clock_init(&functional_source);
  uint64_t edge = fake_hf - fake_hf % 1000000;
  for (uint64_t t = edge - 2000000; t <= edge; t += 1000000) {
    assert(r2_pps_precision_add(
        &pps, (struct r2_capture){t * 16, 4, R2_CAPTURE_PPS}));
    if (pps.rate_valid)
      assert(r2_source_clock_publish(
          &functional_source, &pps,
          (struct r2_capture){t * 16 + 1, 4, R2_CAPTURE_NOW}));
  }
  struct r2_capture at = {fake_hf * 16, 4, R2_CAPTURE_NOW};
  assert(
      r2_source_utc_bind(&functional_source, at,
                         (INT64_C(1792800000000000) + (int64_t)fake_hf) * 1000,
                         3, R2_ASSOC_UNKNOWN) == 1);
  memset(&input, 0, sizeof input);
  input.view = (struct r2_clock_view){.valid = true,
                                      .fix_active = true,
                                      .pps_present = true,
                                      .source_available = true,
                                      .source_epoch = 3,
                                      .mono_us = (int64_t)fake_kernel};
  input.last_pps = (int64_t)(fake_kernel - fake_hf % 1000000);
  input.last_rmc = (int64_t)fake_kernel;
}
static struct r2_clock raw_clock(void) {
  struct r2_clock c = {0};
  c.view = input.view;
  c.view.mono_us = (int64_t)fake_kernel;
  c.last_pps = (int64_t)(fake_kernel - fake_hf % 1000000);
  c.last_rmc = (int64_t)fake_kernel;
  c.last_rmc_utc = INT64_C(1792800000000000) + (int64_t)fake_hf;
  return c;
}
static void functional_map_refresh(void) {
  setup(false);
  healthy_input();
  fake_raw = functional_source;
  struct r2_clock c = raw_clock();
  promoted_valid = true;
  promoted_pps = fake_raw.last_edge;
  /* A retained handoff has an actual phase/rate distinct from the raw GNSS fit.
   */
  for (unsigned i = 0; i < functional_source.count; i++) {
    functional_source.maps[i].anchor_ns += 123456;
    functional_source.maps[i].rate_q16 += 100;
  }
  int64_t offset = functional_source.utc_offset_ns;
  struct r2_source_clock retained = functional_source;
  r2_service_clock(&c);
  assert(promoted_valid &&
         !memcmp(&retained, &functional_source, sizeof retained));
  tick_to(fake_hf + 100000);
  c = raw_clock();
  r2_service_clock(&c);
  assert(promoted_valid &&
         !memcmp(&retained, &functional_source, sizeof retained));
  uint64_t old_end = retained
                         .maps[(retained.head + R2_SOURCE_MAP_HISTORY - 1) %
                               R2_SOURCE_MAP_HISTORY]
                         .until;
  /* Only a genuinely new PPS advances the delivered coordinate/horizon. */
  tick_to(67000001);
  c = raw_clock();
  const struct r2_source_map *raw =
      &fake_raw.maps[(fake_raw.head + R2_SOURCE_MAP_HISTORY - 1) %
                     R2_SOURCE_MAP_HISTORY];
  struct r2_pps_precision pps = {.epoch = 4,
                                 .last = 67000000 * UINT64_C(16),
                                 .rate_q16 = raw->rate_q16,
                                 .seen = true,
                                 .rate_valid = true};
  struct r2_capture now = {fake_hf * 16, 4, R2_CAPTURE_NOW};
  assert(r2_source_clock_publish(&fake_raw, &pps, now));
  uint64_t predicted, generation;
  assert(r2_source_clock_read(&retained, now, &predicted, &generation));
  r2_service_clock(&c);
  assert(promoted_valid && promoted_pps == pps.last &&
         functional_source.utc_offset_ns == offset);
  uint64_t delivered;
  assert(
      r2_source_clock_read(&functional_source, now, &delivered, &generation) &&
      delivered == predicted);
  assert(functional_source
             .maps[(functional_source.head + R2_SOURCE_MAP_HISTORY - 1) %
                   R2_SOURCE_MAP_HISTORY]
             .until > old_end);
  retained = functional_source;
  r2_service_clock(&c);
  assert(!memcmp(&retained, &functional_source, sizeof retained));
  /* Atomic source_read sees the same promoted map plus raw eligibility
   * metadata. */
  struct gm7_source source;
  struct r2_source_clock copy;
  struct r2_clock copied;
  struct gm7_bridge b = {.hf = fake_hf,
                         .epoch = 4,
                         .kernel_lo = fake_kernel,
                         .kernel_hi = fake_kernel};
  assert(source_read(&source, &copy, &copied, &b) &&
         !memcmp(&copy, &functional_source, sizeof copy));
  fake_raw.utc_association++;
  r2_service_clock(&c);
  assert(!promoted_valid && !functional_source.count);
  healthy_input();
  fake_raw = functional_source;
  promoted_valid = true;
  promoted_pps = fake_raw.last_edge;
  fake_raw.epoch++;
  r2_service_clock(&c);
  assert(!promoted_valid && !functional_source.count);
  healthy_input();
  fake_raw = functional_source;
  promoted_valid = true;
  promoted_pps = fake_raw.last_edge;
  tick_to(fake_hf + 1600000);
  c = raw_clock();
  r2_service_clock(&c);
  assert(!promoted_valid && !functional_source.count);
  puts("PASS one persistent functional map: nonzero promoted phase/rate "
       "survives duplicate raw PPS, new PPS preserves delivered continuity, "
       "original horizon only advances once, atomic metadata/map read, "
       "association/epoch/stale-raw fences");
}
static void takeover_fixture_at(uint64_t at) {
  setup(false);
  tick_to(at);
  healthy_input();
  owner.scope = (struct gm7_scope){.birth = UINT64_C(1792800000000000),
                                   .founder = 501,
                                   .term = 1,
                                   .active = 501,
                                   .boot = 777,
                                   .source_gen = 3,
                                   .capture_gen = 4,
                                   .train = 5,
                                   .timing_epoch = 22};
  owner.selected = (struct gm7_control){.sender = 501,
                                        .boot = 777,
                                        .sequence = 10,
                                        .birth_us = owner.scope.birth,
                                        .founder = 501,
                                        .term = 1,
                                        .active = 501,
                                        .active_boot = 777,
                                        .source_gen = 3,
                                        .capture_gen = 4,
                                        .train_gen = 5,
                                        .hot = identity,
                                        .hot_boot = boot,
                                        .sent_mono_us = 66284900,.timing_epoch=22};
  owner.selected_contact = 66284900;
  owner.selected_until = 68784900;
  owner.initial_due = owner.takeover_due = owner.serving = false;
  owner.armed = true;
  owner.armed_since = 66000000;
  owner.source_previous_until = fake_hf + 1500000;
  owner.source = source_at(fake_hf);
  fake_timing =
      (struct r2_gm7_timing){.scope = {.root = 1,
                                       .root_identity = 501,
                                       .boot = 777,
                                       .source_epoch = 22,
                                       .publication_epoch = 5,
                                       .follower = true},
                             .counter_epoch = 4,
                             .capture_gen = 4,
                             .shadow_gen = 9,
                             .phase_enclosure_us = 500,
                             .retained = true,
                             .map = {.valid = true,
                                     .delivery = fake_hf * 16,
                                     .anchor = (int64_t)fake_hf * 1000},
                             .utc_offset_ns = INT64_C(1792800000000000000)};
  fake_promote_ready = true;
  promoted_valid = takeover_preparing = false;
  takeover_bound = 0;
  primary_frame = diag_frame = status_cycle = UINT64_MAX;
  fake_rx_count = 0;
}
static void takeover_fixture(void) { takeover_fixture_at(68783100); }
static void yield_fixture(void) {
  setup(true);
  tick_to(96033000);
  healthy_input();
  fake_timing = (struct r2_gm7_timing){0};
  owner.source = source_at(fake_hf);
  owner.source_previous_until = fake_hf + 1500000;
  struct gm7_peer *p = &owner.peer[0];
  p->used = true;
  p->identity = 49;
  p->boot = 65;
  p->kind = GM7_STATUS;
  p->flags = GM7_ELIGIBLE | GM7_CAPABLE | GM7_SHADOW | GM7_HOT;
  p->received=(struct gm7_peer_control){.control=(struct gm7_control){0},.control_until=101000000};
  p->received.control = (struct gm7_control){.sender = 49,
                                            .boot = 65,
                                            .sequence = 1,
                                            .birth_us = owner.scope.birth,
                                            .founder = owner.scope.founder,
                                            .term = owner.scope.term,
                                            .active = identity,
                                            .active_boot = boot,
                                            .train_gen = owner.scope.train,
                                            .timing_epoch=owner.scope.timing_epoch,
                                            .source_gen = 3,
                                            .capture_gen = 4,
                                            .shadow_gen = 9,
                                            .hot = 49,
                                            .hot_boot = 65,
                                            .priority1 = 128,
                                            .clock_class = 1,
                                            .accuracy = 255,
                                            .priority2 = 128,
                                            .variance = 65535};
  owner.nominee = owner.quality_better_id = 49;
  owner.nominee_boot = owner.quality_better_boot = 65;
  owner.quality_better_since = 66000000;
  fake_submit_refusal = false;
  takeover_bound = 0;
  primary_frame = diag_frame = status_cycle = UINT64_MAX;
}
static void yield_copies(void) {
  for (unsigned reject = 0; reject < 2; reject++) {
    yield_fixture();
    uint8_t original[GM7_MAX_AD_BYTES];
    size_t length = 0;
    uint64_t first = 0, deadline = 0;
    for (unsigned i = 0; i < 3; i++) {
      tick_to(96033000 + i * UINT64_C(250000));
      fake_submit_refusal = reject && i == 0;
      cycle(&service_work);
      if (!i) {
        first = owner.event.first;
        deadline = owner.event.deadline;
        assert(first >= 96038000 && first <= 96038002 &&
               deadline ==
                   owner.event.packet.body.control.sent_mono_us + 750000);
        length = gm7_wire_size(GM7_ANNOUNCE) + 4;
        assert(!gm7_ad_encode(original, sizeof original, &owner.event.packet,
                              &group));
      }
      assert(owner.event.valid && !owner.event.negative &&
             owner.event.first == first && owner.event.deadline == deadline);
      assert(owner.event.packet.flags & GM7_YIELD);
      assert(!fake_native_live[GM7_SECONDARY]);
      if (reject && i == 0) {
        assert(!fake_native_live[GM7_PRIMARY] && owner.event.sent == 1 &&
               event_evidence.queued == 1 && event_evidence.failed == 1);
        tick_to(fake_hf + 1000);
        cycle(&service_work);
        assert(event_evidence.queued == 1 && event_evidence.failed == 1 &&
               owner.event.first == first);
      } else {
        assert(fake_native_live[GM7_PRIMARY]);
        struct gm7_job *j = &scheduler.job[GM7_PRIMARY];
        assert(j->window.nominal == first + i * GM7_FRAME_US &&
               j->length == length && !memcmp(j->ad, original, length));
        finish(GM7_PRIMARY, true, true);
      }
    }
    fake_submit_refusal = false;
    struct gm7_bridge b;
    assert(bridge(&b));
    publish(b);
    assert(diagnostic.event_queued == 3 &&
           diagnostic.event_completed == 3 - reject &&
           diagnostic.event_failed == reject && !diagnostic.event_unsent);
    tick_to(deadline - 1);
    cycle(&service_work);
    assert(owner.serving && owner.yield_deadline == deadline);
    tick_to(deadline);
    cycle(&service_work);
    assert(!owner.serving && !fake_join.serving);
  }
  yield_fixture();
  cycle(&service_work);
  finish(GM7_PRIMARY, true, true);
  input.view.fix_active = false;
  tick_to(fake_hf + 500);
  cycle(&service_work);
  assert(!owner.serving && owner.event.valid && owner.event.negative);
  uint64_t negative_sequence = owner.event.packet.body.control.sequence,
           deadline = owner.event.deadline;
  uint8_t original[GM7_MAX_AD_BYTES];
  assert(
      !gm7_ad_encode(original, sizeof original, &owner.event.packet, &group));
  for (unsigned i = 0; i < 3; i++) {
    tick_to(96283000 + i * UINT64_C(250000));
    cycle(&service_work);
    assert(fake_native_live[GM7_PRIMARY] && !fake_native_live[GM7_SECONDARY]);
    assert(owner.event.packet.body.control.sequence == negative_sequence &&
           owner.event.deadline == deadline);
    assert(!memcmp(scheduler.job[GM7_PRIMARY].ad, original,
                   GM7_CONTROL_BYTES + 4));
    finish(GM7_PRIMARY, true, true);
  }
  assert(event_evidence.completed == 3 && !event_evidence.failed);
  yield_fixture();
  owner.peer[0].received.control_until = fake_hf;
  owner.yield_ready = true;
  cycle(&service_work);
  assert(!owner.yield_ready && !owner.event.valid);
  fake_submit_refusal = false;
  puts("PASS actual service quality YIELD3 immutable primary copies/no "
       "secondary, queue refusal consumes original opportunity without "
       "reanchor/retry, truthful completed/failed/unsent counters, "
       "original750ms stop, superseding WITHDRAW3 copies and expired nominee "
       "readiness fence");
}
static void administrative_yield_fixture(void) {
  yield_fixture();
  /* Equal-quality T096s do not trigger the strict better-quality dwell. */
  owner.peer[0].received.control.clock_class = owner.source.quality.clock_class;
  owner.quality_better_since = 0;
  owner.yield_ready = false;
  struct gm7_bridge b;
  assert(bridge(&b));
  timing(b,&owner.source,&functional_source,&input.view);
  publish(b);
}
static void administrative_yield(void) {
  administrative_yield_fixture();
  assert(r2_gm7_request_yield() == -EINPROGRESS);
  uint32_t requested = yield_request.requested_ms;
  tick_to(fake_hf + 1000);
  assert(r2_gm7_request_yield() == -EALREADY &&
         yield_request.requested_ms == requested);
  cycle(&service_work);
  assert(!yield_request.pending && !yield_request.result &&
         owner.event.valid && !owner.event.negative &&
         (owner.event.packet.flags & GM7_YIELD));
  uint64_t deadline = owner.event.deadline;
  assert(deadline == owner.event.packet.body.control.sent_mono_us + 750000);
  uint8_t original[GM7_MAX_AD_BYTES];
  assert(!gm7_ad_encode(original, sizeof original, &owner.event.packet, &group));
  for (unsigned i=0;i<3;i++) {
    if (i) { tick_to(96033000 + i*UINT64_C(250000)); cycle(&service_work); }
    assert(fake_native_live[GM7_PRIMARY] &&
           !memcmp(scheduler.job[GM7_PRIMARY].ad, original, GM7_CONTROL_BYTES+4) &&
           owner.event.deadline == deadline);
    finish(GM7_PRIMARY,true,true);
  }
  tick_to(deadline); cycle(&service_work);
  assert(!owner.serving && event_evidence.completed == 3);
  /* Admission never changes the announced nominee or supplies HOT readiness. */
  for (unsigned fault=0;fault<9;fault++) {
    administrative_yield_fixture();
    assert(r2_gm7_request_yield() == -EINPROGRESS);
    if (fault==0) owner.peer[0].flags &= ~GM7_HOT;
    if (fault==1) owner.peer[0].received.control_until = fake_hf;
    if (fault==2) {
      owner.peer[0].identity = 50;
      owner.peer[0].received.control.sender = owner.nominee = 50;
    }
    if (fault==3) {
      owner.nominee_boot++;
      owner.peer[0].boot = owner.nominee_boot;
      owner.peer[0].received.control.boot = owner.nominee_boot;
    }
    if (fault==4) owner.event_generation++;
    if (fault==5) { tick_to(fake_hf+300000); healthy_input(); }
    if (fault==6) input.view.fix_active = false;
    if (fault==7) r2_service_stop();
    if (fault==8) owner.peer[0].received.control.timing_epoch++;
    cycle(&service_work);
    if (yield_request.pending || !yield_request.result || owner.yield_ready ||
        (owner.event.valid && !owner.event.negative))
      fprintf(stderr,"admin fault%u pending%u result%d ready%u event%u negative%u\n",
          fault,yield_request.pending,yield_request.result,owner.yield_ready,
          owner.event.valid,owner.event.negative);
    assert(!yield_request.pending && yield_request.result && !owner.yield_ready &&
           (!owner.event.valid || owner.event.negative));
  }
  administrative_yield_fixture();
  owner.peer[0].flags &= ~GM7_HOT;
  assert(gm7_owner_request_yield(&owner,fake_hf) == -ENODATA);
  owner.source.until_us = fake_hf;
  assert(gm7_owner_request_yield(&owner,fake_hf) == -EACCES);
  status.serving = false;
  assert(r2_gm7_request_yield() == -EACCES && !yield_request.pending);
  puts("PASS bounded owner-thread administrative YIELD reuses ordinary3-copy "
       "event; equal quality accepted only with current confirmed HOT, repeat "
       "does not renew300ms, source/nominee boot/scope/delay/stop fences cancel "
       "one-shot without latent event; result0 means construction acceptance");
}
static void negative_phase_cases(void) {
  setup(true);
  struct gm7_bridge b;
  assert(bridge(&b));
  uint64_t base;
  uint32_t width;
  assert(negative_phase(b, &base, &width) && width >= last_enclosure);
  uint64_t sampled = b.hf;
  tick_to(sampled + 895000);
  assert(bridge(&b));
  owner.now = b.hf; /* Other healthy processing; no phase observation. */
  assert(negative_phase(b, &base, &width) && width == last_enclosure + 200);
  tick_to(sampled + 896000);
  assert(bridge(&b));
  assert(!negative_phase(b, &base, &width) && phase_known);
  tick_to(sampled + 1000000);
  assert(bridge(&b));
  assert(!negative_phase(b, &base, &width) && !phase_known);
  tick_to(sampled + (UINT64_C(1) << 32) * 1000);
  owner.now = fake_hf;
  assert(bridge(&b));
  assert(!negative_phase(b, &base, &width)); /* Expiry never revives. */
  setup(true);
  assert(bridge(&b));
  b.epoch++;
  assert(!negative_phase(b, &base, &width) && !phase_known);
  setup(true);
  tick_to(fake_hf + (UINT64_C(1) << 32) * 1000);
  assert(bridge(&b));
  assert(!negative_phase(b, &base, &width) && !phase_known);
  setup(true);
  assert(bridge(&b));
  owner.now = b.hf + 1;
  assert(!negative_phase(b, &base, &width) && !phase_known);

  /* A receive/drain pass crosses lease expiry but ends inside a legal
   * primary construction window. Entry-only age checking would queue here. */
  yield_fixture();
  tick_to(97023000);
  healthy_input();
  owner.yield_ready = false; /* This case observes phase, not a YIELD action. */
  owner.nominee = owner.nominee_boot = owner.quality_better_id = 0;
  owner.peer[0].received.control_until = 0;
  cycle(&service_work);
  assert(phase_known && !owner.event.valid);
  input.view.fix_active = false;
  tick_to(98010000);
  fake_poll_advance = 23000;
  cycle(&service_work);
  assert(fake_hf == 98033000 && owner.event.valid && owner.event.negative &&
         !phase_known && !fake_native_live[GM7_PRIMARY] &&
         !fake_native_live[GM7_SECONDARY] && !fake_native_live[GM7_STANDBY] &&
         !owner.serving && !fake_join.serving);

  /* Original positive map/PPS expires just after a healthy phase sample. */
  yield_fixture();
  tick_to(97498000);
  healthy_input();
  owner.nominee = owner.nominee_boot = owner.quality_better_id = 0;
  owner.peer[0].received.control_until = 0;
  cycle(&service_work);
  tick_to(98498000);
  cycle(&service_work);
  assert(owner.serving && owner.source.utc_available && phase_known);
  tick_to(98500001);
  cycle(&service_work);
  assert(!owner.serving && !owner.eligible && !owner.source.physical_ready &&
         !owner.source.until_us && owner.event.valid && owner.event.negative);
  /* Frozen v2 explicitly permits unknown UTC on a monotonic WITHDRAW. */
  assert(!owner.event.packet.body.control.sent_utc_us);
  assert(!fake_join.serving);
  uint8_t original[GM7_MAX_AD_BYTES];
  assert(
      !gm7_ad_encode(original, sizeof original, &owner.event.packet, &group));
  for (unsigned i = 0; i < 3; i++) {
    tick_to(98533000 + (uint64_t)i * GM7_FRAME_US);
    cycle(&service_work);
    assert(fake_native_live[GM7_PRIMARY] && !fake_native_live[GM7_STANDBY] &&
           !fake_native_live[GM7_SECONDARY] && !owner.serving &&
           !fake_join.serving && !owner.yield_ready);
    assert(!memcmp(scheduler.job[GM7_PRIMARY].ad, original,
                   GM7_CONTROL_BYTES + 4));
    finish(GM7_PRIMARY, true, true);
  }
  assert(event_evidence.completed == 3 && !event_evidence.failed);

  /* Missing a middle original opportunity never slides later copies. */
  yield_fixture();
  cycle(&service_work);
  uint64_t first = owner.event.first, deadline = owner.event.deadline;
  finish(GM7_PRIMARY, true, true);
  tick_to(96533000);
  cycle(&service_work);
  assert(owner.event.first == first && owner.event.deadline == deadline &&
         scheduler.job[GM7_PRIMARY].window.nominal == first + 500000);
  finish(GM7_PRIMARY, true, true);
  assert(bridge(&b));
  publish(b);
  assert(diagnostic.event_queued == 2 && diagnostic.event_completed == 2 &&
         !diagnostic.event_failed && diagnostic.event_unsent == 1);

  /* UTC loss is explicitly represented as unknown; no expired UTC map or
   * previous observed UTC becomes new timestamp/source authority. */
  setup(true);
  owner.source.utc_available = false;
  owner.source.until_us = 0;
  owner.last_utc_at = fake_hf - 1000001;
  gm7_owner_withdraw(&owner, GM7_R_SOURCE, fake_hf);
  assert(owner.event.valid && owner.event.negative && !owner.serving &&
         !owner.event.packet.body.control.sent_utc_us);
  puts("PASS source/PPS/map expiry WITHDRAW3 immutable primary copies with "
       "no positive/JOIN/YIELD authority; negative-only phase1s boundary, "
       "outward200ppm age, epoch/regression/long-gap/wrap retirement and "
       "missed original opportunity reports unsent without reanchor");
}
static void prospective_blackout_at(uint64_t at, bool revoked) {
  takeover_fixture_at(at);
  cycle(&service_work);
  /* Last legal sample34.9ms; 33.1ms poll must reserve before35.1ms poll
   * passes the−3ms construction edge. No due/output authority exists yet. */
  assert(fake_hf == at && !owner.takeover_due && !owner.serving &&
         owner.reserved && fake_native_live[GM7_PRIMARY]);
  assert(owner.reserved_nominal == 68788000 &&
         owner.reserved_nominal >= owner.selected_contact + GM7_BLACKOUT_US &&
         !fake_join.serving);
  assert(promoted_valid &&
         takeover_bound == owner.selected_contact + GM7_BLACKOUT_BOUND_US);
  struct gm7_owner probe = owner;
  probe.reserved = false;
  uint64_t cause = probe.selected_contact + GM7_BLACKOUT_US;
  assert(!gm7_owner_takeover_at(&probe, cause - 1, cause + 3999, fake_hf));
  assert(gm7_owner_takeover_at(&probe, cause, cause + 4000, fake_hf));
  uint64_t permission = probe.source.until_us;
  probe.source.until_us = cause + 4000;
  assert(!gm7_owner_takeover_at(&probe, cause, cause + 4000, fake_hf));
  probe.source.until_us = permission;
  probe.selected.hot = 0;
  assert(!gm7_owner_takeover_at(&probe, cause, cause + 4000, fake_hf));
  probe.selected.hot = identity;
  probe.selected_contact = fake_hf - 1000000;
  probe.selected.yield_utc_us = probe.source.utc_us + 3000;
  probe.remote_yield_sequence = probe.selected.sequence;
  probe.remote_yield_after = fake_hf + 3000;
  assert(
      !gm7_owner_takeover_at(&probe, fake_hf + 2999, fake_hf + 6999, fake_hf));
  assert(
      gm7_owner_takeover_at(&probe, fake_hf + 3000, fake_hf + 7000, fake_hf));
  probe.selected.yield_utc_us = probe.remote_yield_sequence =
      probe.remote_yield_after = 0;
  probe.selected_contact = owner.selected_contact;
  struct gm7_peer *rival = &probe.peer[0];
  rival->used = true;
  rival->identity = 900;
  rival->boot = 999;
  rival->kind = GM7_ANNOUNCE;
  rival->received=(struct gm7_peer_control){.control=owner.selected,.control_until=fake_hf + 2000000};
  rival->received.control.birth_us=owner.scope.birth-1000;
  rival->received.control.active = rival->received.control.sender = 900;
  rival->received.control.active_boot = 999;
  assert(gm7_owner_reserve(&probe, cause, cause + 4000, fake_hf) == -EACCES &&
         !probe.reserved && !probe.serving);
  assert(!owner.takeover_due && !owner.serving);
  tick_to(68784800);
  cycle(&service_work);
  assert(owner.reserved && !owner.serving && !owner.takeover_due);
  tick_to(68785100);
  cycle(&service_work);
  assert(owner.reserved && !owner.serving);
  finish(GM7_PRIMARY, true, true);
  assert(owner.serving && owner.active_since == fake_hf &&
         owner.first_announce_start >= 68788000);
  tick_to(68812500);
  fake_timing.first_tx_observed_complete = true;
  fake_timing.first_tx_lo = (fake_hf - 500) * 16;
  fake_timing.first_tx_hi = (fake_hf - 498) * 16;
  fake_timing.first_tx_derived_end_hi = fake_hf * 16;
  cycle(&service_work);
  assert(owner.serving && !takeover_bound);
  takeover_fixture_at(at);
  cycle(&service_work);
  assert(owner.reserved);
  /* An accepted same-scope fresh ANN at the staggered next poll cancels the
   * conditional blackout, even when its nominee remains this HOT. */
  struct gm7_peer *peer = &owner.peer[0];
  peer->used = true;
  peer->identity = 501;
  peer->boot = 777;
  peer->kind = GM7_ANNOUNCE;
  peer->received=(struct gm7_peer_control){.control=owner.selected,.control_until=100000000};
  tick_to(at < 68780000 ? at + 5000 : 68785100);
  struct gm7_packet fresh = {.group = group.id,
                             .key_epoch = group.epoch,
                             .kind = GM7_ANNOUNCE,
                             .flags = GM7_ELIGIBLE,
                             .body.control = owner.selected};
  fresh.body.control.sequence++;
  fresh.body.control.sent_mono_us = fake_hf - 10;
  fresh.body.control.sent_utc_us = UINT64_C(1792800000000000) + fake_hf - 10;
  fresh.body.control.max_age_us = 2750000;
  fresh.body.control.source_valid_us = 1500000;
  fresh.body.control.clock_class = 2;
  fresh.body.control.accuracy = 255;
  fresh.body.control.priority1 = fresh.body.control.priority2 = 128;
  fresh.body.control.variance = 65535;
  if (revoked)
    fresh.body.control.hot = fresh.body.control.hot_boot = 0;
  fake_rx_count = 1;
  fake_rx[0] = (struct r2_gm7_rx){.raw = (uint32_t)((fake_hf - 10) * 16),
                                  .epoch = 4,
                                  .length = GM7_CONTROL_BYTES + 4};
  assert(!gm7_ad_encode(fake_rx[0].ad, sizeof fake_rx[0].ad, &fresh, &group));
  cycle(&service_work);

  assert(!owner.reserved && !owner.serving && !fake_native_live[GM7_PRIMARY] &&
         owner.selected_contact > 66284900);
  takeover_fixture_at(at);
  cycle(&service_work);
  assert(owner.reserved);
  input.view.fix_active = false;
  tick_to(at < 68780000 ? at + 5000 : 68785100);
  cycle(&service_work);
  assert(!owner.reserved && !owner.serving && !fake_native_live[GM7_PRIMARY]);
  takeover_fixture_at(at);
  cycle(&service_work);
  finish(GM7_PRIMARY, true, true);
  tick_to(takeover_bound + 1);
  healthy_input();
  fake_timing.first_tx_observed_complete = true;
  fake_timing.first_tx_derived_end_hi = fake_hf * 16;
  cycle(&service_work);
  assert(!owner.serving && owner.reason == GM7_R_MISSED_BOUND);
  fake_promote_ready = false;
  puts("PASS actual service34.9ms last sample /33.1→35.1ms staggered polls "
       "reserve conditional blackout without early due/output; completed "
       "ANN+first G1 meet original3s; fresh contact/source fault cancel and "
       "late G1 fails closed");
}
static void source_horizon_preparation(void) {
  for(unsigned revoke=0;revoke<3;revoke++){
    takeover_fixture_at(68435000);
    unsigned stops=fake_stop_calls,activates=fake_activate_calls;
    cycle(&service_work);
    assert(owner.armed&&takeover_preparing&&fake_stop_calls>stops&&!owner.reserved&&!owner.serving);
    struct gm7_peer *p=&owner.peer[0];p->used=true;p->identity=501;p->boot=777;
    p->kind=GM7_ANNOUNCE;p->flags=GM7_ELIGIBLE;
    p->received=(struct gm7_peer_control){.control=owner.selected,
      .control_until=owner.selected_until};
    p->received.control.max_age_us=(uint32_t)(owner.selected_until-owner.selected_contact);
    stops=fake_stop_calls;
    tick_to(fake_hf+1000);cycle(&service_work);
    assert(owner.armed&&takeover_preparing&&fake_stop_calls==stops); /* Closed drain is not restarted. */
    fake_pt_closed=false;tick_to(fake_hf+1000);cycle(&service_work);
    assert(fake_stop_calls==stops+1&&takeover_preparing); /* Incomplete drain is still polled/stopped. */
    struct gm7_packet fresh={.group=group.id,.key_epoch=group.epoch,
      .kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE,.body.control=owner.selected};
    fresh.body.control.sequence=11;fresh.body.control.sent_mono_us=fake_hf;
    fresh.body.control.sent_utc_us=UINT64_C(1792800000000000)+fake_hf;
    fresh.body.control.max_age_us=2750000;fresh.body.control.source_valid_us=1500000;
    fresh.body.control.clock_class=2;
    if(revoke==1)fresh.body.control.hot=fresh.body.control.hot_boot=0;
    if(revoke==2)input.view.fix_active=false;
    fake_rx_count=1;fake_rx[0]=(struct r2_gm7_rx){.raw=(uint32_t)(fake_hf*16),.epoch=4,.length=GM7_CONTROL_BYTES+4};
    assert(!gm7_ad_encode(fake_rx[0].ad,sizeof fake_rx[0].ad,&fresh,&group));
    tick_to(fake_hf+1000);cycle(&service_work);
    assert(!takeover_preparing&&!owner.reserved&&!owner.serving&&fake_activate_calls>activates);
    if(revoke)assert(!owner.armed);else assert(owner.armed);
  }
  puts("PASS contact-silence350ms preparation preserves retained map without output/reservation; advanced ANN invokes PT resume, nominee/source revocation cancels safely");
}
static void prospective_blackout(void) {
  prospective_blackout_at(68783100, false);
  prospective_blackout_at(68764000, false);
  prospective_blackout_at(68764000, true);
}
static void ordinary_progress(uint64_t self,uint64_t incarnation) {
 setup(false);identity=owner.identity=self;boot=owner.boot=incarnation;
 status_cycle=diag_frame=primary_frame=UINT64_MAX;
 unsigned statuses=0;uint64_t previous_pps=0;
 for(uint64_t t=66000000;t<86000000;t+=1000){
  tick_to(t);if(t/1000000!=previous_pps){healthy_input();previous_pps=t/1000000;}
  for(unsigned i=0;i<GM7_JOB_SLOTS;i++)if(fake_native_live[i]&&fake_hf>=scheduler.job[i].window.nominal+1500){statuses+=scheduler.job[i].kind==GM7_STATUS;finish(i,true,false);}
  cycle(&service_work);
 }
 assert(statuses>=9);
}

static void terminal_faults(void) {
  setup(true);
  healthy_input();
  struct gm7_source actual;struct r2_source_clock local_map;struct r2_clock local_clock;struct gm7_bridge b;
  assert(bridge(&b)&&source_read(&actual,&local_map,&local_clock,&b));
  owner.source=actual;owner.last_utc=actual.utc_us;owner.last_utc_at=fake_hf;
  fake_overflow=1;
  cycle(&service_work);
  if(resource_fault||!owner.serving||owner.armed||status.drops!=1)fprintf(stderr,"overflow state fault%u serving%u armed%u drops%u reason%u source%u\n",resource_fault,owner.serving,owner.armed,status.drops,owner.reason,owner.eligible);
  assert(!resource_fault&&owner.serving&&!owner.armed&&status.drops==1);
  tick_to(fake_hf+1000);healthy_input();cycle(&service_work);
  assert(!resource_fault&&owner.eligible&&owner.serving);
  setup(true);
  fake_hf_valid = false;
  cycle(&service_work);
  assert(status.error == -ESTALE && !owner.serving && !owner.armed &&
         status.closed && r2_service_close_result() == -ESTALE);
  setup(true);
  healthy_input();
  fake_ble_error = -ETIMEDOUT;
  cycle(&service_work);
  assert(status.error == -ETIMEDOUT && atomic_get(&stopping) &&
         !status.serving);
  fake_ble_error = 0;
  setup(true);
  healthy_input();
  fake_pt_error = -EIO;
  cycle(&service_work);
  assert(status.error == -EIO && atomic_get(&stopping));
  fake_pt_error = 0;
  puts("PASS management observation overflow counts/drops without retiring GM; "
       "lost HF domain fences and drains without "
       "fabricated copies, transport/PT faults preserve terminal stop guards");
}
static void previews(const char *folder) {
  static const uint16_t palette[] = {
      0x0000, 0x0820, 0x1042, 0xffff, 0xa534, 0x37ff, 0x4d1f, 0x4fea, 0xfd20,
      0xf9c7, 0x3987, 0x0821, 0xbdf7, 0x8c71, 0x2104, 0x3186, 0x6b4d};
  uint8_t fb[R2_UI_FRAME_BYTES];
  struct r2_ui_model m = {.gm7_owner = true,
                          .gm7_peer_valid = true,
                          .gm7_peer_id = 0x1234,
                          .gm7_peer_known = 7,
                          .gm7_peer_role = GM7_W_HOT,
                          .gm7_peer_hot_ms = UINT32_MAX - 1,
                          .gm7_peer_active_ms = UINT32_MAX - 1,
                          .gm7_peer_pps_ms = UINT32_MAX - 1};
  struct r2_ui_status s = {.page = 5};
  for (unsigned i = 0; i < 4; i++) {
    m.gm7_peer_current = i == 0;
    if (i == 2)
      m.gm7_peer_known = 0;
    if (i == 3)
      m.gm7_peer_valid = false;
    r2_ui_render(fb, &m, &s, 0);
    char path[512];
    snprintf(path, sizeof path, "%s/peer-%u.ppm", folder, i);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n80 160\n255\n");
    for (unsigned p = 0; p < R2_UI_PIXELS; p++) {
      assert(fb[p] < sizeof palette / sizeof palette[0]);
      uint16_t c = palette[fb[p]];
      uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31),
                        (uint8_t)(((c >> 5) & 63) * 255 / 63),
                        (uint8_t)((c & 31) * 255 / 31)};
      assert(fwrite(rgb, 1, 3, f) == 3);
    }
    assert(!fclose(f));
  }
}
static void heard_only_peer(void) {
  setup(false);healthy_input();
  management_rx=management_mac=management_accepted=0;fake_rx_count=0;
  owner.source_since=fake_hf;owner.eligible=owner.initial_due=false;
  struct gm7_packet locator;
  assert(!gm7_owner_control(&owner,&locator,fake_hf));
  locator.body.control.sender=401;locator.body.control.boot=51;
  locator.body.control.sent_mono_us=fake_hf-100;
  struct r2_gm7_rx *rx=&fake_rx[fake_rx_count++];
  memset(rx,0,sizeof *rx);rx->raw=(uint32_t)((fake_hf-10)*16);rx->epoch=4;
  rx->length=GM7_CONTROL_BYTES+4;
  assert(!gm7_ad_encode(rx->ad,sizeof rx->ad,&locator,&group));
  cycle(&service_work);
  assert(management_mac==1 && management_accepted==1 && owner.peer[0].heard);
  struct r2_ui_model m={0};r2_service_ui(&m);
  assert(m.gm7_peer_valid && m.gm7_peer_id==401 && m.gm7_peer_role==255 &&
      !m.gm7_peer_current && m.gm7_peer_fresh && !m.gm7_peer_eligible_claim &&
      !m.gm7_peer_known && m.gm7_peer_hot_ms==UINT32_MAX);
  char json[2048];
  assert(r2_gm7_diagnostic_format(json,sizeof json,1,"source",boot,true)>0);
  assert(strstr(json,"GM7v3+S2+C1") && strstr(json,"\"heard\":true") &&
      strstr(json,"\"control_fresh\":true") &&
      strstr(json,"\"independent_eligible_claim\":false") &&
      strstr(json,"\"authority\":false"));
  uint64_t expiry=peer_display[0].report.until;
  tick_to(fake_hf+4500000);cycle(&service_work);
  r2_service_ui(&m);
  assert(!m.gm7_peer_valid && fake_kernel>=expiry && !owner.armed && !owner.serving);
  puts("PASS authenticated acquiring STATUS remains visible as heard-only "
       "unqualified/freshness-unproven with unknown timers, original4.5s "
       "heard expiry and no timing/HOT/election authority");
}
static void continuing_input(void){
  struct r2_source_clock retained=functional_source;
  healthy_input();fake_raw=functional_source;
  struct r2_clock c=raw_clock();
  functional_source=retained;r2_service_clock(&c);
}
static void durable_control_cases(void){
  takeover_fixture();cycle(&service_work);assert(owner.reserved);
  uint64_t at=fake_hf;
  fake_direct_contact[0]=501;fake_direct_contact[1]=777;
  fake_direct_contact[2]=(at-10)*16;fake_direct_contact[3]=4;
  tick_to(at+1000);cycle(&service_work);
  assert(!owner.reserved&&!owner.serving&&!takeover_preparing&&!takeover_bound);
  /* Even if timing qualification is lost, raw current-GM progression cannot
   * open the cold initial-election path. Local PPS map remains available. */
  memset(&fake_timing,0,sizeof fake_timing);
  int64_t local_offset=functional_source.utc_offset_ns;
  for(unsigned i=0;i<35;i++){
    tick_to(fake_hf+200000);continuing_input();
    fake_direct_contact[2]=(fake_hf-10)*16;
    cycle(&service_work);
    assert(!owner.serving&&!owner.initial_due&&!owner.reserved&&functional_source.count&&
        functional_source.utc_offset_ns==local_offset);
  }
  uint64_t last_contact=owner.selected_contact;
  bool fallback=false;
  for(unsigned i=0;i<180&&!fallback;i++){
    tick_to(fake_hf+200000);continuing_input();cycle(&service_work);
    fallback=owner.reserved||owner.serving;
    if(fake_hf-last_contact<GM7_QUIET_US)
      assert(!fallback&&!fake_precision_enabled);
  }
  assert(fallback&&fake_hf-last_contact>=GM7_QUIET_US&&functional_source.count&&
      functional_source.utc_offset_ns==local_offset);
  assert(owner.reserved&&!owner.armed&&!owner.serving);
  struct gm7_job cold=scheduler.job[GM7_PRIMARY];
  fake_native_live[GM7_PRIMARY]=false;
  fake_result[fake_result_count++]=(struct r2_gm7_tx){.ticket=cold.ticket,
      .generation=cold.generation,.buffer_version=cold.buffer_version,.epoch=4,
      .primary_address=(uint32_t)((cold.window.nominal+42)*16),
      .aux_address=(uint32_t)((cold.window.nominal+551)*16),
      .aux_end=(uint32_t)((cold.window.nominal+1400)*16),.slot=GM7_PRIMARY,.complete=true,.exposed=true};
  tick_to(cold.window.latest_end+60000);continuing_input();cycle(&service_work);
  assert(owner.serving&&!owner.reserved&&fake_precision_enabled&&functional_source.count&&
      functional_source.utc_offset_ns==local_offset);
  /* No remote fit and no local fix/PPS: ordinary unready capability is still
   * visible from a best-effort native window, without clock authority. */
  setup(false);healthy_input();input.view.fix_active=input.view.pps_present=false;
  cycle(&service_work);
  assert(!owner.eligible&&!owner.serving&&fake_native_live[GM7_STANDBY]&&
      scheduler.job[GM7_STANDBY].kind==GM7_STATUS&&!scheduler.job[GM7_STANDBY].positive);
  /* A real completed first ANN is processed before a delayed worker can
   * discard the consumed handoff just because its reserved_end has passed. */
  takeover_fixture();cycle(&service_work);assert(owner.reserved);
  struct gm7_job j=scheduler.job[GM7_PRIMARY];
  fake_native_live[GM7_PRIMARY]=false;
  fake_result[fake_result_count++]=(struct r2_gm7_tx){.ticket=j.ticket,
      .generation=j.generation,.buffer_version=j.buffer_version,.epoch=4,
      .primary_address=(uint32_t)((j.window.nominal+42)*16),
      .aux_address=(uint32_t)((j.window.nominal+551)*16),
      .aux_end=(uint32_t)((j.window.nominal+1400)*16),.slot=GM7_PRIMARY,.complete=true,.exposed=true};
  tick_to(j.window.latest_end+60000);healthy_input();cycle(&service_work);
  assert(owner.serving&&!owner.reserved&&owner.first_announce_end<=j.window.latest_end);
  puts("PASS raw contact cancels silence reservation/closure and inhibits cold election; PPS source stays live through completed non-HOT cold ANN/precision enable, no-fix capability remains visible, delayed completed ANN commits before reservation retirement");
}
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  yield_copies();
  administrative_yield();
  negative_phase_cases();
  prospective_blackout();
  source_horizon_preparation();
  functional_map_refresh();
  ordinary_progress(17, 33);
  ordinary_progress(17 + (UINT64_C(1) << 32), 33);
  ordinary_progress(17 + (UINT64_C(1) << 48), 65);
  terminal_faults();
  shutdown_copies(0);
  shutdown_copies(1);
  provisional_exposure();
  diagnostics_and_remap();
  heard_only_peer();
  durable_control_cases();
  if (argc > 1)
    previews(argv[1]);
  return 0;
}
