#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Immutable actuator state. Integer epoch subtraction preserves nanoseconds.
struct ClockPrediction {
 uint64_t tick=0;
 int64_t epoch=0,phase=0;
 double rate=0;
 // Bounded drift forecast. After ten seconds retain the terminal frequency,
 // rather than extrapolating a quadratic indefinitely through an outage.
 double acceleration=0;
 uint64_t acceleration_until=0;
 int64_t at(uint64_t now) const {
  if(!tick || now<tick)return 0;
  const auto elapsed=now-tick;
  const double slew=std::copysign(std::min(std::abs(double(phase)),double(elapsed)/40),double(phase));
  const double dt=double(elapsed)/20000000;
  const double limit=acceleration_until?(acceleration_until>tick?double(acceleration_until-tick)/20000000:0):10.0,h=std::min(dt,limit);
  return epoch+int64_t(elapsed)*50+int64_t(std::llround(dt*rate+acceleration*h*(dt-h/2)+slew));
 }
};
