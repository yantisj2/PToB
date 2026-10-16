#include "rc_boundary.h"
#include "rc_slots.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void listen(struct rc_slots *s, uint64_t from, uint64_t until) {
  rc_slots_listen(s, from, true);
  rc_slots_listen(s, until, true);
}
int main(void) {
  struct rc_slots s;
  rc_slots_init(&s, 1, 0);
  s.preferred_max_slot = 2;
  assert(rc_slots_begin(&s, 0, false, 0));
  for (uint8_t slot = 1; slot <= 2; ++slot) {
    rc_slots_seen(&s, slot, -40, 1);
    rc_slots_seen(&s, slot, -41, 2);
  }
  listen(&s, 0, 3000000);
  assert(rc_slots_finish(&s, 3000000, 0) && s.target == 3);
  rc_slots_commit(&s, 3000000);
  assert(s.slot == 3 && s.changes == 1);
  assert(!rc_slots_begin(&s, 11000000, true, 0));
  /* Repeated client hints and an occupied alternative cannot force a move. */
  assert(rc_slots_begin(&s, 303000000, true, 0));
  rc_slots_seen(&s, 3, -90, 303000001);
  rc_slots_seen(&s, 3, -90, 303000002);
  listen(&s, 303000000, 304000000);
  assert(rc_slots_finish(&s, 304000000, 0) && !s.moving && s.target == 3);
  assert(!rc_slots_begin(&s, 312000000, true, 0));
  /* One repeated receipt or alternating identities is not two observations. */
  assert(rc_slots_begin(&s, 604000000, true, 0));
  rc_slots_contender(&s, 10, 604000001);
  rc_slots_contender(&s, 10, 604000001);
  assert(s.conflict_hits == 1);
  rc_slots_contender(&s, 11, 604000002);
  assert(s.conflict_hits == 1);
  listen(&s, 604000000, 605000000);
  assert(rc_slots_finish(&s, 605000000, 0) && !s.moving && s.target == 3);
  assert(rc_slots_begin(&s, 905000000, true, 0));
  rc_slots_contender(&s, 10, 905000001);
  rc_slots_contender(&s, 10, 905000002);
  listen(&s, 905000000, 906000000);
  assert(rc_slots_finish(&s, 906000000, 0) && s.moving && s.target != 3);
  rc_slots_rollback(&s, 906000001);
  assert(s.slot == 3 && s.rollbacks == 1);
  /* No legal alternative retains the old slot even with a contender. */
  rc_slots_init(&s, 3, 2);
  assert(rc_slots_begin(&s, 0, true, 0));
  rc_slots_contender(&s, 10, 1); rc_slots_contender(&s, 10, 2);
  listen(&s, 0, 1000000);
  assert(rc_slots_finish(&s, 1000000, 0) && !s.moving && s.busy == 1 && s.target == 3);
  rc_slots_init(&s, 1, 0);
  assert(rc_slots_begin(&s, 0, false, 0));
  assert(!rc_slots_finish(&s, 1000000, 0));
  assert(!rc_slots_finish(&s, 2500000, 0));
  assert(rc_slots_finish(&s, 6000000, 0));
  assert(s.scan_failures == 1 && s.rollbacks == 1 && !s.available && !s.verified);
  rc_slots_init(&s, 1, 0);
  assert(rc_slots_begin(&s, 0, false, 0));
  rc_slots_listen(&s, 0, true); rc_slots_listen(&s, 500000, false);
  rc_slots_listen(&s, 1500000, true); rc_slots_listen(&s, 2000000, true);
  assert(s.scan_owned_us == 500000 && !rc_slots_finish(&s, 2000000, 0));
  rc_slots_listen(&s, 5000000, true);
  assert(rc_slots_finish(&s, 5000000, 0) && s.verified == 1);
  rc_slots_commit(&s, 5000000);
  assert(rc_slots_begin_startup_check(&s, 5000001, 0));
  listen(&s, 5000001, 8000001);
  assert(rc_slots_finish(&s, 8000001, 0) && !s.moving);
  struct rc5_routes routes = {
      .count = 4,
      .node = {
          {.id = 1, .transmitter = true},
          {.id = 2, .slot = 1, .depth = 1, .transmitter = true},
          {.id = 3, .parent = 1, .slot = 2, .depth = 2, .transmitter = true},
          {.id = 4, .parent = 2, .slot = 255, .depth = 2}}};
  assert(rc5_routes_valid(&routes));
  struct r2_group_config g = {.id = 7, .epoch = 9, .psk = {1, 2, 3}};
  struct rc5_control c = {.type = RC5_REQUEST,
                          .sender = 3,
                          .target = 2,
                          .sender_boot = 10,
                          .target_boot = 11,
                          .challenge = 12,
                          .nonce = 13,
                          .sequence = 14,
                          .lease_us = RC5_LEASE_US,
                          .slots = 36,
                          .operation = 1},
                     d;
  uint8_t wire[RC5_CONTROL_BYTES];
  assert(!rc5_control_encode(wire, &c, &routes, &g));
  assert(wire[6] == 6 && wire[11] == 4 && wire[122] == 1);
  printf("RC6_VECTOR ");
  for (unsigned i = 0; i < sizeof wire; i++)
    printf("%02x", wire[i]);
  puts("");
  assert(!rc5_control_decode(&d, wire, sizeof wire, &routes, &g) &&
         d.operation == 1 && d.slots == 36);
  wire[122] ^= 1;
  assert(rc5_control_decode(&d, wire, sizeof wire, &routes, &g));
  wire[122] ^= 1;
  wire[6] = 5;
  assert(rc5_control_decode(&d, wire, sizeof wire, &routes, &g));
  struct rc5_routes foreign = routes;
  foreign.node[1].id = 77;
  struct rc5_control hint = {.type = RC5_CAPABILITY,
                             .sender = 1,
                             .target = 255,
                             .sender_boot = 99,
                             .next_challenge = 1,
                             .slots = 36,
                             .publishing = true};
  uint8_t slot;
  assert(!rc5_control_encode(wire, &hint, &foreign, &g));
  assert(rc5_control_decode(&d, wire, sizeof wire, &routes, &g));
  assert(rc6_occupancy_hint(wire, sizeof wire, &g, 10, &slot) && slot == 1);
  assert(!rc6_occupancy_hint(wire, sizeof wire, &g, 99, &slot));
  wire[123] = 0;
  assert(!rc6_occupancy_hint(wire, sizeof wire, &g, 10, &slot));
  puts("RC6_AUTO_PASS occupied3 cooldown all_busy rollback later_parent MAC "
       "operation version two_rc");
  return 0;
}
