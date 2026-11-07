/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main historical_fence_cases
#include "gm7_service_test.c"
#undef main
static uint32_t recorded_reason, recorded_generation, recorded_calls;
void r2_radio_service_gm7_withdraw_cause(uint32_t reason, uint32_t generation) {
  recorded_reason = reason;
  recorded_generation = generation;
  ++recorded_calls;
  r2_radio_service_withdraw();
}
int main(void) {
  setup(true);
  healthy_input();
  uint32_t before = owner.event_generation;
  fence(73, fake_hf);
  assert(recorded_calls == 1 && recorded_reason == 73);
  assert(recorded_generation == owner.event_generation && owner.event_generation > before);
  assert(!fake_scope.gm);
  puts("PASS actual GM7 fence forwards exact known reason and postwithdraw generation to "
       "retirement hook; original owner/controller withdrawal executes");
}
