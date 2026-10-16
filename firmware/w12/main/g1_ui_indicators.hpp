#pragma once
#include "g1_relative_quality.hpp"
namespace v12 {
// Presentation only: counts are receive/controller callbacks. In particular a
// TX flash never asserts an actual RF timestamp, calibrated edge or authority.
class G1ActivityLamp {
  uint32_t last_=0;uint64_t until_=0,again_=0;
  bool seen_=false;
 public:
  bool update(uint32_t count,uint64_t now,bool enabled){
    if(!enabled){last_=count;seen_=true;until_=again_=0;return false;}
    if(!seen_){last_=count;seen_=true;return false;}
    if(count!=last_){
      last_=count;
      // Do not stretch a pulse into a solid light under continuous traffic.
      if(now>=again_&&now<=UINT64_MAX-1200000){until_=now+800000;again_=until_+400000;}
    }
    return now<until_;
  }
};
class G1UiIndicators {
  G1ActivityLamp rx_,tx_;
  G1RelativeQuality quality_{};G1ClockPrediction clock_{};
  uint64_t until_=0;bool locked_=false,gps_configured_=false;
 public:
  struct Lamps {bool pps=false,rx=false,tx=false,gps=false,subus=false;};
  explicit G1UiIndicators(bool gps_configured=false):gps_configured_(gps_configured){}
  void quality(const G1RelativeQuality& q,const G1ClockPrediction& clock,
               bool locked,uint64_t until20){quality_=q;clock_=clock;locked_=locked;until_=until20;}
  Lamps update(uint64_t now,uint32_t rx,uint32_t tx,bool healthy,bool tx_armed,
               uint64_t gps_packet20,uint64_t gps_pps20,bool gps_fix){
    Lamps out;
    // An initialized UART is not evidence that a GPS receiver is present.
    const auto fresh=[&](uint64_t at,uint64_t age){return at&&now>=at&&now-at<age;};
    out.gps=fresh(gps_packet20,40000000)||fresh(gps_pps20,40000000);
    const bool rx_on=rx_.update(rx,now,healthy),tx_on=tx_.update(tx,now,healthy&&tx_armed);
    out.subus=healthy&&locked_&&now<until_&&quality_.locked(now,clock_);
    // Configured GNSS units keep these lamps on physical input, including
    // while acquiring or after signal loss. Presence still requires fresh data.
    if(gps_configured_||out.gps){out.pps=fresh(gps_pps20,2000000);out.rx=fresh(gps_packet20,2000000);out.tx=gps_fix;}
    else {
      const auto utc=out.subus?clock_.at(now):0;
      out.pps=utc>0&&utc%INT64_C(1000000000)<100000000;
      out.rx=rx_on;out.tx=tx_on;
    }
    return out;
  }
};
}
