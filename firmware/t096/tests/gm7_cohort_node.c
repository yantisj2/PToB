/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * One unseeded production service instance. The external radio model owns
 * delivery and complete-event outcomes; no peer/session/HOT is injected.
 */
#define R2_TIMING_EVENTS_TEST 1
#define main historical_service_main
#include "gm7_service_test.c"
#undef main
#include "gm7_cohort_bridge.h"

static uint64_t origin, phase;
void r2_timing_event(unsigned kind, int result, uint64_t peer, uint32_t detail,
                     uint32_t a, uint32_t b) {
  printf("V %u %d %llu %u %u %u\n", kind, result, peer, detail, a, b);
}
static void cohort_init(uint64_t id, uint64_t incarnation, uint64_t offset,
                        uint64_t utc_phase) {
  setup(false); /* Only platform boundary defaults; discard owner fixture. */
  origin = offset;
  phase = utc_phase;
  fake_hf = UINT64_C(66000000) + origin;
  fake_kernel = UINT64_C(500000000) + origin * 3;
  identity = id;
  boot = incarnation;
  assert(!gm7_owner_init(&owner, &group, identity, boot, true, fake_hf));
  gm7_owner_reservation_guard(&owner, reservation_fence, NULL);
  gm7_scheduler_init(&scheduler, fake_hf);
  memset(&fake_scope, 0, sizeof fake_scope);
  memset(&fake_timing, 0, sizeof fake_timing);
  memset(&fake_client, 0, sizeof fake_client);
  roster.identity[0] = identity;
  status_cycle = diag_frame = primary_frame = UINT64_MAX;
  takeover_preparing = promoted_valid = phase_known = false;
  phase_step_at = takeover_bound = 0;
  fake_rx_count = 0;
  management_rx = management_mac = management_accepted = 0;
}
static void cohort_source(void) {
  healthy_input();
  /* The isolated source fixture starts its relative coordinate at the oldest
   * of three PPS samples. Cohort PT requires a continuous epoch coordinate. */
  uint64_t shift=(fake_hf-fake_hf%1000000-2000000)*1000;
  for(unsigned i=0;i<R2_SOURCE_MAP_HISTORY;i++)
    functional_source.maps[i].anchor_ns+=shift;
  functional_source.utc_offset_ns-=(int64_t)shift;
  /* External model's independent functional-UTC phase; not an accuracy bound. */
  functional_source.utc_offset_ns +=
      ((int64_t)phase - (int64_t)origin) * 1000;
  input.last_rmc_utc = INT64_C(1792800000000000) +
                      (int64_t)(fake_hf - origin + phase);
}
int main(int argc, char **argv) {
  bool physical=argc>1 && !strcmp(argv[1],"--physical");
  fake_handoff_enforced=physical;
  char command[1024];
  setvbuf(stdout, NULL, _IOLBF, 0);
  while (fgets(command, sizeof command, stdin)) {
    uint64_t a, b, c, d;
    if (sscanf(command, "I %llu %llu %llu %llu", &a, &b, &c, &d) == 4) {
      cohort_init(a, b, c, d);
      puts("E");
    } else if (sscanf(command, "C %llu %llu %llu", &a, &b, &c) == 3) {
      unsigned slot = (unsigned)a;
      assert(slot < GM7_JOB_SLOTS);
      struct gm7_job *j = &scheduler.job[slot];
      if (fake_native_live[slot] && j->ticket == b) {
        fake_native_live[slot] = false;
        uint64_t nominal = j->window.nominal;
        assert(fake_result_count < GM7_JOB_SLOTS);
        fake_result[fake_result_count++] = (struct r2_gm7_tx){
          .ticket = j->ticket, .generation = j->generation,
          .buffer_version = j->buffer_version, .epoch = 4,
          .primary_address = (uint32_t)((nominal + 42) * 16),
          .aux_address = (uint32_t)((nominal + 551) * 16),
          .aux_end = (uint32_t)((nominal + 1400) * 16), .slot = slot,
          .complete = !!c, .exposed = !!c};
      }
    } else if (sscanf(command, "D %llu", &a) == 1) {
      char *hex = strchr(command + 2, ' ');
      assert(hex && fake_rx_count < 3);
      ++hex;
      struct r2_gm7_rx *r = &fake_rx[fake_rx_count++];
      memset(r, 0, sizeof *r);
      r->raw = (uint32_t)((a + origin) * 16);
      r->epoch = 4;
      for (unsigned n = 0; hex[n * 2] && hex[n * 2] != '\n'; n++) {
        unsigned v;
        assert(n < sizeof r->ad && sscanf(hex + n * 2, "%2x", &v) == 1);
        r->ad[n] = (uint8_t)v;
        r->length = (uint16_t)(n + 1);
      }
      struct gm7_packet p;
      if (!gm7_ad_decode(&p,r->ad,r->length,&group) && p.kind == GM7_ANCHOR &&
          p.body.anchor.requester == identity) {
        struct gm7_pending *q = &owner.requester.pending[0];
        printf("V 32 -999 %llu 60 %u %u\n",p.body.anchor.responder,
          (unsigned)(q->used | q->sent << 1 |
          (q->request.nonce_lo == p.body.anchor.nonce_lo) << 2),
          (unsigned)(a + origin - q->tx));
      }
    } else if (command[0]=='P') {
      struct gm7_cohort_output out;
      assert(strlen(command+2)>=sizeof out*2);
      for(unsigned n=0;n<sizeof out;n++) {
        unsigned v;assert(sscanf(command+2+n*2,"%2x",&v)==1);
        ((uint8_t *)&out)[n]=(uint8_t)v;
      }
      fake_timing=out.timing;fake_pt_closed=out.closed;
      fake_handoff_valid=out.ready;fake_promote_ready=out.ready;
    } else if (sscanf(command, "T %llu", &a) == 1) {
      tick_to(a + origin);
      cohort_source();
      cycle(&service_work);
      bool control_live=owner.serving || (owner.scope.active &&
          fake_hf<owner.selected_until && fake_hf<owner.selected_source_until);
      printf("S %u %u %llu %llu %llu %llu %u %u %u %d %u %llu %llu %u\n",
        owner.serving, owner.armed, owner.scope.active, owner.scope.birth,
        owner.scope.term, owner.requester.sequence,
        (unsigned)management_mac, (unsigned)management_accepted,
        owner.drops, status.error, scan_aligned, owner.scope.founder, owner.scope.boot,
        control_live);
      for (unsigned slot = 0; slot < GM7_JOB_SLOTS; slot++) {
        struct gm7_job *j = &scheduler.job[slot];
        if (!fake_native_live[slot]) continue;
        printf("J %u %llu %llu %llu %u %u ", slot, j->ticket,
          j->window.nominal - origin, j->window.latest_end - origin,
          j->positive, j->kind);
        for (unsigned n = 0; n < j->length; n++) printf("%02x", j->ad[n]);
        putchar('\n');
      }
      if (physical) {
        struct gm7_cohort_input in={.path=fake_path,.anchor=fake_client,
            .scope=fake_scope,.source=functional_source,.kernel=fake_kernel,
            .hf=fake_hf,.resume_until=fake_resume_until,
            .utc_ns=(int64_t)owner.source.utc_us*1000,
            .source_epoch=owner.source.source_gen,
            .source_ready=owner.source.physical_ready && owner.eligible,
            .handoff=takeover_preparing,.root=owner.scope.active,
            .boot=owner.scope.boot,.timing_epoch=owner.scope.timing_epoch};
        printf("O ");
        for(unsigned n=0;n<sizeof in;n++)printf("%02x",((uint8_t*)&in)[n]);
        putchar('\n');
      }
      puts("E");
    } else assert(!"bad cohort command");
  }
  return 0;
}
