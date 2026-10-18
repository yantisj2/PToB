#pragma once
#include <cstdint>

namespace v12 {
// Advisory, within-boot trial policy. These observations never restore a
// parent binding, source permission, clock model, or a discovery locator.
class G1ParentHistory {
 public:
  static constexpr unsigned Capacity=16;
  static constexpr uint32_t RetainSeconds=1800,FailureDecaySeconds=30;
  static constexpr uint32_t LockRetrySeconds=300;
  static constexpr uint16_t UnknownPermille=750;
  struct Row {
    uint64_t identity=0,context=0;
    uint32_t observed=0,failed_at=0;
    uint16_t delivery=0,usable=0,delivery_samples=0,usable_samples=0;
    uint8_t failures=0;bool relay=false;uint16_t flags=0;
    uint32_t lock_failed_at=0; // Uses the row's former alignment padding.
  };
 private:
  Row rows_[Capacity]{};
  uint64_t last_us_=0;bool clock_seen_=false;
  static uint32_t seconds(uint64_t now){return uint32_t(now/1000000);}
  static uint32_t age(uint32_t now,uint32_t then){return uint32_t(now-then);}
  static bool live(const Row& r,uint32_t now){return r.identity&&age(now,r.observed)<RetainSeconds;}
  void clock(uint64_t now){
    if(clock_seen_&&(now<last_us_||now-last_us_>=uint64_t(RetainSeconds)*1000000))clear();
    else if(clock_seen_&&now/1000000!=last_us_/1000000){
      for(auto& r:rows_)if(!live(r,seconds(now)))r={};
    }
    clock_seen_=true;last_us_=now;
  }
  bool valid_clock(uint64_t now)const{return !clock_seen_||(now>=last_us_&&now-last_us_<uint64_t(RetainSeconds)*1000000);}
  Row& obtain(uint64_t id,bool relay,uint64_t now){
    clock(now);const auto s=seconds(now);unsigned at=Capacity;
    for(unsigned i=0;i<Capacity;i++)if(rows_[i].identity==id&&rows_[i].relay==relay){at=i;break;}
    if(at<Capacity&&live(rows_[at],s))return rows_[at];
    if(at==Capacity){
      at=0;
      for(unsigned i=0;i<Capacity;i++){
        if(!live(rows_[i],s)){at=i;break;}
        if(age(s,rows_[i].observed)>age(s,rows_[at].observed))at=i;
      }
    }
    rows_[at]={};rows_[at].identity=id;rows_[at].relay=relay;rows_[at].observed=s;return rows_[at];
  }
  static void ewma(uint16_t& value,uint16_t& samples,uint16_t sample){
    if(sample>1000)sample=1000;
    value=samples?uint16_t((3u*value+sample)/4u):sample;
    if(samples<UINT16_MAX)++samples;
  }
 public:
  void clear(){for(auto& r:rows_)r={};last_us_=0;clock_seen_=false;}
  void advance(uint64_t now){clock(now);}
  const Row* find(uint64_t id,bool relay,uint64_t now)const{
    if(!valid_clock(now))return nullptr;
    for(const auto& r:rows_)if(r.identity==id&&r.relay==relay&&live(r,seconds(now)))return &r;
    return nullptr;
  }
  unsigned entries(uint64_t now)const{
    if(!valid_clock(now))return 0;
    unsigned n=0;for(const auto& r:rows_)if(live(r,seconds(now)))++n;return n;
  }
  // A tag identifies only the latest authenticated context for diagnostics.
  // Stable-ID/role reputation deliberately survives a boot/root/path change;
  // it remains advisory, and the caller still authenticates every new owner.
  void authenticated(uint64_t id,bool relay,uint64_t tag,uint64_t now){
    if(!id)return;
    obtain(id,relay,now).context=tag;
  }
  void sample(uint64_t id,bool relay,uint16_t delivery,bool usable_known,uint16_t usable,uint64_t now){
    if(!id)return;
    auto& r=obtain(id,relay,now);r.observed=seconds(now);
    ewma(r.delivery,r.delivery_samples,delivery);
    if(usable_known)ewma(r.usable,r.usable_samples,usable);
    if(delivery)r.failures=0; // Actual delivery, never admission alone.
  }
  void lock_failed(uint64_t id,bool relay,uint64_t now){
    if(!id)return;
    auto& r=obtain(id,relay,now);r.flags|=1;r.lock_failed_at=seconds(now);
  }
  void locked(uint64_t id,bool relay,uint64_t now){
    clock(now);for(auto& r:rows_)if(r.identity==id&&r.relay==relay)r.flags&=uint16_t(~1u);
  }
  bool lock_deferred(uint64_t id,bool relay,uint64_t now)const{
    const auto* r=find(id,relay,now);
    return r&&(r->flags&1)&&age(seconds(now),r->lock_failed_at)<LockRetrySeconds;
  }
  unsigned failures(const Row& r,uint64_t now)const{
    const auto elapsed=age(seconds(now),r.failed_at)/FailureDecaySeconds;
    return elapsed>=r.failures?0:unsigned(r.failures-elapsed);
  }
  unsigned failed(uint64_t id,bool relay,uint64_t now){
    if(!id)return 0;
    auto& r=obtain(id,relay,now);
    unsigned n=failures(r,now);if(n<4)++n;
    r.failures=uint8_t(n);r.failed_at=r.observed=seconds(now);return n;
  }
  // Four points per dB, bounded RSSI, modest GM preference. Unknown is a
  // neutral prior, explicitly distinguished by zero sample counts.
  static constexpr int RssiPoints=4,GmBonus=16,SwitchMargin=16;
  static constexpr int DeliveryDivisor=4,UsableDivisor=8,FailurePoints=16;
  static int score(const Row* r,bool relay,int rssi,uint64_t now,const G1ParentHistory& h){
    if(rssi<-100)rssi=-100;
    if(rssi>-20)rssi=-20;
    const int delivery=r&&r->delivery_samples?r->delivery:UnknownPermille;
    const int usable=r&&r->usable_samples?r->usable:UnknownPermille;
    return (rssi+100)*RssiPoints+(delivery-int(UnknownPermille))/DeliveryDivisor+
      (usable-int(UnknownPermille))/UsableDivisor+(relay?0:GmBonus)-(r?int(h.failures(*r,now))*FailurePoints:0);
  }
};
static_assert(sizeof(G1ParentHistory::Row)==40,"Reviewed fixed history row");
static_assert(sizeof(G1ParentHistory)<=1024,"Parent history SRAM budget");
}
