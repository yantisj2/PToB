#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "td_signal.hpp"
namespace v12 {
constexpr uint16_t TdDeliveryWarningPermille=850;
// Worker-owned observations, never source admission, output or clock training.
struct TdPeer {
  TdSignal signal{};
  uint64_t id = 0, root = 0, seen_us = 0, sampled_us = 0, channels = 0;
  uint64_t boot = 0,pa_seen_us=0,quality_us=0,time_seen_us=0;
  uint32_t source_epoch=0,source_ttl_us=UINT32_MAX,origin_age_us=UINT32_MAX;
  bool time_known=false,time_valid=false;
  uint32_t advertisements = 0, packets = 0, matched = 0, rejected = 0, missed = 0, duplicates = 0;
  uint32_t jitter_ns = UINT32_MAX, max_ns = 0, source_age_us = UINT32_MAX;
  uint8_t depth = 0, phy = 0, stream_slot = UINT8_MAX;
  int8_t rssi = 127, rssi_min = 127, rssi_max = -127;
  bool relay = false, publishing = false, ready = false;
};
struct TdTrace {
  uint64_t at_us = 0, id = 0;
  uint32_t jitter_ns = UINT32_MAX;
  uint16_t delivery = UINT16_MAX;
  int8_t rssi=127;
};
struct TdSnapshot {
  TdPeer peers[8]{};
  TdTrace gm[24]{}, relay[24]{};
  uint64_t now_us = 0, target = 0, last_target = 0, request_target = 0, request_until_us = 0;
  uint32_t switches = 0, sync_failures = 0, unmatched = 0;
  uint8_t phase = 0, count = 0;
  bool enabled = false, request = false, pending = false, owner_stale = false;
  uint64_t chart_gm=0,chart_relay=0,plot_gm=0,plot_relay=0;
  uint32_t chart_switches=0;
  struct Role {
    enum Result : uint8_t {Waiting,NoReport,Invalid,Expired,WrongScope,Replay,Report};
    Result result=Waiting;
    uint64_t hot=0,root=0,root_boot=0,seen_us=0,checked_us=0,next_us=0,until_us=0;
    uint32_t sequence=0,checks=0,misses=0,source_epoch=0;
    bool known=false,available=false;
  } role{};
};
class TdModel {
  struct Original {
    uint64_t rx = 0;
    uint32_t publication = 0;
    uint16_t event = 0;
    uint8_t channel = 255;
    bool used = false;
  };
  Original originals[16]{};
  unsigned head = 0;
  uint64_t last_rx = 0, last_tx = 0, scope_boot = 0;
  uint32_t scope_train = 0, scope_epoch = 0;
  uint32_t observations = 0;
  double rate = 1, squares = 0;
  uint32_t tail = 0;
  uint16_t last_event = 0;
  bool event_seen = false;
  uint32_t stint_received=0,stint_missed=0,stint_jitter=UINT32_MAX,rssi_count=0;
  int64_t rssi_sum=0;
  struct Choice {uint64_t id=0,chosen=0,challenger=0,since=0,absent=0;} choice[2]{};

public:
  TdSnapshot view{};
  static bool fresh(const TdPeer &p, uint64_t now) {
    return p.id && now >= p.seen_us && now - p.seen_us < 10000000;
  }
  TdPeer &peer(uint64_t id) {
    unsigned slot = 0;
    for (unsigned i = 0; i < 8; i++) {
      if (view.peers[i].id == id)
        return view.peers[i];
      if (!view.peers[i].id || view.peers[i].seen_us < view.peers[slot].seen_us)
        slot = i;
    }
    view.peers[slot] = {};
    view.peers[slot].id = id;
    return view.peers[slot];
  }
  void seen(uint64_t id, uint64_t root, bool relay, uint8_t depth, bool publishing, bool ready,
            int8_t rssi, uint8_t phy, uint64_t now, uint8_t stream_slot = UINT8_MAX) {
    auto &p = peer(id);
    p.root = root;
    p.relay = relay;
    p.depth = depth;
    p.publishing = publishing;
    p.stream_slot = relay && publishing && stream_slot > 0 && stream_slot <= 3
                        ? stream_slot : UINT8_MAX;
    p.ready = ready;
    p.seen_us = now;
    p.phy = phy;
    if (p.advertisements != UINT32_MAX)
      ++p.advertisements;
    if (rssi != 127) {
      p.signal.observe(rssi,now);
      p.rssi = rssi;
      p.rssi_min = std::min(p.rssi_min, rssi);
      p.rssi_max = std::max(p.rssi_max, rssi);
    }
  }
  static const TdPeer* best_peer(const TdSnapshot& snapshot,bool relay, uint64_t now, bool nearest = false) {
    const TdPeer *winner = nullptr;
    for (const auto &p : snapshot.peers) {
      if (!fresh(p, now) || p.relay != relay || (!nearest && !p.publishing))
        continue;
      const bool quality = p.quality_us && now >= p.quality_us && now - p.quality_us < 30000000 &&
                           p.jitter_ns != UINT32_MAX;
      const bool oldquality = winner && winner->quality_us && now >= winner->quality_us &&
                              now - winner->quality_us < 30000000 &&
                              winner->jitter_ns != UINT32_MAX;
      bool better = !winner;
      if (winner) {
        if (nearest)
          better = p.rssi != 127 && (winner->rssi == 127 || p.rssi > winner->rssi);
        else if (quality != oldquality)
          better = quality;
        else if (quality && p.jitter_ns != winner->jitter_ns)
          better = p.jitter_ns < winner->jitter_ns;
        else
          better = p.rssi != 127 && (winner->rssi == 127 || p.rssi > winner->rssi);
        if (!better && p.rssi == winner->rssi && (!quality || p.jitter_ns == winner->jitter_ns))
          better = p.id < winner->id;
      }
      if (better)
        winner = &p;
    }
    return winner;
  }
  uint64_t best(bool relay,uint64_t now,bool nearest=false)const {
    const auto* winner=best_peer(view,relay,now,nearest);
    return winner?winner->id:0;
  }
  void update_chart(uint64_t now){
    for(unsigned kind=0;kind<2;kind++){
      auto& c=choice[kind];const auto* candidate=best_peer(view,kind,now);const TdPeer* current=nullptr;
      for(const auto& p:view.peers)if(p.id==c.id&&c.id)current=&p;
      const bool live=current&&fresh(*current,now)&&current->publishing;
      bool select=!c.id&&candidate;
      if(c.id&&!live){if(!c.absent)c.absent=now;c.challenger=c.since=0;
        select=candidate&&now>=c.absent&&now-c.absent>=20000000;
      }else if(live){
        c.absent=0;bool better=false;
        if(candidate&&candidate->id!=c.id){
          const bool nq=candidate->quality_us&&now>=candidate->quality_us&&now-candidate->quality_us<30000000&&candidate->jitter_ns!=UINT32_MAX;
          const bool oq=current->quality_us&&now>=current->quality_us&&now-current->quality_us<30000000&&current->jitter_ns!=UINT32_MAX;
          better=(nq&&(!oq||(uint64_t(candidate->jitter_ns)*4<=uint64_t(current->jitter_ns)*3&&candidate->jitter_ns<current->jitter_ns)))||
            (candidate->rssi!=127&&current->rssi!=127&&int(candidate->rssi)>=int(current->rssi)+6&&(!oq||(nq&&candidate->jitter_ns<=current->jitter_ns)));
        }
        if(better){if(c.challenger!=candidate->id){c.challenger=candidate->id;c.since=now;}
          select=now>=c.chosen&&now-c.chosen>=30000000&&now>=c.since&&now-c.since>=15000000;
        }else{c.challenger=0;c.since=0;}
      }
      if(select&&candidate&&candidate->id!=c.id){c={candidate->id,now,0,0,0};inc(view.chart_switches);}
      (kind?view.chart_relay:view.chart_gm)=c.id;
    }
  }
  void source(uint64_t id,uint32_t epoch,uint64_t issued,uint64_t expires,uint64_t tx,uint64_t now){
    auto& p=peer(id);p.source_epoch=epoch;p.time_seen_us=now;p.time_known=true;
    p.time_valid=epoch&&tx&&issued<=tx&&tx<expires;
    p.origin_age_us=p.time_valid?uint32_t(std::min<uint64_t>(UINT32_MAX-1,(tx-issued)/1000)):UINT32_MAX;
    p.source_ttl_us=p.time_valid?uint32_t(std::min<uint64_t>(UINT32_MAX-1,(expires-tx)/1000)):0;
  }
  void signal(int8_t rssi,uint64_t now){if(rssi==127)return;auto& p=peer(view.target);p.signal.observe(rssi,now);p.rssi=rssi;p.rssi_min=std::min(p.rssi_min,rssi);p.rssi_max=std::max(p.rssi_max,rssi);rssi_sum+=rssi;inc(rssi_count);}
  static void inc(uint32_t &n) {
    if (n != UINT32_MAX)
      ++n;
  }
  void begin(uint64_t id) {
    view.target = id;
    if(id)view.last_target=id;
    inc(view.switches);
    stint_received=stint_missed=rssi_count=0;stint_jitter=UINT32_MAX;rssi_sum=0;
    head = 0;
    for (auto &o : originals)
      o = {};
    last_rx = last_tx = scope_boot = 0;
    scope_train = scope_epoch = observations = tail = 0;
    squares = 0;
    rate = 1;
    event_seen = false;
  }
  void scope(uint64_t boot, uint32_t train, uint32_t epoch) {
    if (boot != scope_boot || train != scope_train || epoch != scope_epoch) {
      auto id = view.target;
      begin(id);
      scope_boot = boot;
      scope_train = train;
      scope_epoch = epoch;
    }
  }
  void packet(uint32_t publication, uint16_t event, uint8_t channel, uint64_t rx, uint64_t now) {
    auto &p = peer(view.target);
    if (event_seen) {
      const auto gap = uint16_t(event - last_event);
      if (!gap) {
        inc(p.duplicates);
        return;
      }
      if (gap >= 32768) {
        inc(p.rejected);
        return;
      }
      if (gap < 32768 && gap > 1){
        p.missed = uint32_t(std::min<uint64_t>(UINT32_MAX, uint64_t(p.missed) + gap - 1));
        stint_missed=uint32_t(std::min<uint64_t>(UINT32_MAX,uint64_t(stint_missed)+gap-1));
      }
    }
    event_seen = true;
    last_event = event;
    inc(p.packets);
    inc(stint_received);
    if (channel < 37)
      p.channels |= uint64_t(1) << channel;
    originals[head++ % 16] = {rx, publication, event, channel, true};
    p.sampled_us = now;
  }
  static bool rx_stale(uint64_t rx, uint64_t now) {
    return now > UINT64_MAX / 20 || now * 20 < rx || now * 20 - rx > 5000000;
  }
  bool correction(uint32_t publication, uint16_t event, uint8_t channel, uint64_t tx, uint64_t now,
                  uint32_t age = UINT32_MAX) {
    for (auto &o : originals)
      if (o.used && o.publication == publication && o.event == event && o.channel == channel) {
        if (!tx || rx_stale(o.rx, now))
          return false;
        o.used = false;
        auto &p = peer(view.target);
        inc(p.matched);
        p.source_age_us = age;
        if (last_rx && o.rx > last_rx && tx > last_tx) {
          const auto dr = o.rx - last_rx, dt = tx - last_tx;
          if (dr <= 40000000 && dt <= 2000000000) {
            const double rxns = double(dr) * 50;
            if (observations) {
              const double e = rxns - double(dt) * rate;
              const auto magnitude = uint32_t(std::min<double>(UINT32_MAX - 1, std::fabs(e)));
              squares += e * e;
              tail = std::max(tail, magnitude);
              p.jitter_ns =
                  uint32_t(std::min<double>(UINT32_MAX - 1, std::sqrt(squares / observations)));
              stint_jitter=p.jitter_ns;p.quality_us=now;
              p.max_ns = tail;
            }
            const double measured = rxns / double(dt);
            rate = observations ? rate * .875 + measured * .125 : measured;
            ++observations;
          } else {
            observations = tail = 0;
            squares = 0;
            rate = 1;
          }
        }
        last_rx = o.rx;
        last_tx = tx;
        p.sampled_us = now;
        return true;
      }
    return false;
  }
  void finish(uint64_t now) {
    if (!view.target)
      return;
    auto &p = peer(view.target);
    auto *trace = p.relay ? view.relay : view.gm;
    for (unsigned i = 0; i < 23; i++)
      trace[i] = trace[i + 1];
    const auto total = uint64_t(stint_received) + stint_missed;
    trace[23] = {now, p.id, observations > 1 ? stint_jitter : UINT32_MAX,
                 uint16_t(total?uint64_t(stint_received)*1000/total:UINT16_MAX),
                 rssi_count?int8_t(rssi_sum/int64_t(rssi_count)):int8_t(127)};
    auto& plot=p.relay?view.plot_relay:view.plot_gm;
    const auto selected=p.relay?view.chart_relay:view.chart_gm;
    if(!plot||(selected==p.id&&trace[23].jitter_ns!=UINT32_MAX))plot=p.id;
  }
  TdSnapshot snapshot(uint64_t now) const {
    auto s = view;
    s.now_us = now;
    s.count = 0;
    for (auto &p : s.peers) {
      if (fresh(p, now))
        ++s.count;
      else
        p.publishing = p.ready = false;
      if(!p.relay&&p.pa_seen_us&&now>=p.pa_seen_us&&now-p.pa_seen_us>=10000000)p.publishing=false;
    }
    bool root_visible=false;
    for(const auto& p:s.peers)if(p.id==s.role.root&&p.publishing&&fresh(p,now)&&
      (!p.boot||p.boot==s.role.root_boot)&&(!p.source_epoch||!s.role.source_epoch||p.source_epoch==s.role.source_epoch))root_visible=true;
    const auto* selected=best_peer(s,false,now);
    if(!root_visible||!selected||selected->id!=s.role.root)s.role.available=false;
    return s;
  }
};
} // namespace v12
