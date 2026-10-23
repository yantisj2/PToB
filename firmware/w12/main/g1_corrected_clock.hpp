#pragma once
#include "g1_client_policy.hpp"
#include "periodic_rx_model.hpp"
#include "g1_clock_prediction.hpp"
#include "g1_relative_quality.hpp"
#include "rc7_client.hpp"
#ifdef PTOB_N30
#include "n30_timing_probe.hpp"
#endif
extern "C" {
#include "r2/r2-g1/relay_codec.h"
}
namespace v12 {
// V14 functional leaf map. Exact captured publication + a later signed
// correction is distinct from the calibrated precision servo. The descriptor
// event/channel candidates are checked against the authenticated tuple; their
// calibration flags remain false; relative software frequency is separate
// from strict fit/precision/LOCK and never changes the hardware oscillator.
class G1CorrectedClock {
#ifdef V12_RC7_CLOCK_TEST_INSPECT
  friend struct Rc7PermissionInspect;
#endif
  struct Original {
    uint64_t local=0,created=0,expires=0,frame=0;int64_t offset=0;
    uint32_t publication=0,enclosure=UINT32_MAX,epoch=0;
    uint16_t event=0;uint8_t channel=255;bool used=false;
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    int64_t predicted=INT64_MIN,nominal=0; // INT64_MIN means no frozen direct capture.
#endif
  };
  // A valid correction is available after a nonzero captured tick. Zero
  // availability is the inactive marker; no extra padded bool allocation.
  struct Correction {r2_relay_correction value{};uint64_t available=0,expires=0;};
  static_assert(sizeof(Correction)==40,"Correction slot allocation changed");
  // At16Hz the250ms association horizon contains at most five originals and
  // two correction slots per frame. Eight bounded entries cover this path
  // while preserving the existing total2KiB added-RAM budget.
  static constexpr unsigned Pending=8;
  Original originals_[Pending]{};Correction corrections_[Pending]{};
  // Mutually exclusive authenticated owners: directGR3 stores its remote
  // capture epoch here; admittedRC7 stores last_parent_ns - source.issued.
  // The latter is <=RC_DYNAMIC_SOURCE_NS(<2^31), lossless in uint32_t.
  // Every directGR3<->RC7/full-path/boot/source transition must withdraw(),
  // even when the root-only G1Anchor::same() happens to be true. The native
  // JOIN/admission owner fences are part of this adapter's integration seam.
  G1Anchor scope_{};uint32_t remote_epoch_=0,local_epoch_=0;
  uint64_t until_=0,last_original_=0,last_remote_ns_=0;
  // RC7 consumed tuple and mapped original have separate lifetimes. These are
  // the only16 added bytes; full clock allocation remains<=1864B.
  uint64_t mapped_original20_=0,mapped_hold_until_us_=0;
  uint32_t accepted_=0,refused_=0;
  uint32_t quality_generation_=1;
  uint32_t coarse_enclosure_ns_=UINT32_MAX;
  // Keep64 physical points:62.5ms acquisition spacing gets repeated-channel
  // evidence promptly; after the first fit,250ms spacing grows toward16s. Offset encoding is lossless: local
  // ticks stay exact and a signed24-bit residual stores remote_ns-local_ns.
  // The32s expiry and full64-bit base/scope precede every conversion.
  struct Point {uint32_t local=0;uint8_t residual[3]{},channel=255;};
  static_assert(sizeof(Point)==8,"Compact physical history changed");
  struct Bucket {uint64_t squares=0;uint16_t second=0,count=0,peak=0;uint8_t accel=0,flags=0;};
  static_assert(sizeof(Bucket)==16,"Physical-time quality allocation changed");
  Point points_[64]{};Bucket quality_[16]{};
  unsigned count_=0;uint64_t point_at_=0,base_local_=0,base_remote_=0,quality_second_=0;
  uint8_t fit_reason_=0;
  static int32_t residual(const Point& p){
    uint32_t v=uint32_t(p.residual[0])|(uint32_t(p.residual[1])<<8)|(uint32_t(p.residual[2])<<16);
    if(v&0x800000)v|=0xff000000;
    return int32_t(v);
  }
  static void residual(Point& p,int32_t v){for(unsigned i=0;i<3;i++)p.residual[i]=uint8_t(uint32_t(v)>>(8*i));}
  uint64_t local(const Point& p)const{return base_local_+p.local;}
  uint64_t remote(const Point& p)const{return uint64_t(int64_t(base_remote_)+int64_t(p.local)*50+residual(p));}
  void rebase(){
    if(!count_){base_local_=base_remote_=0;return;}
    const uint32_t shift=points_[0].local;const int32_t offset=residual(points_[0]);
    const auto next_remote=remote(points_[0]);
    for(unsigned i=0;i<count_;i++){
      const int32_t next=residual(points_[i])-offset;
      if(next<-0x800000||next>0x7fffff){count_=0;base_local_=base_remote_=0;frequency_trained_=false;fit_reason_=5;return;}
    }
    for(unsigned i=0;i<count_;i++){points_[i].local-=shift;residual(points_[i],residual(points_[i])-offset);}
    base_local_+=shift;base_remote_=next_remote;
  }
  void quality_add(uint64_t tick,uint32_t error,bool causal){
    const auto second=tick/20000000;
    if(quality_second_&&(second<quality_second_||second-quality_second_>=16))for(auto& b:quality_)b={};
    quality_second_=second;auto& b=quality_[second%16];
    if((!b.count&&!b.flags)||b.second!=uint16_t(second)){b={};b.second=uint16_t(second);}
    if(!causal){inc(quality_missing_);return;} // Excluded and counted, never zero-filled.
    const uint64_t square=uint64_t(error)*error;
    if(b.count==UINT16_MAX||b.squares==UINT64_MAX||square>UINT64_MAX-b.squares){b.squares=UINT64_MAX;b.flags|=1;}
    else b.squares+=square;
    if(b.count!=UINT16_MAX)++b.count;
    b.peak=uint16_t(std::max<uint32_t>(b.peak,std::min<uint32_t>(error,UINT16_MAX)));
  }
  G1ClockPrediction clock_{};bool frequency_trained_=false;uint32_t rate_pairs_=0;
  uint64_t phase_fit_tick_=0;int64_t phase_fit_epoch_=0;
  uint32_t scatter_ns_=UINT32_MAX;
  double rate_guard_=0;
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
  uint64_t acceleration_at_=0;double acceleration_rate_=0;bool acceleration_ready_=false;
#endif
  uint32_t predictions_=0;uint64_t causal_squares_=0,nominal_squares_=0,causal_max_=0;
  uint32_t quality_missing_=0;
  uint32_t discontinuities_=0;uint64_t last_discontinuity_ns_=0;
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
  bool refuse(){inc(refused_);return false;}
  static bool stale(uint64_t now,uint64_t before){return now<before||now-before>uint64_t(periodic_rx::MaxAgeUs)*20;}
  void pending_clear(){for(auto& o:originals_)o={};for(auto& c:corrections_)c={};}
  bool discontinuity(const Original& o,uint64_t remote){
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    const auto predicted=o.predicted!=INT64_MIN?o.predicted:clock_.relative_at(o.local);
#else
    if(!clock_.tick||o.local<clock_.tick)return false;
    const auto predicted=clock_.relative_at(o.local);
#endif
    if(predicted<=0)return false;
    const auto error=uint64_t(std::llabs(int64_t(remote)-predicted));
    // Gross continuity screen only:1ms allowance plus the entire existing
    // +/-50ppm rate envelope over the actual held interval. It never grants
    // precision. A seconds-scale step must not enter phase steering.
    const auto elapsed=o.local>=clock_.tick?o.local-clock_.tick:0;
    const auto limit=UINT64_C(1000000)+elapsed/400+o.enclosure;
    if(error<=limit)return false;
    inc(predictions_);causal_max_=std::max(causal_max_,error);
    auto nominal=clock_;nominal.rate=0;
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    const auto baseline=uint64_t(std::llabs(int64_t(remote)-(o.predicted!=INT64_MIN?o.nominal:nominal.relative_at(o.local))));
#else
    const auto baseline=uint64_t(std::llabs(int64_t(remote)-nominal.relative_at(o.local)));
#endif
    if(error>UINT32_MAX||baseline>UINT32_MAX||error*error>UINT64_MAX-causal_squares_||baseline*baseline>UINT64_MAX-nominal_squares_)
      causal_squares_=nominal_squares_=UINT64_MAX;
    else {causal_squares_+=error*error;nominal_squares_+=baseline*baseline;}
    inc(discontinuities_);last_discontinuity_ns_=error;
    const auto saved=scope_;withdraw();scope_=saved;
    return true;
  }
  void observe_clock(const Original& o,uint64_t remote,uint64_t available){
    // Score the frozen delivered map before this unseen physical correction
    // can train it. Include signed phase/channel residuals; no detrending.
    uint32_t causal=UINT32_MAX;bool causal_available=false;
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    auto nominal=clock_;nominal.rate=0;
    const auto predicted=o.predicted!=INT64_MIN?o.predicted:clock_.relative_at(o.local);
    const auto raw=o.predicted!=INT64_MIN?o.nominal:nominal.relative_at(o.local);
    if(predicted>0&&raw>0){
#else
    if(clock_.tick&&o.local>=clock_.tick){
      const auto predicted=clock_.relative_at(o.local);auto nominal=clock_;nominal.rate=0;
      const auto raw=nominal.relative_at(o.local);
#endif
      if(predicted>0&&raw>0){
        causal_available=true;
        const uint64_t error=uint64_t(std::llabs(int64_t(remote)-predicted));
        const uint64_t baseline=uint64_t(std::llabs(int64_t(remote)-raw));
        const bool overflow=error>UINT32_MAX||baseline>UINT32_MAX||
          error*error>UINT64_MAX-causal_squares_||baseline*baseline>UINT64_MAX-nominal_squares_;
        if(overflow){causal_squares_=nominal_squares_=UINT64_MAX;}
        else {causal_squares_+=error*error;nominal_squares_+=baseline*baseline;}
        if(error>causal_max_)causal_max_=error;
        causal=uint32_t(std::min<uint64_t>(UINT32_MAX,error));
        inc(predictions_);
      }
    }
    // All pre-update errors enter physical one-second buckets, independently
    // of channel/time selection for fitting. Full cumulative tails stay above.
#ifdef PTOB_N30
    if(n30_probe::pair_sink)n30_probe::pair_sink({scope_.root,scope_.boot,o.local,available,remote,
      predicted,scope_.source_epoch,scope_.publication,o.epoch,o.publication,o.event,o.channel,causal_available,o.created,o.expires});
#endif
    quality_add(o.local,causal,causal_available);
    double rate=clock_.rate;bool fitted=false;
    // Fine receivers must track oscillator warm-up, not average its rate over sixteen
    // seconds after the first fit. Keep the same bounded64-point allocation,
    // physical/channel checks and causal quality history; retain every16Hz
    // opportunity instead of discarding three of four after acquisition.
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    constexpr uint64_t spacing=1250000;
#else
    const uint64_t spacing=frequency_trained_?5000000u:1250000u;
#endif
    if(!point_at_||o.local-point_at_>=spacing){
      while(count_&&(o.local<local(points_[0])||o.local-local(points_[0])>640000000)){
        for(unsigned i=1;i<count_;i++)points_[i-1]=points_[i];
        --count_;
      }
      if(count_==64){for(unsigned i=1;i<count_;i++)points_[i-1]=points_[i];--count_;}
      rebase();
      if(!count_){base_local_=o.local;base_remote_=remote;}
      const uint64_t delta=o.local-base_local_;
      const int64_t innovation=int64_t(remote)-int64_t(base_remote_)-int64_t(delta)*50;
      // A compact-history overflow is explicit untrained evidence, never a
      // truncated timestamp, selected outlier or larger accepted rate.
      if(delta>UINT32_MAX||innovation<-0x800000||innovation>0x7fffff){
        count_=0;base_local_=base_remote_=0;frequency_trained_=false;fit_reason_=5;
      }else{
        auto& point=points_[count_++];point.local=uint32_t(delta);point.channel=o.channel;residual(point,int32_t(innovation));point_at_=o.local;
        // Start only after2s and eight repeated-channel degrees of freedom.
        // The same64-point history keeps growing to its normal16s span;
        // early rate uncertainty and every delivered error still gate lock.
        fit_reason_=count_<8?1:o.local-local(points_[0])<40000000?2:3;
        if(count_>=8&&o.local-local(points_[0])>=40000000){
          double sx[37]{},sy[37]{};unsigned n[37]{};
          for(unsigned i=0;i<count_;i++){const auto& p=points_[i];const auto ch=p.channel;
            const double x=double(p.local)/20000000,y=double(residual(p));sx[ch]+=x;sy[ch]+=y;++n[ch];
          }
          double xx=0,xy=0;unsigned pairs=0;
          for(unsigned ch=0;ch<37;ch++)if(n[ch]>1)pairs+=n[ch]-1;
          rate_pairs_=pairs;
          for(unsigned i=0;i<count_;i++){const auto& p=points_[i];const auto ch=p.channel;if(n[ch]<2)continue;
            const double x=double(p.local)/20000000-sx[ch]/n[ch],y=double(residual(p))-sy[ch]/n[ch];xx+=x*x;xy+=x*y;
          }
          if(pairs>=8&&xx>0){const double candidate=xy/xx;
            fit_reason_=4;
            if(std::isfinite(candidate)&&std::abs(candidate)<=50000){
              double squares=0;
              for(unsigned i=0;i<count_;i++){const auto& p=points_[i];const auto ch=p.channel;if(n[ch]<2)continue;
                const double x=double(p.local)/20000000-sx[ch]/n[ch],y=double(residual(p))-sy[ch]/n[ch];
                const double e=y-candidate*x;squares+=e*e;
              }
              scatter_ns_=uint32_t(std::min<double>(UINT32_MAX,std::ceil(std::sqrt(squares/(pairs+1)))));
              const double span=double(o.local-local(points_[0]))/20000000;
              const double sigma=std::sqrt(squares/(pairs-1)/xx);
              rate_guard_=std::max(50/span,3*sigma+std::abs(candidate-clock_.rate));
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
              // Adjacent overlapping fits amplify channel/quantization noise
              // into an infinite acceleration guard. Use an observed >=1s
              // rate baseline; keep the same worst-bucket255/unknown fence.
              if(acceleration_at_&&o.local>=acceleration_at_&&o.local-acceleration_at_>=20000000){
                const double dt=double(o.local-acceleration_at_)/20000000;
                const double drift=std::ceil(std::abs(candidate-acceleration_rate_)/dt);
                auto& bucket=quality_[(o.local/20000000)%16];
                const uint8_t guard=std::isfinite(drift)&&drift<=254?uint8_t(drift):255;
                bucket.accel=std::max(bucket.accel,guard);
                acceleration_at_=o.local;acceleration_rate_=candidate;acceleration_ready_=true;
              }else if(!acceleration_at_){acceleration_at_=o.local;acceleration_rate_=candidate;}
#else
              if(frequency_trained_&&phase_fit_tick_&&o.local>phase_fit_tick_){
                const double dt=double(o.local-phase_fit_tick_)/20000000;
                const double drift=std::ceil(std::abs(candidate-clock_.rate)/dt);
                auto& bucket=quality_[(o.local/20000000)%16];
                const uint8_t guard=std::isfinite(drift)&&drift<=254?uint8_t(drift):255;
                bucket.accel=std::max(bucket.accel,guard);
              }
#endif
              rate=candidate;frequency_trained_=fitted=true;fit_reason_=0;
              double phase_sum=0;
              for(unsigned i=0;i<count_;i++){const auto& p=points_[i];const auto dt=o.local-local(p);
                phase_sum+=double(int64_t(this->remote(p))-int64_t(remote))+double(dt)*50+double(dt)/20000000*candidate;
              }
              phase_fit_tick_=o.local;phase_fit_epoch_=int64_t(remote)+int64_t(std::llround(phase_sum/count_));
            }
          }
        }
      }
    }
    const uint64_t age=available-o.local;
    int64_t target=int64_t(remote)+int64_t(age)*50+int64_t(std::llround(double(age)/20000000*rate));
    // A stale fit cannot suppress a newly measured phase after reception loss.
    // This2s freshness is separate from source/replay/output permission.
    if(phase_fit_tick_&&available>=phase_fit_tick_&&available-phase_fit_tick_<=40000000){
      const auto dt=available-phase_fit_tick_;
      target=phase_fit_epoch_+int64_t(dt)*50+int64_t(std::llround(double(dt)/20000000*rate));
    }
    const int64_t previous=clock_.tick&&available>=clock_.tick?clock_.relative_at(available):target;
    // Preserve the already delivered clock through every new sample. Large
    // acquisition phase uses bounded faster steering; fine hold retains1ppm.
    // Exactly the T096 radio_precision/precision_step domain split: the
    // oscillator fits correction.ticks; signed coarse UTC is only an offset.
    // NMEA/coarse UTC jitter cannot enter relative phase/frequency training.
    if(phase_fit_tick_&&available>=phase_fit_tick_&&available-phase_fit_tick_>40000000&&
       clock_.tick&&o.local>=clock_.tick&&o.local-clock_.tick<=40000000){
      target=previous+clock_.phase_remaining(available);
    }
    clock_={available,previous,target-previous,rate,o.offset};
    if(fitted)frequency_trained_=true;
  }
 public:
  void clear(){
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    acceleration_at_=0;acceleration_rate_=0;acceleration_ready_=false;
#endif
    coarse_enclosure_ns_=UINT32_MAX;mapped_original20_=mapped_hold_until_us_=0;inc(quality_generation_);pending_clear();scope_={};remote_epoch_=local_epoch_=0;until_=last_original_=last_remote_ns_=0;count_=0;point_at_=base_local_=base_remote_=quality_second_=0;for(auto& b:quality_)b={};fit_reason_=0;clock_={};phase_fit_tick_=0;phase_fit_epoch_=0;frequency_trained_=false;rate_pairs_=0;scatter_ns_=UINT32_MAX;rate_guard_=0;}
  void withdraw(){const double rate=clock_.rate;clear();clock_.rate=rate;}
  void scope(const G1Anchor& next){if(!scope_.same(next))withdraw();scope_=next;}
  bool same(const G1Anchor& next)const{return scope_.same(next);}
  void hold(){pending_clear();} // No permission/history renewal.
  uint32_t accepted()const{return accepted_;}uint32_t refused()const{return refused_;}
  uint64_t until()const{return until_;}
  uint64_t original20()const{return mapped_original20_?mapped_original20_:last_original_;}
  uint32_t coarse_enclosure_ns()const{return coarse_enclosure_ns_;}
  bool frequency_trained(uint64_t tick=0)const{if(!tick)tick=clock_.tick;return frequency_trained_&&phase_fit_tick_&&tick>=phase_fit_tick_&&tick-phase_fit_tick_<=40000000;}
  uint32_t rate_pairs()const{return rate_pairs_;}uint32_t scatter_ns()const{return scatter_ns_;}
  uint32_t predictions()const{return predictions_;}
  uint64_t causal_squares()const{return causal_squares_;}uint64_t nominal_squares()const{return nominal_squares_;}
  uint64_t causal_max_ns()const{return causal_max_;}
  uint32_t discontinuities()const{return discontinuities_;}
  uint64_t last_discontinuity_ns()const{return last_discontinuity_ns_;}
  uint64_t fit_tick20()const{return phase_fit_tick_;}
  uint32_t fit_points()const{return count_;}
  uint32_t fit_span_ms()const{return count_?uint32_t((local(points_[count_-1])-local(points_[0]))/20000):0;}
  uint8_t fit_reason()const{return fit_reason_;}
  uint32_t quality_missing()const{return quality_missing_;}
  G1ClockPrediction prediction()const{return clock_;}
  G1RelativeQuality quality(uint64_t now,uint64_t physical20=0)const{
    G1RelativeQuality q;q.original20=phase_fit_tick_;q.rate_guard_ppb=rate_guard_;
    q.scope_generation=quality_generation_==UINT32_MAX?0:quality_generation_;
    if(!scope_.valid(now)||now>UINT64_MAX/20)return q;
    if(!physical20)physical20=now*20; // Host fixtures; runtime passes its actual GPTimer read.
    const auto current=physical20/20000000;
    if(!quality_second_||current<quality_second_||current-quality_second_>=16)return q;
    uint64_t squares=0;uint32_t maximum=0;
    for(const auto& b:quality_){
      if(!b.count||uint16_t(current-b.second)>=16)continue;
      if(b.flags||b.squares==UINT64_MAX||b.squares>UINT64_MAX-squares){q.rms_ns=q.max_ns=UINT32_MAX;return q;}
      squares+=b.squares;maximum=std::max(maximum,uint32_t(b.peak));q.samples+=b.count;
      q.accel_guard_ppb_s=std::max(q.accel_guard_ppb_s,b.accel==255?INFINITY:double(b.accel));
    }
    if(q.samples){q.rms_ns=uint32_t(std::ceil(std::sqrt(double(squares)/q.samples)));q.max_ns=maximum;}
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    if(!acceleration_ready_)q.accel_guard_ppb_s=INFINITY;
#endif
    if(!frequency_trained(physical20))q.rate_guard_ppb=INFINITY;
    return q;
  }
  bool finalize(uint64_t completion){
    // Publish at actual completion, as historical W12 Oscillator::finalize.
    // A callback arriving during fitting cannot be scored against a model
    // that the receiver had not yet delivered at that original RF time.
    if(!clock_.tick||completion<clock_.tick){withdraw();return false;}
    const auto relative=clock_.relative_at(completion);if(relative<=0){withdraw();return false;}
    const int64_t slew=clock_.phase_slewed(completion);
    clock_={completion,relative,clock_.phase-slew,clock_.rate,clock_.utc_offset};return true;
  }
  bool covers(uint64_t now,uint64_t through)const{return scope_.valid(now)&&until_>through&&through>=now;}
  bool deliver(uint64_t tick,uint64_t now,uint64_t& utc_us)const {
    // Consumer sees clock validity only. Beyond original signed permission
    // this is bounded coarse HOLD, never a current followed-GM entitlement.
    if(!scope_.valid(now)||!clock_.tick||tick<clock_.tick)return false;
    const auto utc=clock_.at(tick);if(utc<=0)return false;utc_us=uint64_t(utc)/1000;return true;
  }
  // Caller authenticates O3/R3 and the direct root/boot/source/train first.
  // Expired output may still acquire using the independent live replay anchor,
  // exactly as T096 radio_service precision_step keeps acquisition context.
  bool receive(const r2_relay_packet& p,const periodic_rx::Stamp& capture,
               uint64_t available20,uint64_t now,const G1Anchor& anchor,rc5_origin* matched_origin=nullptr){
    scope(anchor);
    if(!anchor.valid(now)||!capture.actual||!capture.tick20||!capture.association_observed||
       capture.channel>36||!capture.epoch||!capture.sequence||capture.map_enclosure_ns==UINT32_MAX||
       capture.available20<capture.tick20||available20<capture.available20||stale(available20,capture.tick20)||
       p.origin.boot!=anchor.boot||p.origin.source_epoch!=anchor.source_epoch||p.route.train!=anchor.publication||
       p.route.depth||p.route.slot||p.route.transmitter!=p.origin.root||p.route.parent!=p.origin.root||
       p.route.boot!=p.origin.boot||p.route.map_generation!=p.route.train||!p.route.publication||!p.route.capture_epoch||
       p.origin.hold_horizon_us||p.origin.hold_budget_ns||p.count>2)return refuse();
    // Epochs increase inside one authenticated boot/source/train. Retire
    // old tuples once, then allow fresh pairs in the new epoch. Previously
    // the old value stayed latched and every new-epoch packet was refused.
    if(remote_epoch_&&p.route.capture_epoch<remote_epoch_)return refuse();
    if(remote_epoch_&&remote_epoch_!=p.route.capture_epoch){const auto saved=scope_;withdraw();scope_=saved;remote_epoch_=p.route.capture_epoch;return refuse();}
    remote_epoch_=p.route.capture_epoch;
    // A recreated PA capture epoch retires pending physical tuples, while the
    // already delivered map uses the same continuous20MHz clock through quiet.
    if(local_epoch_!=capture.epoch){pending_clear();local_epoch_=capture.epoch;}
    unsigned slot=Pending;
    for(unsigned i=0;i<Pending;i++){
      auto& o=originals_[i];
      if(o.used&&!stale(available20,o.local)&&o.publication==p.route.publication&&o.event==capture.event&&o.channel==capture.channel)return false;
      if(!o.used||stale(available20,o.local))slot=i;
    }
    if(slot==Pending){pending_clear();return refuse();}
    originals_[slot]={capture.tick20,p.origin.created_ns,p.origin.expires_ns,p.origin.frame_ns,p.origin.utc_offset_ns,
      p.route.publication,capture.map_enclosure_ns,capture.epoch,capture.event,capture.channel,true};
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
    // A later correction can arrive after another map has been published.
    // Score the original against the map actually available at its capture,
    // never against a map trained by intervening/newer physical evidence.
    originals_[slot].predicted=clock_.relative_at(capture.tick20);
    auto nominal=clock_;nominal.rate=0;
    originals_[slot].nominal=nominal.relative_at(capture.tick20);
#endif
    for(unsigned k=0;k<p.count;k++){
      const auto& value=p.correction[k];if(value.generation!=p.route.map_generation||value.channel>36)return refuse();
      slot=Pending;bool duplicate=false;
      for(unsigned i=0;i<Pending;i++){
        auto& c=corrections_[i];
        if(c.available&&!stale(available20,c.available)&&c.value.publication==value.publication&&c.value.event==value.event&&c.value.channel==value.channel){
          if(c.value.ticks!=value.ticks||c.value.generation!=value.generation){clear();return refuse();}
          duplicate=true;break;
        }
        if(!c.available||stale(available20,c.available))slot=i;
      }
      if(duplicate)continue;
      if(slot==Pending){pending_clear();return refuse();}
      corrections_[slot]={value,available20,p.origin.expires_ns};
    }
    Original* best=nullptr;Correction* correction=nullptr;
    for(auto& o:originals_)if(o.used&&!stale(available20,o.local)&&o.local>last_original_
#if !defined(PTOB_N30) && !defined(PTOB_W12_FINE_CLOCK)
      &&o.local>clock_.tick
#endif
    )
      for(auto& c:corrections_)if(c.available&&!stale(available20,c.available)&&o.publication==c.value.publication&&o.event==c.value.event&&o.channel==c.value.channel&&(!best||o.local<best->local)){best=&o;correction=&c;}
    if(!best)return false;
    correction->available=0;last_original_=best->local;
    const auto& c=correction->value;
    if(c.ticks>uint64_t(INT64_MAX)/125||!c.generation||correction->expires>best->expires)return refuse();
    const uint64_t raw=c.ticks*125/2;
    if(raw<best->created||raw>=correction->expires||best->offset<0||raw>uint64_t(INT64_MAX-best->offset))return refuse();
    const uint64_t remote=raw;
    const uint64_t elapsed=(available20-best->local)*50;
    const uint64_t permission=correction->expires-raw;
    const uint64_t guard=anchor.guard(now)*1000;
    if(permission<=elapsed||permission-elapsed<=best->enclosure||permission-elapsed-best->enclosure<=guard)return refuse();
    uint64_t remaining=(permission-elapsed-best->enclosure-guard)/1000;
    // Passive leaf age is strictly less than1.5s, clipped by signed source
    // permission (T096 radio_precision_read/output_read); no500ms relay rule.
    const uint64_t age=elapsed/1000;
    if(age>=1500000||!remaining||now>UINT64_MAX-remaining)return refuse();
    if(remaining>1500000-age)remaining=1500000-age;
    // Only an authenticated, matched physical pair with actual remaining
    // permission can retire a map. Preserve the full failed prediction and
    // require a new pair; no corrupt sample trains the replacement clock.
    if(discontinuity(*best,remote))return refuse();
    if(last_remote_ns_&&remote<=last_remote_ns_)return refuse();
    until_=now+remaining;
    last_remote_ns_=remote;
    observe_clock(*best,raw,available20);
    if(matched_origin)*matched_origin={anchor.root,anchor.boot,anchor.source_epoch,best->created,
      std::min(best->expires,correction->expires),best->offset,best->frame,0,0};
    coarse_enclosure_ns_=best->enclosure;
    inc(accepted_);return true;
  }
  enum class Rc7Kind:uint8_t {Refused,PermissionOnly,Mapped};
  struct Rc7Update {Rc7Kind kind=Rc7Kind::Refused;uint64_t tuple_original20=0,
    mapped_original20=0,map_tick20=0,mapped_hold_until_us=0,permission_until_us=0;
    uint32_t tuple_age_us=0;};
  Rc7Update receive_admitted_rc7(const Rc7Client& client,uint64_t available20,
      uint64_t now,const G1Anchor& anchor,const r2_group_config& group){
    auto denied=[&](){refuse();return Rc7Update{};};
    const auto& state=client.receiver.clock;const auto& path=client.receiver.path;
    const auto& r=state.reference;const auto& capture=client.completed;
    const auto& stamp=capture.stamp;
    if(!rc7_path_valid(&path)||!path.depth||
       client.request.type!=RC7_REQUEST||!state.bound||client.lease_until<=now||
       !r.valid||!anchor.valid(now)||!Rc7Client::captured(stamp,available20)||
       client.local_epoch!=stamp.epoch||!capture.publication||
       r.publication!=capture.publication||r.event!=stamp.event||r.channel!=stamp.channel||
       r.rx!=rc7_tick16(stamp.tick20)||r.completed_at!=rc7_tick16(available20)||
       r.original_available<rc7_tick16(stamp.available20)||r.original_available>r.completed_at||
       r.followup_rx>r.completed_at||r.parent_tx_ns!=state.last_parent_ns||
       rc7_tick16(available20)>=state.context_until)return denied();
    const auto& own=path.node[path.depth];const auto& root=path.node[0];
    const auto& admission=state.admission;
    if(own.id!=client.request.target||own.boot!=client.request.target_boot||
       client.receiver.self!=client.request.sender||root.id!=anchor.root||root.boot!=anchor.boot||path.source_epoch!=anchor.source_epoch||
       root.train!=anchor.publication||state.root_boot!=root.boot||state.root_epoch!=path.source_epoch||
       state.parent_boot!=own.boot||state.parent_train!=own.train||state.parent_epoch!=own.epoch||
       admission.root_boot!=root.boot||admission.root_epoch!=path.source_epoch||
       admission.parent_boot!=own.boot||admission.parent_train!=own.train||admission.parent_epoch!=own.epoch||
       !r.generation||r.origin.root!=root.id||r.origin.boot!=root.boot||
       r.origin.epoch!=path.source_epoch||r.origin.hold_us||r.origin.hold_ns||
       r.parent_tx_ns>uint64_t(INT64_MAX)||r.parent_tx_ns<r.origin.issued||
       r.parent_tx_ns>=r.origin.expires||
       r.origin.utc_offset>INT64_MAX-int64_t(r.parent_tx_ns))return denied();
    uint8_t proof[16];uint64_t lo=0,hi=0;
    if(rc7_origin_sign(proof,&r.origin,&group)||std::memcmp(proof,r.proof,16)||
       !rc5_coarse_interval(&state,rc7_tick16(available20),&lo,&hi)||hi>=r.origin.expires)return denied();
    if((clock_.tick&&!scope_.same(anchor))||stamp.tick20<=last_original_||
       (last_remote_ns_&&r.parent_tx_ns<=last_remote_ns_))return denied();
    // remote_epoch_ is used only by directGR3 in that mutually exclusive mode.
    // For admittedRC7 it stores the prior issued-to-TX age (<1.65e9ns), so an
    // older immutable source envelope cannot renew permission. Native full
    // path/mode changes still withdraw before bind, as required today.
    if(last_remote_ns_&&(remote_epoch_>last_remote_ns_||
       r.origin.issued<last_remote_ns_-remote_epoch_))return denied();
    const auto issued_age=r.parent_tx_ns-r.origin.issued;
    if(issued_age>UINT32_MAX)return denied();
    const auto elapsed=(available20-stamp.tick20)*50;
    const auto permission=r.origin.expires-r.parent_tx_ns;
    const auto guard=anchor.guard(now)*1000;
    const uint64_t age=uint64_t(r.age_us)+(elapsed+999)/1000;
    if(permission<=elapsed||permission-elapsed<=stamp.map_enclosure_ns||
       permission-elapsed-stamp.map_enclosure_ns<=guard||age>=1500000)return denied();
    const auto age_left=1500000-age;
    auto remaining=std::min<uint64_t>((permission-elapsed-stamp.map_enclosure_ns-guard)/1000,age_left);
    if(!remaining||now>UINT64_MAX-remaining||now>UINT64_MAX-age_left)return denied();
    const bool mapped=stamp.tick20>clock_.tick;
    if(!mapped){
      if(!client.utc_known||!clock_.tick||!mapped_original20_||mapped_hold_until_us_<=now)return denied();
      remaining=std::min<uint64_t>(remaining,mapped_hold_until_us_-now);
      if(!remaining)return denied();
    }
    if(mapped){
      Original original{stamp.tick20,r.origin.issued,r.origin.expires,r.origin.frame,r.origin.utc_offset,
        r.publication,stamp.map_enclosure_ns,stamp.epoch,r.event,r.channel,true};
      if(discontinuity(original,r.parent_tx_ns))return denied();
      scope(anchor);observe_clock(original,r.parent_tx_ns,available20);
      // Establish this once from that mapped original's actual elapsed RX
      // age plus its inherited age. Later PermissionOnly originals never
      // replace or extend this immutable map-hold deadline.
      mapped_original20_=stamp.tick20;mapped_hold_until_us_=now+age_left;inc(accepted_);
    }
    // PermissionOnly reaches no fitter/finalize/quality/forecast operation.
    // Consume this admitted tuple once, while original20() remains mapped RX.
    last_original_=stamp.tick20;last_remote_ns_=r.parent_tx_ns;
    remote_epoch_=uint32_t(issued_age);local_epoch_=stamp.epoch;until_=now+remaining;
    return {mapped?Rc7Kind::Mapped:Rc7Kind::PermissionOnly,stamp.tick20,mapped_original20_,
      clock_.tick,mapped_hold_until_us_,until_,uint32_t(age)};
  }
  // RC7 has already authenticated the complete admitted path and matched both
  // corrections to saved originals. Use their immutable envelopes and exact
  // W12 captures; no T096 phase calibration or normalized counter enters fit.
  bool receive_rc7(const rc5_reference& r,const periodic_rx::Stamp& s,
                   uint64_t available20,uint64_t now,const G1Anchor& anchor){
    scope(anchor);
    if(!r.valid||!anchor.valid(now)||available20<s.tick20||stale(available20,s.tick20)||
       s.tick20<=last_original_||s.tick20<=clock_.tick||r.parent_tx_ns>uint64_t(INT64_MAX)||
       r.origin.root!=anchor.root||r.origin.boot!=anchor.boot||r.origin.epoch!=anchor.source_epoch||
       r.origin.hold_us||r.origin.hold_ns||r.parent_tx_ns<r.origin.issued||
       r.parent_tx_ns>=r.origin.expires||r.origin.utc_offset>INT64_MAX-int64_t(r.parent_tx_ns))return refuse();
    const auto elapsed=(available20-s.tick20)*50;
    const auto permission=r.origin.expires-r.parent_tx_ns;
    const auto guard=anchor.guard(now)*1000;
    const uint64_t age=uint64_t(r.age_us)+(elapsed+999)/1000;
    if(permission<=elapsed||permission-elapsed<=s.map_enclosure_ns||
       permission-elapsed-s.map_enclosure_ns<=guard||age>=1500000)return refuse();
    const auto remaining=std::min<uint64_t>((permission-elapsed-s.map_enclosure_ns-guard)/1000,1500000-age);
    if(!remaining||now>UINT64_MAX-remaining)return refuse();
    Original o{s.tick20,r.origin.issued,r.origin.expires,r.origin.frame,r.origin.utc_offset,r.publication,
      s.map_enclosure_ns,s.epoch,r.event,r.channel,true};
    if(discontinuity(o,r.parent_tx_ns)|| (last_remote_ns_&&r.parent_tx_ns<=last_remote_ns_))return refuse();
    last_original_=s.tick20;last_remote_ns_=r.parent_tx_ns;until_=now+remaining;
    observe_clock(o,r.parent_tx_ns,available20);inc(accepted_);return true;
  }
};
// Eight immutable original frame values add64B for RC7 envelope forwarding.
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
static_assert(sizeof(G1CorrectedClock)<=2048,"Fine frozen capture predictions exceed2KiB clock budget");
#else
static_assert(sizeof(G1CorrectedClock)<=1864,"Functional clock exceeds its fixed RAM allocation");
#endif
}
