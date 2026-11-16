// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <vector>
#include <cassert>
#include <algorithm>
int main(){
 using namespace watch;
 std::vector<uint16_t> full(Width*Height),cached(Width*Height),panel(Width*Height);
 RenderCache cache;View view;view.settings.face=1;Model m;m.time_valid=true;m.motion=true;m.source=Source::BLE;m.lora_on=true;
 for(int i=0;i<240;++i){
  uint64_t now=1000000+uint64_t(i)*50000;m.sampled_us=now;m.until_us=now+250000;m.utc_us=12000000000ULL+now+(i>=200?60000000:0);
  m.calendar.astronomy_valid=true;m.calendar.today.valid=true;m.calendar.sampled_utc=m.utc_us/1000000;m.calendar.today.noon=12000;m.calendar.solar_hour_angle_deg=i*7.0;m.calendar.solar_altitude_deg=(i%40)-20;m.calendar.moon_phase=.25;m.calendar.moon_illumination=.5;m.source_rssi=-65;
  m.solar_clock=solar_opposition(i*7.0);
  m.signal_position=(i%90)/90.0;m.tide_position=(i%60)/60.0;
  m.rms_position=(i%100)/100.0;m.hold_position=(i%80)/80.0;m.reserve_position=.7;m.phase_position=(i%60)/60.0;
  view.settings.face=(i>=80&&i<85)?0:(i>=140?2:1);view.screen=(i>=120&&i<125)?Screen::Launcher:Screen::Face;view.asleep=i==180;
  render(full.data(),m,view,now);render(cached.data(),m,view,now,&cache);assert(full==cached);
  // Emulate exactly the partial panel transfer. Every changed pixel must be sent.
  for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)if(cache.changed[(y/RenderCache::Tile)*RenderCache::Cols+x/RenderCache::Tile])panel[y*Width+x]=cached[y*Width+x];
  assert(panel==full);
  if(i==20)assert(std::count(cache.changed,cache.changed+sizeof cache.changed,uint8_t(1))<RenderCache::Cols*RenderCache::Rows/2);
 }
}
