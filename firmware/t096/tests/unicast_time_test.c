/* SPDX-License-Identifier: Apache-2.0 */
#include "ucs_vectors.h"
#include "unicast_time.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void command(uint8_t w[72], const struct r2_group_config *key,
                    unsigned op, uint32_t seq, uint64_t stream) {
  memcpy(w, vector_on, 72);
  w[4] = op;
  r2_group_put(w + 32, stream, 8);
  r2_group_put(w + 40, seq, 4);
  assert(!uc1_sign(w, 72, UC1_CONTROL, key));
}
static void shared_pool(const struct r2_group_config *key) {
  struct uc1_session clients[4] = {0};
  struct uc1_session *sessions[4];
  uint8_t w[72];
  for (unsigned i = 0; i < 4; i++) {
    sessions[i] = &clients[i];
    clients[i].boot = 44;
    clients[i].challenge = 55;
  }
  const unsigned expected[4][4] = {
      {16, 0, 0, 0}, {8, 8, 0, 0}, {8, 4, 4, 0}, {4, 4, 4, 4}};
  for (unsigned count = 0; count < 4; count++) {
    memcpy(w, vector_on, 72);
    w[6] = UC1_ALLOW_DOWNGRADE | UC1_SUPPORT_8HZ;
    assert(!uc1_sign(w, 72, UC1_CONTROL, key));
    assert(uc1_control(&clients[count], w, 72, key, 1000000, 77 + count, 185,
                       30000) == 1);
    assert(uc1_pool(sessions, 1000000));
    unsigned total = 0;
    for (unsigned i = 0; i < 4; i++) {
      assert(clients[i].hz == expected[count][i]);
      total += clients[i].hz;
    }
    assert(total <= 16);
  }
  /* No upgrade when clients depart, renew, expire or send OFF followed by ON.
   */
  for (unsigned i = 1; i < 4; i++)
    clients[i].hz = 0;
  assert(uc1_pool(sessions, 2000000) && clients[0].hz == 4);
  command(w, key, UC1_RENEW, 2, 77);
  assert(uc1_control(&clients[0], w, 72, key, 2000000, 99, 185, 30000) == 1 &&
         clients[0].hz == 4);
  command(w, key, UC1_OFF, 3, 77);
  assert(uc1_control(&clients[0], w, 72, key, 2000001, 99, 185, 30000) == 1);
  command(w, key, UC1_ON, 4, 0);
  w[6] = 3;
  assert(!uc1_sign(w, 72, UC1_CONTROL, key));
  assert(uc1_control(&clients[0], w, 72, key, 2000002, 99, 185, 30000) == 1 &&
         clients[0].hz == 4);
  command(w, key, UC1_ON, 5, 0);
  w[6] = 3;
  assert(!uc1_sign(w, 72, UC1_CONTROL, key));
  assert(uc1_control(&clients[0], w, 72, key, clients[0].until, 100, 185,
                     30000) == 1 &&
         clients[0].hz == 4);
  /* Reconnection creates a fresh session and can use the full budget again. */
  clients[0] = (struct uc1_session){.boot = 44, .challenge = 55};
  memcpy(w, vector_on, 72);
  w[6] = 3;
  assert(!uc1_sign(w, 72, UC1_CONTROL, key));
  assert(uc1_control(&clients[0], w, 72, key, 3000000, 101, 185, 30000) == 1);
  assert(uc1_pool(sessions, 3000000) && clients[0].hz == 16);
  /* Mixed 8+4+4 fills the same budget. */
  for (unsigned i = 0; i < 4; i++)
    clients[i] = (struct uc1_session){.boot = 44, .challenge = 55};
  for (unsigned i = 0; i < 3; i++) {
    memcpy(w, vector_on, 72);
    w[5] = i ? 4 : 8;
    w[6] = 3;
    assert(!uc1_sign(w, 72, UC1_CONTROL, key));
    assert(uc1_control(&clients[i], w, 72, key, 4000000, 110 + i, 185, 30000) ==
           1);
  }
  assert(uc1_pool(sessions, 4000000) && clients[0].hz == 8 &&
         clients[1].hz == 4 && clients[2].hz == 4);
  /* Refusal is transactional when every peer requires an exact rate. */
  for (unsigned i = 0; i < 4; i++)
    clients[i] =
        (struct uc1_session){.hz = 16, .ceiling = 16, .until = 5000000};
  assert(!uc1_pool(sessions, 4000000));
  for (unsigned i = 0; i < 4; i++)
    assert(clients[i].hz == 16);
  /* Old clients do not advertise support for the newly added 8 Hz grant. */
  clients[0] = (struct uc1_session){.hz = 16, .until = 5000000, .options = 1};
  clients[1] = (struct uc1_session){.hz = 4, .until = 5000000, .options = 3};
  clients[2].hz = clients[3].hz = 0;
  assert(uc1_pool(sessions, 4000000) && clients[0].hz == 4);
  /* Four compressed arrivals fit; a fifth does not. Long idle adds no burst. */
  uint64_t budget = 0;
  for (unsigned i = 0; i < 5; i++)
    assert(uc1_credit(&budget, 1000000, 16, 4) == (i < 4));
  assert(!uc1_credit(&budget, 1062499, 16, 4));
  assert(uc1_credit(&budget, 1062500, 16, 4));
  budget = 0;
  for (unsigned second = 0; second < 1000; second++)
    for (unsigned lane = 0; lane < 4; lane++)
      for (unsigned client = 0; client < 4; client++)
        assert(uc1_credit(&budget,
                          1000000 + (uint64_t)second * 1000000 + lane * 250000,
                          16, 4));
  assert(!uc1_credit(&budget, UINT64_MAX, 16, 4));
  puts("PASS shared16: 16; 8+8; 8+4+4; 4+4+4+4; no increases through "
       "departure/renew/OFF/expiry; reconnect reset; legacy grant "
       "compatibility; transactional exact-rate refusal; aggregate 16Hz "
       "compressed-arrival bound");
}
int main(void) {
  struct r2_group_config group = {.psk = {1, 2, 3}, .id = 7, .epoch = 9}, key;
  uc1_key(&key, &group, UINT64_C(0x1000000000000003));
  assert(!memcmp(key.psk, vector_key, 16));
  shared_pool(&key);
  struct uc1_session eight = {.boot = 44, .challenge = 55};
  assert(uc1_control(&eight, vector_on8, 72, &key, 1, 77, 185, 30000) == 1 &&
         eight.hz == 8);
  struct uc1_session shared = {.boot = 44, .challenge = 55};
  assert(uc1_control(&shared, vector_on16_shared, 72, &key, 1, 77, 185,
                     50000) == 1 &&
         shared.hz == 8);
  assert(uc1_verify(vector_on, 72, UC1_CONTROL, &key));
  assert(uc1_verify(vector_sample, 144, UC1_SAMPLE, &key));
  struct uc1_session s = {.boot = 44, .challenge = 55};
  uint8_t w[72];
  assert(uc1_control(&s, vector_on, 72, &key, 1000000, 77, 185, 30000) == 1 &&
         s.hz == 16);
  uint64_t until = s.until;
  assert(uc1_control(&s, vector_on, 72, &key, 2000000, 88, 185, 30000) == 0 &&
         s.until == until && s.stream == 77);
  assert(uc1_probe(&s, vector_probe, 64, &key, 2000000));
  assert(!uc1_probe(&s, vector_probe, 64, &key, 2100000));
  assert(!uc1_live(&s, until));
  assert(uc1_live(&s, until - 1));
  command(w, &key, UC1_RENEW, 2, 77);
  assert(uc1_control(&s, w, 72, &key, until - 1, 88, 185, 30000) == 1 &&
         s.until == until - 1 + UC1_LEASE_US);
  command(w, &key, UC1_OFF, 3, 77);
  assert(uc1_control(&s, w, 72, &key, until, 88, 185, 30000) == 1 &&
         !uc1_live(&s, until));
  assert(uc1_control(&s, vector_on, 72, &key, until + 1, 88, 185, 30000) < 0);
  s = (struct uc1_session){.boot = 44, .challenge = 55};
  assert(uc1_control(&s, vector_on, 72, &key, 1, 77, 185, 50000) == 1 &&
         s.hz == 4 && s.reason == UC1_CAPACITY);
  s = (struct uc1_session){.boot = 44, .challenge = 55};
  assert(uc1_control(&s, vector_on, 72, &key, 1, 77, 185, 150000) == 1 &&
         s.hz == 1);
  s = (struct uc1_session){.boot = 44, .challenge = 55};
  assert(uc1_control(&s, vector_on, 72, &key, 1, 77, 23, 30000) == 1 && !s.hz &&
         s.reason == UC1_MTU);
  /* Real BLE arrival compression must not turn a correctly paced client into
   * a stream of timeouts. One pending input/notification remains independent.
   */
  s = (struct uc1_session){.boot = 44, .challenge = 55};
  assert(uc1_control(&s, vector_on, 72, &key, 1, 77, 185, 50000) == 1 &&
         s.hz == 4);
  uint8_t probe[64];
  const uint32_t jitter[4] = {100000, 160000, 0, 90000};
  for (unsigned i = 0; i < 40; i++) {
    memcpy(probe, vector_probe, 64);
    r2_group_put(probe + 4, i + 1, 4);
    assert(!uc1_sign(probe, 64, UC1_PROBE, &key));
    assert(uc1_probe(&s, probe, 64, &key,
                     1000000 + (uint64_t)i * 250000 + jitter[i % 4]));
    assert(!uc1_probe(&s, probe, 64, &key,
                      1000000 + (uint64_t)i * 250000 + jitter[i % 4]));
  }
  s = (struct uc1_session){.boot = 44, .challenge = 55};
  assert(uc1_control(&s, vector_on, 72, &key, 1, 77, 185, 30000) == 1);
  for (unsigned i = 0; i < 3; i++) {
    memcpy(probe, vector_probe, 64);
    r2_group_put(probe + 4, i + 1, 4);
    assert(!uc1_sign(probe, 64, UC1_PROBE, &key));
    assert(uc1_probe(&s, probe, 64, &key, 1000000) == (i < 2));
  }
  uint64_t debt = s.rate_next_at;
  command(w, &key, UC1_RENEW, 2, 77);
  assert(uc1_control(&s, w, 72, &key, 1000001, 88, 185, 30000) == 1 &&
         s.rate_next_at == debt);
  assert(!uc1_probe(&s, probe, 64, &key, 1000001));
  assert(!uc1_probe(&s, probe, 64, &key, UINT64_MAX));
  for (unsigned i = 0; i < 72; i++) {
    memcpy(w, vector_on, 72);
    w[i] ^= 1;
    assert(!uc1_verify(w, 72, UC1_CONTROL, &key));
  }
  assert(!uc1_verify(vector_sample, 144, UC1_STATE, &key));
  puts("PASS UCS1 cross-language vectors, replay/duplicate lease, renewal/OFF, "
       "expiry, capacity/MTU, bounded jitter credit and all-byte tampering");
}
