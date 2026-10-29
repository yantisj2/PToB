/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_radio_fixture.inc"
#include "gm7_bootstrap_fixture.h"
int main(int argc, char **argv) {
  assert(argc == 2);
  struct gm7_bootstrap_fixture f;
  FILE *file = fopen(argv[1], "rb");
  assert(file && fread(&f, sizeof f, 1, file) == 1 && !fclose(file));
  assert(!f.anchor.source && f.scope.follower && !f.scope.gm &&
         f.roster.identity[1]==f.scope.root_identity);
  assert(!r2_radio_service_init(0, &f.roster));
  fake_time = capture_us = f.at;
  capture_tick = 16000000;
  r2_radio_service_activate();
  r2_radio_service_gm7_publish(f.scope,&f.anchor,&f.roster,-1,f.at,f.anchor.until);
  bool running = false;
  precision_poll(f.at, &running);
  assert(!public_client && pt_output.scope.follower &&
         active_scope.root_identity == f.scope.root_identity &&
         active_scope.boot == f.scope.boot && !pt_input.source_ready &&
         !pt_input.source.count);
  uint64_t root_ns = UINT64_C(66000000000);
  struct r2_enrollment receive_roster = enrollment;
  for (unsigned i = 0; i < 10; i++) {
    uint64_t ns = root_ns + (uint64_t)i * 62500000;
    struct r2_precision_packet p = {
        .root = 0, .boot = f.scope.boot,
        .source_epoch = f.scope.source_epoch, .train = f.scope.publication_epoch,
        .publication = i + 1, .capture_epoch = 4, .flags = 1,
        .created_ns = ns - 1000000,
        .expires_ns = root_ns + 1000000000,
        .utc_offset_ns = INT64_C(1792800000000000000)};
    if (i) {
      p.count = 1;
      p.correction[0] = (struct r2_precise_correction){
          .publication = i, .event = (uint16_t)i, .channel = 3,
          .ticks = (root_ns + (uint64_t)(i - 1) * 62500000) * 2 / 125,
          .generation = f.scope.publication_epoch,
          .expires_ns = root_ns + 1000000000};
    }
    /* Actual root-side GRv3 codec uses its own private path order; receiver
     * decodes the full root identity into its independently remapped slot1. */
    enrollment = receive_roster;
    enrollment.identity[0] = f.scope.root_identity;
    enrollment.identity[1] = f.roster.identity[0];
    if (!i)
      generic_frame = ns - 2000000;
    packet = (struct r2_radio_record){
        .raw = 16000000 + i * 1000000 + 144, .epoch = 1,
        .length = R2_RELAY_AD_BYTES, .event = (uint16_t)(i + 1),
        .channel = 3};
    assert(!generic_encode(packet.ad, &p));
    enrollment = receive_roster;
    fake_time = capture_us = f.at + (uint64_t)i * 62500 + 1000;
    capture_tick = (uint64_t)packet.raw + 16000;
    queued = true;
    precision_step(fake_time);
    assert(!queued && !error && !pt_input.source_ready &&
           active_scope.root_identity == f.scope.root_identity &&
           precision.scope.boot == f.scope.boot && precision.remote_capture == 4 &&
           !pt_output.scope.gm);
  }
  assert(precision.accepted >= 4 && !precision.rejected &&
         timing_lamp_events >= 10 && !timing_lamp_tx);
  uint32_t accepted = precision.accepted;
  packet.ad[20] ^= 1;
  queued = true;
  precision_step(++fake_time);
  assert(precision.accepted == accepted);
  printf("PASS actual ordinary-ANN GM service output -> normal PA "
         "acquisition/full-identity GRv3 decode/captured G1 correction "
         "association accepted=%u; no localGNSS/ownsource/positiveGM "
         "authority; badMAC refused (synthetic RF edges only)\n", accepted);
  return 0;
}
