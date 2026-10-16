#include "../main/g1_ui_indicators.hpp"
#include "../legacy/button_input.hpp"
#include <cassert>
#include <cstdio>
int main(){
  constexpr uint64_t Hz=20000000;
  ButtonInput button(false);
  auto update=[&](bool down,uint64_t at){return button.update(down,at,false,false,false,false,false,true);};
  auto held=[&](uint64_t at){return button.held(at,false,false,false,false,false,true);};
  update(true,Hz);update(true,Hz+Hz/20);
  assert(held(Hz+ButtonInput::ToggleTicks-1)==ButtonInput::None);
  assert(held(Hz+ButtonInput::ToggleTicks)==ButtonInput::ToggleRelayOnly);
  assert(held(4*Hz)==ButtonInput::None);
  update(false,4*Hz);assert(update(false,4*Hz+Hz/20)==ButtonInput::None);
  update(true,5*Hz);update(true,5*Hz+Hz/20);
  update(false,5*Hz+Hz/5);assert(update(false,5*Hz+Hz/4)==ButtonInput::NextPage);
  update(true,7*Hz);update(true,7*Hz+Hz/20);
  update(false,9*Hz);assert(update(false,9*Hz+Hz/20)==ButtonInput::ToggleRelayOnly);
  ButtonInput boot(true);assert(boot.held(10*Hz,false,false,false,false,false,true)==ButtonInput::None);
  v12::G1UiIndicators lights;v12::G1RelativeQuality q;
  q.original20=10*Hz;q.samples=16;q.rms_ns=100;q.max_ns=300;q.rate_guard_ppb=20;
  v12::G1ClockPrediction clock{10*Hz,10000000000,0,0,0};
  lights.quality(q,clock,true,12*Hz);
  auto get=[&](uint64_t at,uint32_t rx=0,uint32_t tx=0,bool healthy=true,bool armed=true,uint64_t packet=0,uint64_t pps=0,bool fix=false){return lights.update(at,rx,tx,healthy,armed,packet,pps,fix);};
  auto a=get(10*Hz);assert(a.subus&&a.pps&&!a.gps&&!a.rx&&!a.tx);
  a=get(10*Hz+Hz/5,1,1);assert(a.subus&&!a.pps&&a.rx&&a.tx);
  // Continuous traffic has a40ms pulse ceiling and20ms dark interval.
  a=get(10*Hz+Hz/5+800000,2,2);assert(!a.rx&&!a.tx);
  a=get(10*Hz+Hz/5+1200000,3,3);assert(a.rx&&a.tx);
  a=get(11*Hz,3,3,false);assert(!a.pps&&!a.subus&&!a.rx&&!a.tx);
  a=get(11*Hz,3,4,true,false);assert(a.subus&&a.pps&&!a.tx);
  a=get(12*Hz);assert(!a.subus&&!a.pps); // Immutable permission expiry.
  q.max_ns=1100;lights.quality(q,clock,true,20*Hz);
  a=get(11*Hz);assert(!a.subus&&!a.pps); // Low RMS alone cannot hide a bad tail.
  q.max_ns=300;q.rate_guard_ppb=2000;lights.quality(q,clock,true,20*Hz);
  a=get(11*Hz);assert(!a.subus&&!a.pps); // Held projection crosses1us.
  lights.quality(q,clock,false,20*Hz);
  a=get(11*Hz,0,0,true,true,11*Hz,11*Hz,true);
  assert(a.gps&&a.pps&&a.rx&&a.tx&&!a.subus); // Actual GNSS lamps stay separate.
  a=get(11*Hz+2000000,0,0,true,true,11*Hz,11*Hz,true);
  assert(a.gps&&!a.pps&&!a.rx&&a.tx);
  a=get(13*Hz,0,0,true,true,11*Hz,11*Hz,true);
  assert(!a.gps&&!a.pps&&!a.rx&&!a.tx);
  // Configured GNSS lamps stay dark on missing/stale input, even while the
  // BLE model is locked and both controller activity counters change.
  v12::G1UiIndicators gps_lights{true};
  q.rate_guard_ppb=20;gps_lights.quality(q,clock,true,20*Hz);
  a=gps_lights.update(11*Hz,50,50,true,true,0,0,false);
  assert(!a.gps&&!a.pps&&!a.rx&&!a.tx&&a.subus);
  a=gps_lights.update(11*Hz,51,51,true,true,11*Hz,11*Hz,true);
  assert(a.gps&&a.pps&&a.rx&&a.tx);
  a=gps_lights.update(13*Hz,52,52,true,true,11*Hz,11*Hz,false);
  assert(!a.gps&&!a.pps&&!a.rx&&!a.tx);
  // Physical PPS still appears when BLE is unavailable; it cannot set subus.
  a=gps_lights.update(14*Hz,53,53,false,false,0,14*Hz,false);
  assert(a.gps&&a.pps&&!a.rx&&!a.tx&&!a.subus);
  std::puts("PASS main long-press single toggle/release/boot/tap and PPS/quality/expiry/GPS/RX/TX pulse gates");
}
