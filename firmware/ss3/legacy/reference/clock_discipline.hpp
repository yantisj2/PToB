#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "hold_timer.hpp"
#include "clock_feedback.hpp"
#include "clock_tracker.hpp"

// Bounded regression on the physical counter and measured source UTC.
// Local counter samples are never pre-steered by the applied trim.
// Trim is UTC/local rate correction in ppb; RESID is measured minus applied.
class CardputerDiscipline {
public:
    enum Source : uint8_t { None, Ntp, Pts, GpsBle, NetworkBle, GpsLora };
    static constexpr uint64_t Hz=20000000;
    struct Quality {
        Source source=None; bool fit=false;
        int32_t trim_ppb=0,residual_ppb=0;
        int32_t trim_mppb=0,residual_mppb=0,measured_mppb=0;
        uint32_t rms_ns=UINT32_MAX,rms_us=UINT32_MAX,uncertainty_us=UINT32_MAX,span_s=0,age_s=UINT32_MAX;
        uint16_t samples=0,downweighted=0;
        uint32_t sample_bound_us=UINT32_MAX,phase_remaining_us=UINT32_MAX,holdover_us=UINT32_MAX;
        // A statistical stability estimate, never an absolute RF/UTC bound.
        uint32_t precision_ns=UINT32_MAX,rate_sigma_ppb=UINT32_MAX;
        // Pre-update one-step prediction scatter. For LoRa this is relative
        // stability only; radio/path and absolute UTC remain uncalibrated.
        uint32_t relative_prediction_ns=UINT32_MAX;
        uint32_t hold_1us_ms=0,hold_remaining_ms=0,hold_trials=0,hold_failures=0,hold_trial_ms=1,hold_error_ns=0;
        int32_t hold_raw_ppb=0;
        double hold_p95_ppb=0,hold_guard_ppb=0;
        uint32_t hold_tested_ms=0,hold_samples=0,hold_p99_samples=0;
        uint64_t hold_age_ticks=0;
        uint32_t hold_p95_ns=UINT32_MAX;
        bool feedback_selected=false;
        uint32_t feedback_samples=0,feedback_promotions=0,feedback_rollbacks=0;
        uint32_t feedback_base_p95=UINT32_MAX,feedback_p95=UINT32_MAX;
        uint32_t feedback_base_rms=UINT32_MAX,feedback_rms=UINT32_MAX;
        int32_t feedback_innovation_ns=0,feedback_rate_mppb=0;
        bool tracker_selected=false,tracker_curved=false;
        uint32_t tracker_samples=0,tracker_promotions=0,tracker_rollbacks=0;
        uint32_t tracker_base_rms=UINT32_MAX,tracker_rms=UINT32_MAX;
        uint32_t tracker_base_p95=UINT32_MAX,tracker_p95=UINT32_MAX;
        int32_t tracker_acceleration_mppb_s=0;

    };
    void reset() {
        next_=count_=0;anchor_=last_=fit_tick_=0;fit_epoch_=0;filter_error_ns_=0;downweighted_=0;epoch_=phase_=0;
        mapping_floor_ns_=0;hold_.reset();feedback_.reset();tracker_.reset();acceleration_=0;hold_reference_=delivery_=0;
        causal_next_=causal_count_=0;recovery_count_=0;recovery_tick_=0;
        bound_=session_=span_=0;rms_=UINT32_MAX;source_=None;fit_=false;trim_=measured_=slope_sigma_=0;
    }
    bool valid() const {return source_!=None;}
    bool fresh(uint64_t now) const {return valid() && now>=last_ && now-last_<=Hz*15;}
    Source source() const {return source_;}
    int64_t utc(uint64_t tick) const {return utc_ns(tick)/1000;}
    int64_t utc_ns(uint64_t tick) const {
        return valid()?prediction().at(tick):0;
    }
    ClockPrediction prediction() const {return {anchor_,epoch_,phase_,trim_,acceleration_};}
    // Called after a received batch, using actual processing time. Older D1
    // timestamps must never launch experiments in the already-observed past.
    void reference_available(uint64_t now) {
        hold_.age(now);
        if(!valid() || now<last_ || last_==hold_reference_)return;
        hold_reference_=last_;delivery_=now;
        const auto q=quality(now);
        hold_.start(now,prediction(),source_==GpsLora?q.relative_prediction_ns:q.precision_ns);
    }
    HoldTimer::Summary hold_summary(unsigned bucket) const {return hold_.summary(bucket);}
    uint32_t hold_samples(uint64_t now,unsigned percentile=95) const {return hold_.samples(now,percentile);}
    uint32_t hold_tested_budget(uint64_t now,uint64_t output_error=0,unsigned percentile=95) const {
        const auto q=quality(now);
        return hold_.tested_budget(now,source_==GpsLora?q.relative_prediction_ns:q.precision_ns,0,output_error,0,percentile);
    }
    uint32_t hold_budget(uint64_t now,uint64_t output_error=0,bool remaining=false,unsigned percentile=95) const {
        const auto q=quality(now);
        return hold_.budget(now,source_==GpsLora?q.relative_prediction_ns:q.precision_ns,
                            q.hold_guard_ppb,output_error,remaining?q.hold_age_ticks:0,percentile);
    }
    bool observe(uint64_t tick,int64_t utc_us,uint32_t uncertainty,Source source,
                 uint32_t session,bool prefer_ble=false,bool calibration=false) {
        if(utc_us<1704067200000000LL || utc_us>9000000000000000LL)return false;
        return observe_ns(tick,utc_us*1000,uncertainty,source,session,prefer_ble,calibration);
    }
    bool observe_ns(uint64_t tick,int64_t sample_ns,uint32_t uncertainty,Source source,
                    uint32_t session,bool prefer_ble=false,bool calibration=false,uint32_t tuning_noise_ns=0,bool source_local=false) {
        if(!tick || sample_ns<0 || (!source_local && sample_ns<1704067200000000000LL) || uncertainty>2000000 || source==None)return false;
        // During the radio-cycle experiment, PTS is a measured subject.
        // A GPS-trained master holds over through Wi-Fi windows; NTP cannot
        // silently replace that independent reference.
        if(calibration && (source==Pts || (source==Ntp && source_==GpsBle)))return false;
        if(valid() && tick<=last_)return false;
        const auto rank=[prefer_ble](Source s){
            return s==GpsBle?(prefer_ble?4:3):
                s==GpsLora?2:
                s==NetworkBle?(prefer_ble?3:2):
                s==Pts?(prefer_ble?2:4):1;
        };
        if(fresh(tick) && rank(source)<rank(source_))return false;
        const bool had_clock=valid();
        const int64_t previous_ns=had_clock?utc_ns(tick):sample_ns;
        const double previous_rate=trim_+(had_clock?acceleration_*std::min(10.0,double(tick-anchor_)/Hz):0);
        if(fresh(tick) && source==source_ && session==session_ &&
           std::llabs(sample_ns-previous_ns)>std::max<int64_t>(250000000,4000LL*uncertainty))return false;
        if(source!=source_ || session!=session_) {recovery_count_=0;count_=next_=0;fit_=false;span_=0;rms_=UINT32_MAX;measured_=slope_sigma_=0;downweighted_=0;mapping_floor_ns_=0;causal_next_=causal_count_=0;hold_.reset();feedback_.reset();tracker_.reset();hold_reference_=delivery_=0;}
        if(last_ && tick-last_>Hz*15){hold_.reset();feedback_.reset();tracker_.reset();hold_reference_=delivery_=0;}
        // Quarantine disturbed BLE timestamps before they can alter the fit
        // or actuator. A coherent new phase can rebuild the fit, while UTC
        // continues on its last applied rate until that fit is established.
        if(fit_ && source==GpsBle){
            const double elapsed=double(tick-last_)/Hz;
            const double gate=std::max(5000.0,8.0*rms_+elapsed*
                (3*slope_sigma_+std::abs(measured_-trim_)));
            const auto dt=tick-fit_tick_;
            const int64_t fitted=fit_epoch_+int64_t(dt)*50+
                int64_t(std::llround(double(dt)/Hz*measured_));
            if(double(std::llabs(sample_ns-fitted))>gate){
                if(recovery_count_ && tick>recovery_tick_ && tick-recovery_tick_<=Hz*5 &&
                   std::llabs((sample_ns-recovery_ns_)-int64_t(tick-recovery_tick_)*50-
                     int64_t(std::llround(double(tick-recovery_tick_)/Hz*trim_)))<=5000)
                    ++recovery_count_;
                else recovery_count_=1;
                recovery_tick_=tick;recovery_ns_=sample_ns;
                if(recovery_count_<3)return false;
                count_=next_=0;fit_=false;span_=0;rms_=UINT32_MAX;
                hold_.reset();feedback_.reset();tracker_.reset();hold_reference_=delivery_=0;
                recovery_count_=0;
            }else recovery_count_=0;
        }
        hold_.score(tick,sample_ns);
        if(source==GpsBle){
            const int64_t dt=int64_t(tick)-int64_t(fit_tick_);
            const int64_t baseline=feedback_.selected()?feedback_.predict(tick):
                fit_epoch_+dt*50+int64_t(std::llround(double(dt)/Hz*measured_));
            tracker_.observe(tick,sample_ns,baseline,fit_);
        }
        // Validate the *previous* fit against this unseen observation. The
        // fixed NSS-to-GPTimer mapping bias cancels in these rate predictions.
        if(source==GpsLora && fit_ && tuning_noise_ns && tuning_noise_ns<=5000 &&
           tick>=last_ && tick-last_<=Hz*3) {
            const int64_t dt=int64_t(tick)-int64_t(fit_tick_);
            const int64_t prediction=feedback_.selected()?feedback_.predict(tick):fit_epoch_+dt*50+
                int64_t(std::llround(double(dt)/Hz*measured_));
            const uint64_t error=uint64_t(std::llabs(sample_ns-prediction));
            causal_[causal_next_]=uint32_t(std::min<uint64_t>(UINT32_MAX,error));
            causal_next_=(causal_next_+1)%16;
            if(causal_count_<16)++causal_count_;
        }
        if(fit_ && (source==GpsBle || source==GpsLora || source==Pts)){
            const auto dt=tick-fit_tick_;
            const int64_t baseline=fit_epoch_+int64_t(dt)*50+
                int64_t(std::llround(double(dt)/Hz*measured_));
            feedback_.observe(tick,sample_ns,baseline,measured_,tuning_noise_ns);
        }
        const uint64_t previous_fit_tick=fit_tick_;
        source_=source;session_=session;last_=tick;bound_=uncertainty;
        if(source==GpsLora || source==Pts)mapping_floor_ns_=tuning_noise_ns;
        // Decimate to about one per second without losing alternate GPS seconds
        // when the local oscillator runs slightly fast or slow.
        if(!count_ || tick-points_[(next_+63)%64].tick>=Hz*9/10) {
            points_[next_]={tick,sample_ns,tuning_noise_ns};next_=(next_+1)%64;if(count_<64)++count_;
            fit();
        }
        int64_t target=had_clock && !fit_?previous_ns:sample_ns;
        // A radio receive observation can jitter by much more than one timer
        // tick. Once the rate fit is established, steer phase toward its
        // fitted epoch instead of chasing the latest receive error. Keep the
        // raw observation in the fit so rate trim and RESID still respond.
        if(fit_ && tuning_noise_ns && (source==GpsBle || source==GpsLora || source==Pts))
            target=fit_epoch_+int64_t(tick-fit_tick_)*50+
                int64_t(std::llround(double(tick-fit_tick_)/Hz*measured_));
        if(feedback_.selected())target=feedback_.predict(tick);
        trim_=previous_rate;acceleration_=0;
        if(fit_ && fit_tick_!=previous_fit_tick)
            trim_+=((feedback_.selected()?feedback_.rate():measured_)-trim_)*0.25;
        if(source==GpsBle && tracker_.selected()){
            const auto& m=tracker_.model();target=m.at(tick);
            trim_+=(m.rate-trim_)*0.25;acceleration_=m.acceleration;
        }
        filter_error_ns_=uint64_t(std::llabs(target-sample_ns));
        anchor_=tick;epoch_=previous_ns;phase_=target-previous_ns;
        return true;
    }
    Quality quality(uint64_t now) const {
        const double applied=trim_+(now>=anchor_?acceleration_*std::min(10.0,double(now-anchor_)/Hz):0);
        Quality q; q.source=source_;q.fit=fit_;q.trim_ppb=int32_t(std::llround(applied));
        q.downweighted=downweighted_;
        q.residual_ppb=fit_?int32_t(std::llround(measured_-applied)):0;
        if(fit_) {
            q.trim_mppb=int32_t(std::llround(applied*1000));
            q.measured_mppb=int32_t(std::llround(measured_*1000));
            q.residual_mppb=q.measured_mppb-q.trim_mppb;
        }
        q.rms_ns=rms_;q.rms_us=rms_==UINT32_MAX?UINT32_MAX:(uint64_t(rms_)+999)/1000;q.span_s=span_;q.samples=count_;
        if(valid() && now>=last_)q.age_s=uint32_t(std::min<uint64_t>(UINT32_MAX,(now-last_)/Hz));
        if(valid() && now>=last_) {
            const uint64_t age_ticks=now-last_;
            // Only the unfinished portion of the phase slew is still error.
            // 500 us/s = one nanosecond per 40 raw ticks.
            const uint64_t remaining=uint64_t(std::llabs(phase_))>age_ticks/40
                ?uint64_t(std::llabs(phase_))-age_ticks/40:0;
            const auto sat=[](uint64_t v){return uint32_t(std::min<uint64_t>(UINT32_MAX,v));};
            q.sample_bound_us=bound_;q.phase_remaining_us=sat((remaining+999)/1000);
            // Retain the unqualified oscillator guard for absolute UTC claims.
            q.holdover_us=sat(uint64_t(std::ceil(double(age_ticks)/Hz*
                std::max(50000.0,std::abs(measured_-trim_))/1000)));
            q.uncertainty_us=sat(uint64_t(bound_)+(filter_error_ns_+999)/1000+q.phase_remaining_us+q.holdover_us);
            // Profile 1 has no calibrated radio/path or UTC bound. A finite
            // internal fit weight must never be presented as absolute accuracy.
            if(source_==GpsLora || source_==Pts){q.sample_bound_us=UINT32_MAX;q.uncertainty_us=UINT32_MAX;}
            if(fit_ && age_ticks<=Hz*3600) {
                q.rate_sigma_ppb=sat(uint64_t(std::ceil(slope_sigma_)));
                // 3-RMS scatter plus the measured rate estimation error and
                // unapplied trim over holdover. Fixed receiver bias is excluded.
                const double prediction_ns=double(age_ticks)/Hz*
                    (3*slope_sigma_+std::abs(measured_-applied)+10*std::abs(acceleration_));
                q.precision_ns=sat(uint64_t(3)*rms_+remaining+
                    uint64_t(std::ceil(prediction_ns)));
                if(source_==GpsLora || source_==Pts)q.precision_ns=std::max(q.precision_ns,mapping_floor_ns_);
                if(source_==GpsLora && causal_count_==16) {
                    double square=0;
                    for(uint32_t error:causal_)square+=double(error)*error;
                    const uint64_t causal=uint64_t(std::ceil(3*std::sqrt(square/16)));
                    // This predicts a future local phase difference. It is
                    // intentionally separate from q.precision_ns, whose
                    // absolute mapping floor remains in force.
                    q.relative_prediction_ns=sat(causal+remaining+
                        uint64_t(std::ceil(prediction_ns))+50);
                }
            }
        }
        q.hold_guard_ppb=3*slope_sigma_+std::abs(measured_-applied)+10*std::abs(acceleration_);
        q.hold_samples=hold_.samples(now,95);q.hold_p99_samples=hold_.samples(now,99);
        q.hold_age_ticks=delivery_ && now>=delivery_?now-delivery_:UINT64_MAX;
        const auto hold_precision=source_==GpsLora?q.relative_prediction_ns:q.precision_ns;
        q.hold_1us_ms=hold_.budget(now,hold_precision,q.hold_guard_ppb);
        q.hold_remaining_ms=hold_.budget(now,hold_precision,q.hold_guard_ppb,0,q.hold_age_ticks);
        q.hold_tested_ms=hold_.tested_budget(now,hold_precision,0);
        for(unsigned i=0;i<HoldTimer::Buckets;++i)if(HoldTimer::Horizons[i]==q.hold_tested_ms)
            q.hold_p95_ns=hold_.summary(i).p95_ns;
        q.hold_trials=hold_.experiments();q.hold_failures=hold_.failures();
        q.hold_trial_ms=HoldTimer::Horizons[0];q.hold_error_ns=hold_.last_error_ns();
        q.feedback_selected=feedback_.selected();q.feedback_samples=feedback_.observations();
        q.feedback_promotions=feedback_.promotions();q.feedback_rollbacks=feedback_.rollbacks();
        q.feedback_base_p95=feedback_.baseline_p95();q.feedback_p95=feedback_.candidate_p95();
        q.feedback_base_rms=feedback_.baseline_rms();q.feedback_rms=feedback_.candidate_rms();
        q.feedback_innovation_ns=feedback_.innovation_ns();
        q.feedback_rate_mppb=int32_t(std::llround(feedback_.rate()*1000));
        q.tracker_selected=tracker_.selected();q.tracker_curved=tracker_.model().curved;
        q.tracker_samples=tracker_.observations();q.tracker_promotions=tracker_.promotions();q.tracker_rollbacks=tracker_.rollbacks();
        q.tracker_base_rms=tracker_.baseline_rms();q.tracker_rms=tracker_.candidate_rms();
        q.tracker_base_p95=tracker_.baseline_p95();q.tracker_p95=tracker_.candidate_p95();
        q.tracker_acceleration_mppb_s=int32_t(std::llround(acceleration_*1000));
        return q;
    }
private:
    HoldTimer hold_{};
    uint64_t hold_reference_=0,delivery_=0;
    ClockFeedback feedback_{};
    ClockTracker tracker_{};
    double acceleration_=0;
    struct Point {uint64_t tick;int64_t utc;uint32_t noise;};
    Point points_[64]{};uint8_t next_=0,count_=0;
    uint32_t causal_[16]{};uint8_t causal_next_=0,causal_count_=0;
    uint64_t anchor_=0,last_=0,fit_tick_=0,filter_error_ns_=0;int64_t epoch_=0,phase_=0,fit_epoch_=0;uint16_t downweighted_=0;
    uint32_t bound_=0,session_=0,span_=0,rms_=UINT32_MAX,mapping_floor_ns_=0;
    uint64_t recovery_tick_=0;int64_t recovery_ns_=0;unsigned recovery_count_=0;
    Source source_=None;bool fit_=false;double trim_=0,measured_=0,slope_sigma_=0;
    void fit() {
        const auto& oldest=points_[count_==64?next_:0];
        const uint32_t history_span=uint32_t((last_-oldest.tick)/Hz);
        span_=history_span;
        if(count_<16 || history_span<30)return;
        // A radio-latched event can support a shorter rate fit that follows
        // crystal temperature drift. Keep the 30 s acquisition gate and the
        // 64-sample fit for software poll and BLE observations.
        const unsigned fit_count=source_==GpsLora && mapping_floor_ns_<=5000?
            std::min<unsigned>(16,count_):count_;
        const unsigned start=(next_+64-fit_count)%64;
        const auto& base=points_[start];
        const auto point_at=[&](unsigned i)->const Point&{
            return points_[(start+i)%64];
        };
        span_=uint32_t((last_-base.tick)/Hz);
        uint32_t noise_floor=UINT32_MAX;
        for(unsigned i=0;i<fit_count;++i)if(point_at(i).noise)
            noise_floor=std::min(noise_floor,point_at(i).noise);
        const auto weight=[&](const Point& p){
            // Robust clipping alone is not uncertainty weighting: otherwise
            // a 15 ms poll midpoint can outweigh a 2 us radio timestamp.
            const double relative=p.noise && noise_floor!=UINT32_MAX?
                std::min(1.0,double(noise_floor)/p.noise):1.0;
            const double variance_weight=relative*relative;
            if(!fit_ || !p.noise)return variance_weight;
            const int64_t dt=int64_t(p.tick)-int64_t(fit_tick_);
            const double error=double(p.utc-fit_epoch_)-double(dt)*50-double(dt)/Hz*measured_;
            return std::max(0.000001,variance_weight*
                std::min(1.0,4.0*p.noise/std::max(1.0,std::abs(error))));
        };
        double sx=0,sy=0,sw=0;downweighted_=0;
        for(unsigned i=0;i<fit_count;++i){const auto& p=point_at(i);const double w=weight(p);
            if(w<1)++downweighted_;
            sw+=w;sx+=w*double(p.tick-base.tick)/Hz;sy+=w*(double(p.utc-base.utc)/1000-double(p.tick-base.tick)/20);}
        const double mx=sx/sw,my=sy/sw;double xx=0,xy=0;
        for(unsigned i=0;i<fit_count;++i){const auto& p=point_at(i);double x=double(p.tick-base.tick)/Hz-mx;double y=double(p.utc-base.utc)/1000-double(p.tick-base.tick)/20-my;const double w=weight(p);xx+=w*x*x;xy+=w*x*y;}
        if(xx<=0)return;
        const double slope=xy/xx;double ss=0,weighted_ss=0;
        for(unsigned i=0;i<fit_count;++i){const auto& p=point_at(i);double x=double(p.tick-base.tick)/Hz;double y=double(p.utc-base.utc)/1000-double(p.tick-base.tick)/20;double e=y-my-slope*(x-mx);ss+=e*e;weighted_ss+=weight(p)*e*e;}
        const double candidate_rate=slope*1000;
        if(!std::isfinite(candidate_rate) || std::abs(candidate_rate)>200000)return;
        measured_=candidate_rate;
        rms_=uint32_t(std::ceil(std::sqrt(ss/fit_count)*1000));
        slope_sigma_=std::sqrt(weighted_ss/(fit_count-2)/xx)*1000;
        const double elapsed=double(last_-base.tick)/Hz;
        fit_tick_=last_;fit_epoch_=base.utc+int64_t(last_-base.tick)*50+
            int64_t(std::llround((my+slope*(elapsed-mx))*1000));fit_=true;

    }
};

// A recovered PTS service must provide three consecutive same-session
// observations before replacing NTP or changing PTS session. This prevents
// one intermittent Wi-Fi beacon from toggling the selected source or fit.
class CardputerPtsRecovery {
public:
    bool allow(uint64_t tick,uint32_t session,CardputerDiscipline::Source selected) {
        const bool qualifying=selected==CardputerDiscipline::Ntp ||
            (selected==CardputerDiscipline::Pts &&
             (recovering_ || (session_ && session!=session_)));
        if(!qualifying) {
            last_selected_=selected;session_=session;last_tick_=tick;
            count_=0;recovering_=false;return true;
        }
        if(last_selected_!=selected || session!=session_ ||
           tick<=last_tick_ || tick-last_tick_>CardputerDiscipline::Hz*4)
            count_=0;
        last_selected_=selected;session_=session;last_tick_=tick;
        recovering_=true;
        if(count_<3)++count_;
        if(count_==3){recovering_=false;return true;}
        return false;
    }
private:
    CardputerDiscipline::Source last_selected_=CardputerDiscipline::None;
    uint64_t last_tick_=0;
    uint32_t session_=0;
    uint8_t count_=0;
    bool recovering_=false;
};
