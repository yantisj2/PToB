/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
#include "gm7_bootstrap_fixture.h"

static struct gm7_owner server;
static struct gm7_packet response;
static uint64_t response_end;
static unsigned requests, positive, statuses, anchors, refusals;
static uint64_t prior_request;
static unsigned residues;
static bool advance_decode_clock;

static uint64_t advancing_owner_clock(void *unused) {
  (void)unused;
  ++fake_hf;
  ++fake_kernel;
  return fake_hf;
}

static void input_at(unsigned source_mode) {
  healthy_input();
  if (source_mode < 2)
    memset(&functional_source, 0, sizeof functional_source);
  if (!source_mode)
    input.view.fix_active = false;
  input.last_rmc_utc = INT64_C(1792800000000000) + (int64_t)fake_hf;
}
static void inject(const struct gm7_packet *p, uint64_t end) {
  assert(fake_rx_count < 3);
  struct r2_gm7_rx *r = &fake_rx[fake_rx_count++];
  memset(r, 0, sizeof *r);
  r->raw = (uint32_t)(end * 16); /* Actual complete AUX END, not ADDRESS. */
  r->epoch = 4;
  r->length = (uint16_t)(gm7_wire_size(p->kind) + 4);
  assert(!gm7_ad_encode(r->ad, sizeof r->ad, p, &group));
}
static void setup_bootstrap(bool retained, unsigned source_mode) {
  setup(retained);
  if (retained) {
    gm7_owner_withdraw(&owner, GM7_R_SOURCE, fake_hf);
    assert(owner.scope.birth && !owner.serving && owner.event.negative);
  } else {
    assert(!gm7_owner_init(&owner, &group, identity, boot, true, fake_hf));
    gm7_owner_reservation_guard(&owner, reservation_fence, NULL);
  }
  if (advance_decode_clock)
    gm7_owner_clock(&owner, advancing_owner_clock, NULL);
  memset(&fake_scope, 0, sizeof fake_scope);
  memset(&fake_timing, 0, sizeof fake_timing);
  memset(&fake_client, 0, sizeof fake_client);
  status_cycle = diag_frame = primary_frame = UINT64_MAX;
  takeover_preparing = promoted_valid = phase_known = false;
  phase_step_at = takeover_bound = response_end = prior_request = 0;
  fake_rx_count = 0;
  management_rx = management_mac = management_accepted = 0;
  requests = positive = statuses = anchors = refusals = residues = 0;
  assert(!gm7_owner_init(&server, &group, 401, 51, true, 0));
  struct gm7_preparation prep = {0};
  for (uint64_t t = 0; t <= fake_hf; t += 1000000) {
    struct gm7_source s = source_at(t);
    gm7_owner_update(&server, &s, &prep, t);
  }
  assert(!gm7_owner_activate(&server, fake_hf + 10, fake_hf + 4010,
                             fake_hf + 10));
  if (retained)
    assert(owner.scope.birth < server.scope.birth);
  input_at(source_mode);
}
static void complete_jobs(bool answer) {
  for (unsigned slot = 0; slot < GM7_JOB_SLOTS; slot++) {
    struct gm7_job *j = &scheduler.job[slot];
    if (!fake_native_live[slot] || fake_hf < j->window.nominal + 1500)
      continue;
    struct gm7_packet p;
    assert(!gm7_ad_decode(&p, j->ad, j->length, &group));
    if (p.kind == GM7_REQUEST) {
      uint64_t actual = j->window.nominal + 1;
      if (prior_request)
        assert(actual - prior_request >= 2000000);
      prior_request = actual;
      ++requests;
      assert(!j->positive);
      residues |= 1u << (actual / 250000 % 4);
      if (answer) {
        struct gm7_preparation prep = {0};
        struct gm7_source s = source_at(fake_hf);
        gm7_owner_update(&server, &s, &prep, fake_hf);
        assert(!gm7_owner_anchor(&server, &response, &p, actual, fake_hf));
        response_end = (actual / GM7_FRAME_US + 1) * GM7_FRAME_US +
                       GM7_SECONDARY_US + 1370;

      }
    }
    statuses += p.kind == GM7_STATUS;
    positive += j->positive;
    fake_native_live[slot] = false;
    uint64_t nominal = j->window.nominal;
    fake_result[fake_result_count++] = (struct r2_gm7_tx){
        .ticket = j->ticket, .generation = j->generation,
        .buffer_version = j->buffer_version, .epoch = 4,
        .primary_address = (uint32_t)((nominal + 42) * 16),
        .aux_address = (uint32_t)((nominal + 551) * 16),
        .aux_end = (uint32_t)((nominal + 1400) * 16), .slot = slot,
        .complete = true, .exposed = true};
  }
}
static void bootstrap(bool retained, unsigned source_mode, bool answer,
                       unsigned rejected, bool copy,
                       struct gm7_bootstrap_fixture *out) {
  setup_bootstrap(retained, source_mode);
  for (uint64_t t = 66000000; t < 78000000; t += 1000) {
    tick_to(t);
    if (t % 1000000 == 0)
      input_at(source_mode);
    complete_jobs(answer);
    if (response_end && t >= response_end) {
      inject(&response, response_end);
      ++anchors;
      response_end = 0;
    }
    if (t % GM7_FRAME_US == (copy ? 45000 : 40000)) {
      struct gm7_preparation prep = {0};
      uint64_t sampled = t - (copy ? 12000 : 7000);
      struct gm7_source s = source_at(sampled);
      gm7_owner_update(&server, &s, &prep, sampled);
      struct gm7_packet locator;
      assert(!gm7_owner_control(&server, &locator, sampled));
      inject(&locator, t - (copy ? 500 : 679));
    }
    fake_submit_refusal = refusals < rejected;
    unsigned before = owner.requester.sequence;
    cycle(&service_work);
    if (fake_submit_refusal && owner.requester.sequence != before)
      ++refusals;
    assert(!owner.serving && !owner.armed && !owner.reserved &&
           !owner.eligible && !resource_fault && !status.error &&
           !fake_join.serving && !phase_known);
    assert(!positive && !statuses);
    if (fake_scope.follower) {
      assert(answer && fake_client.source && fake_client.passive &&
             fake_client.root == 401 && fake_client.boot == 51 &&
             fake_scope.root_identity == 401 && owner.scope.active == 401 &&
             owner.peer[0].session.valid && owner.peer[0].kind == GM7_ANNOUNCE &&
             fake_client.until > fake_kernel && !fake_timing.retained);
      if (out)
        *out = (struct gm7_bootstrap_fixture){fake_path, fake_client,
                                              fake_scope, fake_kernel};
      printf("PASS bootstrap source=%u retained=%u copy=%u refusals=%u "
             "REQUEST=%u ANCHOR=%u selected=%llu no-positive-authority\n",
             source_mode, retained, copy, refusals, requests, anchors,
             (unsigned long long)owner.scope.active);
      return;
    }
  }
  assert(!answer && requests >= 4 &&
         (residues & (residues - 1)) && refusals == rejected);
  printf("PASS dropped response REQUEST progress=%u residues=%x, no shared "
         "phase/resource fault or positive authority\n", requests, residues);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  struct gm7_bootstrap_fixture out;
  bootstrap(false, 0, true, 0, false, &out);
  FILE *f = fopen(argv[1], "wb");
  assert(f && fwrite(&out, sizeof out, 1, f) == 1 && !fclose(f));
  bootstrap(true, 0, true, 0, true, NULL);
  bootstrap(false, 1, true, 2, false, NULL);
  bootstrap(false, 0, false, 0, false, NULL);
  advance_decode_clock = true;
  bootstrap(false, 0, true, 0, false, NULL);
  return 0;
}
