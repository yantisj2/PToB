// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "mobile_time.hpp"
#include <cassert>
#include <cmath>
int main(){
 watch::InstrumentMotion motion;watch::Model m;m.time_valid=true;m.rms_ns=60;m.source_rssi=-40;
 m.calendar.tide_position_valid=true;m.calendar.tide_position=1;
 uint64_t now=1000000;motion.apply(m,now);assert(m.signal_position==0&&m.tide_position==0);
 double signal=0,tide=0;
 for(unsigned i=0;i<90;++i){now+=100000;motion.apply(m,now);
  assert(m.signal_position>=signal&&m.signal_position-signal<=.034);
  assert(m.tide_position>=tide&&m.tide_position-tide<=.013);
  signal=m.signal_position;tide=m.tide_position;
 }
 assert(m.signal_position==1&&m.tide_position==1);
 m.source_rssi=-127;now+=100000;motion.apply(m,now);
 for(unsigned i=0;i<4;++i){now+=100000;motion.apply(m,now);assert(m.signal_position==1);}
 now+=100000;motion.apply(m,now);assert(m.signal_position<1&&m.signal_position>.96);
 m.calendar.tide_position_valid=false;now+=100000;motion.apply(m,now);assert(m.tide_position==1);
 m.calendar.tide_position_valid=true;m.calendar.tide_position=0;now+=100000;motion.apply(m,now);assert(m.tide_position<1&&m.tide_position>.98);
 motion.apply(m,1);assert(std::isfinite(m.signal_position)&&std::isfinite(m.tide_position));
}
