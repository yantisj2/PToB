#include "g1_corrected_clock.hpp"
#include "r2_discipline.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
constexpr uint64_t Epoch=UINT64_C(1750000000000000000);
struct Fixture {
  G1CorrectedClock clock;G1Anchor anchor{true,111,55,Epoch/1000,1000000,2500000,2,3,40000};
  uint32_t publication=1;uint64_t old_local=0,old_remote=0;uint16_t old_event=0;uint8_t old_channel=0;
  int64_t offset_jitter=0;double physical_rate_ppb=2000;
  int64_t relative_step=0;uint32_t remote_epoch=9;
  bool feed(uint64_t now,uint8_t channel,unsigned offset_ns=0,bool correction=true){
    const uint64_t local=now*20;const uint64_t remote=uint64_t(int64_t(now*1000+uint64_t(std::llround(double(now)*physical_rate_ppb/1000000))+offset_ns)+relative_step);
    r2_relay_packet p{};p.origin={0,1,55,2,(now/1000000)*1000000000,(now/1000000)*1000000000+1650000000,0,int64_t(Epoch)+offset_jitter,0,0};
    p.origin.created_ns=uint64_t(int64_t(p.origin.created_ns)+relative_step);p.origin.expires_ns=uint64_t(int64_t(p.origin.expires_ns)+relative_step);
    p.route={0,0,0,0,55,3,publication,remote_epoch,3,UINT32_MAX};
    if(old_local&&correction){p.count=1;p.correction[0]={publication-1,old_event,old_channel,old_remote*2/125,3};}
    periodic_rx::Stamp c{};c.actual=c.association_observed=true;c.tick20=local;c.available20=local+200;c.epoch=1;c.sequence=publication;
    c.event=uint16_t(publication);c.channel=channel;c.map_enclosure_ns=1000;
    const bool accepted=clock.receive(p,c,c.available20,now+10,anchor);
    old_local=local;old_remote=remote;old_event=c.event;old_channel=channel;++publication;return accepted;
  }
};
int main(){
  // The actuator must clear acquisition backlog without a timestamp step,
  // preserve both signs/monotonicity, and leave the fine1ppm path exact.
  for(const int direction:{-1,1}){
    G1ClockPrediction acquiring{20000000,INT64_C(40000000000),direction*50000,0,0};
    const auto epoch=acquiring.relative_at(acquiring.tick);
    assert(epoch==acquiring.epoch&&acquiring.phase_slew_ns_s()==50000);
    for(unsigned i=0;i<48;i++){
      const auto next=acquiring.tick+1250000;
      const auto delivered=acquiring.relative_at(next);
      assert(delivered>acquiring.relative_at(acquiring.tick));
      const auto remaining=acquiring.phase_remaining(next);
      acquiring={next,delivered,remaining,0,0};
    }
    assert(std::abs(acquiring.phase)<100);
    assert(std::abs(acquiring.relative_at(acquiring.tick)-epoch-INT64_C(3000000000)-direction*50000)<100);
    for(const int phase:{0,100,999,1000}){
      G1ClockPrediction fine{20000000,INT64_C(40000000000),direction*phase,0,0};
      assert(fine.phase_slew_ns_s()==1000);
      assert(fine.relative_at(21250000)==fine.epoch+62500000+direction*std::min(phase,63));
      assert(fine.phase_remaining(21250000)==direction*(phase-std::min(phase,62)));
    }
    Fixture acquisition;acquisition.physical_rate_ppb=direction*5000;
    uint64_t first_lock=0,first200=0;
    for(unsigned i=0;i<1600;i++){
      const auto now=UINT64_C(1000000)+uint64_t(i)*62500;
      acquisition.anchor.at=now;acquisition.anchor.until=now+1500000;
      acquisition.feed(now,uint8_t((i*11+i/17)%37));
      const auto model=acquisition.clock.prediction();
      const auto quality=acquisition.clock.quality(now+10);
      if(quality.locked(model.tick,model)&&!first_lock)first_lock=now;
      if(quality.estimate(model.tick,model)<=200&&!first200)first200=now;
      if(i>800){assert(quality.locked(model.tick,model));assert(quality.estimate(model.tick,model)<=200);}
    }
    assert(first_lock&&first_lock<UINT64_C(35000000));
    assert(first200&&first200<UINT64_C(35000000));
    assert(acquisition.clock.causal_max_ns()>1000); // Acquisition tails retained.
    std::printf("ACQUISITION direction=%d lock_us=%llu estimate200_us=%llu\n",direction,
      (unsigned long long)(first_lock-1000000),(unsigned long long)(first200-1000000));
  }
  Fixture f;uint64_t utc=0;
  assert(!f.feed(1000000,4));assert(!f.clock.deliver(20000200,1000010,utc));
  assert(f.feed(1062500,5));assert(f.clock.accepted()==1&&f.clock.deliver(21250200,1062510,utc));
  assert(utc>=Epoch/1000+1062500&&utc<=Epoch/1000+1062515);
  const auto permission=f.clock.until();assert(permission<=2650000);
  assert(G1Window::align(1062510,f.anchor,f.clock.covers(1062510,1062510)));
  f.clock.hold();assert(f.clock.until()==permission);
  assert(f.clock.deliver(60000000,3000000,utc)); // Valid coarse HOLD independently of expired GM permission.
  assert(!f.clock.covers(3000000,3000001));
  assert(!G1Window::align(3000000,f.anchor,f.clock.covers(3000000,3000000)));
  auto renewal=f.anchor;renewal.at=3000000;renewal.until=4000000;
  assert(renewal.live(3000000)&&!G1Window::align(3000000,renewal,f.clock.covers(3000000,3000000)));
  assert(!f.clock.deliver(20*r2_group_anchor_deadline(f.anchor.at,f.anchor.uncertainty),r2_group_anchor_deadline(f.anchor.at,f.anchor.uncertainty),utc));
  auto changed=f.anchor;changed.boot++;f.clock.scope(changed);assert(!f.clock.deliver(60000000,3000000,utc)&&!f.clock.frequency_trained());
  Fixture uncorrected;assert(!uncorrected.feed(1000000,4));assert(!uncorrected.feed(1062500,4,0,false));assert(!uncorrected.clock.until());
  // Train solely from physical corrected pairs. Alternating unknown fixed
  // channel delays must not masquerade as frequency, and no precision gates
  // are changed by the functional map.
  Fixture trained;
  for(unsigned second=1;second<65;second++){
    const uint8_t channel=uint8_t(second%4);const unsigned bias=channel*10000;
    trained.feed(uint64_t(second)*1000000,channel,bias,false);
    assert(trained.feed(uint64_t(second)*1000000+62500,uint8_t((channel+1)%4),bias));
  }
  assert(trained.clock.frequency_trained()&&trained.clock.rate_pairs()>=8);
  assert(trained.clock.predictions()>30&&trained.clock.causal_max_ns()>0);
  assert(std::abs(trained.clock.prediction().rate-2000)<10);
  const auto map=trained.clock.prediction();const auto later=map.at(map.tick+20000000);
  assert(later>map.at(map.tick)&&std::abs((later-map.at(map.tick))-1000002000)<=1000);
  const auto completion=map.tick+2000;const auto expected=map.at(completion);
  assert(trained.clock.finalize(completion)&&trained.clock.prediction().tick==completion&&trained.clock.prediction().at(completion)==expected);
  assert(!trained.clock.deliver(completion-1,64062510,utc));
  trained.clock.withdraw();assert(!trained.clock.frequency_trained()&&!trained.clock.prediction().tick&&std::abs(trained.clock.prediction().rate-2000)<10);
  Fixture varying_utc;
  for(unsigned second=1;second<65;second++){
    // Deliberately far larger than oscillator error, still just a separate
    // authenticated coarse UTC mapping. It must never train frequency.
    varying_utc.offset_jitter=int64_t((second*17)%51)*1000000-25000000;
    const auto channel=uint8_t(second%4);varying_utc.feed(uint64_t(second)*1000000,channel,channel*10000,false);
    assert(varying_utc.feed(uint64_t(second)*1000000+62500,uint8_t((channel+1)%4),channel*10000));
  }
  assert(varying_utc.clock.frequency_trained()&&std::abs(varying_utc.clock.prediction().rate-2000)<10);
  assert(varying_utc.clock.causal_squares()==trained.clock.causal_squares());
  Fixture steady;
  for(unsigned second=1;second<90;second++){
    steady.anchor.at=uint64_t(second)*1000000;steady.anchor.until=steady.anchor.at+1500000;
    steady.feed(steady.anchor.at,4,0,false);assert(steady.feed(steady.anchor.at+62500,4));
  }
  const auto predicted=steady.clock.prediction();const auto at=predicted.tick;
  auto relative=steady.clock.quality(89062510);relative.evaluated20=at;
  assert(relative.samples==16&&relative.ready()&&relative.rms_ns<100&&relative.max_ns<100);
  assert(relative.locked(at,predicted)&&relative.estimate(at,predicted)<1000);
  assert(relative.estimate(at+200000000,predicted)>relative.estimate(at,predicted));
  assert(relative.hold_ms(at,predicted,2000000)<=2000);
  assert(relative.hold_ms(at,predicted,999)==0);
  assert(relative.estimate(at-1,predicted)==UINT32_MAX);
  // The historical working discipline steers toward its fitted epoch. A
  // noisy correction between fit points must not steer the delivered clock
  // toward that raw error; its complete causal tail must still withdraw lock.
  steady.feed(89100000,4,4000,false);assert(steady.feed(89162500,4));
  assert(std::abs(steady.clock.prediction().phase)<200);
  const auto noisy=steady.clock.quality(89162510);
  assert(noisy.max_ns>=3900&&!noisy.locked(steady.clock.prediction().tick,steady.clock.prediction()));
  // Prior-model causal error cannot subsume an independent fresh phase.
  // Retain both, every worst tail, and the unchanged1us quality limit.
  G1RelativeQuality accounted;accounted.original20=at;accounted.samples=16;
  accounted.rms_ns=250;accounted.max_ns=700;accounted.rate_guard_ppb=50;
  auto offset=predicted;offset.tick=at;offset.phase=300;
  assert(accounted.legacy_sum_estimate(at,offset)==1100);
  assert(accounted.estimate(at,offset)==1100&&!accounted.hold_ms(at,offset,5000000)&&!accounted.locked(at,offset));
  offset.phase=100;assert(accounted.estimate(at,offset)==900&&accounted.hold_ms(at,offset,5000000)==1980&&accounted.locked(at,offset));
  accounted.max_ns=1100;assert(accounted.estimate(at,offset)==1250&&!accounted.hold_ms(at,offset,5000000)&&!accounted.locked(at,offset));
  accounted.max_ns=700;offset.phase=1100;assert(accounted.estimate(at,offset)==1900&&!accounted.hold_ms(at,offset,5000000));
  offset.phase=300;assert(accounted.estimate(at+5*20000000,offset)==1050&&!accounted.hold_ms(at+5*20000000,offset,5000000));
  offset.phase=100;accounted.accel_guard_ppb_s=20;
  assert(accounted.estimate(at,offset)==900&&accounted.hold_ms(at,offset,5000000)<1600);
  assert(accounted.estimate(at+5*20000000,offset)==1300&&!accounted.hold_ms(at+5*20000000,offset,5000000));
  accounted.accel_guard_ppb_s=INFINITY;assert(!accounted.ready()&&!accounted.hold_ms(at,offset,5000000));
  accounted.accel_guard_ppb_s=0;
  assert(accounted.hold_ms(at,offset,999)==0);accounted.max_ns=UINT32_MAX;assert(!accounted.ready()&&accounted.estimate(at,offset)==UINT32_MAX);
  const auto old_predictions=steady.clock.predictions();
  steady.anchor.at=100000000;steady.anchor.until=101500000;
  steady.feed(100000000,4,20000,false);assert(steady.feed(100062500,4,20000));
  // A real held gap's unseen error is retained before it can train the model.
  assert(steady.clock.predictions()==old_predictions+1&&steady.clock.causal_max_ns()>19000);
  relative=steady.clock.quality(100062510);assert(relative.max_ns>19000&&!relative.locked(steady.clock.prediction().tick,steady.clock.prediction()));
  steady.clock.withdraw();assert(!steady.clock.quality(100062510).ready());
  //37-channel hopping must retain a fresh rate fit through base rebasing
  // and the quality-second rollover after18h of continuous service.
  for(const auto beginning:{UINT64_C(1000000),UINT64_C(65530000000)}){
    Fixture hopping;unsigned fresh=0;
    for(unsigned i=0;i<2400;i++){
      const auto now=beginning+uint64_t(i)*62500;
      hopping.anchor.at=now;hopping.anchor.until=now+1500000;
      hopping.feed(now,uint8_t((i*11+i/17)%37),unsigned((i*11+i/17)%37)*100);
      if(i>640){
        assert(hopping.clock.frequency_trained()&&hopping.clock.rate_pairs()>=8);
        assert(hopping.clock.fit_points()<=64&&hopping.clock.fit_reason()==0);
        assert(hopping.clock.prediction().tick-hopping.clock.fit_tick20()<10000000);
        ++fresh;
      }
    }
    assert(fresh>1700&&std::abs(hopping.clock.prediction().rate-2000)<75);
    const auto quality=hopping.clock.quality(beginning+UINT64_C(2399)*62500+10);
    const auto different_origin=hopping.clock.quality(beginning+UINT64_C(2399)*62500+17000010,hopping.clock.prediction().tick);
    assert(different_origin.samples==quality.samples&&different_origin.rms_ns==quality.rms_ns);
    assert(quality.samples>16&&quality.max_ns>1000&&!quality.locked(hopping.clock.prediction().tick,hopping.clock.prediction()));
    const auto scope_before=quality.scope_generation;
    const auto resumed=beginning+UINT64_C(2399)*62500+40187500;
    hopping.anchor.at=resumed;hopping.anchor.until=resumed+1500000;
    hopping.feed(resumed,4);hopping.feed(resumed+62500,5);
    assert(!hopping.clock.frequency_trained()&&hopping.clock.fit_points()<=1);
    assert(hopping.clock.quality(resumed+62510).scope_generation==scope_before);
  }
  // Both residual signs and near-envelope drift survive compact rebasing.
  for(const int direction:{-1,1}){
    Fixture envelope;envelope.physical_rate_ppb=direction*48000;
    for(unsigned i=0;i<1600;i++){
      const auto now=UINT64_C(40000000)+uint64_t(i)*62500;
      envelope.anchor.at=now;envelope.anchor.until=now+1500000;
      envelope.feed(now,uint8_t((i*7)%37));
    }
    assert(envelope.clock.frequency_trained()&&std::abs(envelope.clock.prediction().rate-direction*48000)<2);
  }
  G1RelativeQuality huge;huge.original20=1;huge.samples=16;huge.rms_ns=huge.max_ns=UINT32_MAX-1;huge.rate_guard_ppb=1;
  assert(huge.estimate(at,predicted)==UINT32_MAX&&!huge.locked(at,predicted));
  // G1 storage changes capacity only, never a strict capture prerequisite.
  assert(sizeof(G1StrictDiscipline)<sizeof(R2Discipline));
  G1StrictDiscipline eight;R2Discipline sixteen;
  eight.scope({1,11,10000000,1,1,1,0,true});sixteen.scope({1,11,10000000,1,1,1,0,true});
  for(unsigned i=1;i<=9;i++){
    r2_precision_packet packet{};packet.boot=11;packet.source_epoch=packet.train=packet.capture_epoch=1;packet.root=0;packet.publication=i;packet.flags=1;
    packet.created_ns=1000000000+i*1000;packet.expires_ns=packet.created_ns+1000000000;
    periodic_rx::Stamp stamp{};stamp.actual=stamp.event_valid=stamp.channel_valid=stamp.edge_calibrated=true;stamp.epoch=1;stamp.event=i;stamp.channel=4;stamp.map_enclosure_ns=100;
    stamp.tick20=20000000+i*20;stamp.available20=stamp.tick20+1;
    assert(!eight.receive(packet,stamp,stamp.available20,1000000,packet.created_ns));
    assert(!sixteen.receive(packet,stamp,stamp.available20,1000000,packet.created_ns));
  }
  assert(eight.view(20000200,1000000).overflow==1&&!eight.view(20000200,1000000).accepted);
  assert(!sixteen.view(20000200,1000000).overflow);
  R2Discipline strict;periodic_rx::Stamp c{};r2_relay_packet p{};
  assert(!strict.authenticated_gr3(p,c,1,1,1));assert(!strict.view(1,1).fit&&!strict.view(1,1).accepted);
  // The field failure retained a26s relative step through repeated JOINs.
  // Both directions retire the map; the failed pair never trains recovery.
  for(const auto step:{INT64_C(26000000000),-INT64_C(26000000000)}){
    Fixture jump;
    for(unsigned second=40;second<100;second++){
      jump.anchor.at=uint64_t(second)*1000000;jump.anchor.until=jump.anchor.at+1500000;
      jump.feed(jump.anchor.at,4,0,false);assert(jump.feed(jump.anchor.at+62500,4));
    }
    const auto generation=jump.clock.quality(99062510).scope_generation;
    jump.relative_step=step;jump.anchor.at=100000000;jump.anchor.until=101500000;
    jump.feed(100000000,4,0,false);assert(!jump.feed(100062500,4));
    assert(jump.clock.discontinuities()==1&&jump.clock.last_discontinuity_ns()>UINT64_C(25000000000));
    assert(!jump.clock.prediction().tick&&!jump.clock.frequency_trained()&&!jump.clock.quality(100062510).ready());
    assert(jump.clock.causal_max_ns()>UINT64_C(25000000000)&&jump.clock.causal_squares()==UINT64_MAX);
    assert(jump.clock.quality(100062510).scope_generation!=generation);
    assert(!jump.feed(100125000,4,0,false));assert(jump.feed(100187500,4));
    assert(jump.clock.prediction().tick&&std::abs(jump.clock.prediction().phase)<1000);
    assert(jump.clock.discontinuities()==1); // Failure count survives reacquisition.
  }
  // A new remote capture epoch cannot remain latched out forever, and its
  // retired epoch cannot repopulate pending tuples or disturb a fresh map.
  Fixture epoch;
  epoch.feed(1000000,4);assert(epoch.feed(1062500,4));
  epoch.remote_epoch=10;assert(!epoch.feed(1125000,4));assert(!epoch.clock.prediction().tick);
  assert(!epoch.feed(1187500,4,0,false));assert(epoch.feed(1250000,4));
  const auto new_map=epoch.clock.prediction();epoch.remote_epoch=9;assert(!epoch.feed(1312500,4));
  assert(epoch.clock.prediction().tick==new_map.tick);
  std::printf("PASS functional corrected scope/expiry/coarse hold, relative frequency2000ppb, fixed channel bias, independent strict refusal\n");
}
