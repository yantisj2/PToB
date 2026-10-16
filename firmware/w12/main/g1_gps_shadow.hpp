#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Main-task witness only. No receiver/servo/controller calls, heap or storage.
class G1GpsShadow {
 public:
  enum Reason:uint8_t {Waiting=0,Ok,Unmapped,NoMap,Race,Evicted,Expired,Arithmetic,NoFix,Duplicate};
  struct Map {
    G1ClockPrediction clock{};
    uint64_t root=0,boot=0,lo=0,active=0,until=0,permission=0;
    uint32_t source=0,train=0,scope=0;bool valid=false,following=false;
  };
  struct Reference {uint64_t rmc20=0;uint32_t second=0,epoch=0;bool ready=false;};
  struct Record {
    uint64_t edge20=0;int32_t phase_ns=0;uint32_t age_us=0,number=0,session=0,enclosure_ns=UINT32_MAX;
    uint8_t reason=Waiting,reference=0,following=0,permission=0;
  };
  static_assert(sizeof(Record)==32,"Bounded PPS sample allocation");
  struct Score {Map map{};Record row{};int64_t relative_ns=0,utc_ns=0;bool map_valid=false;};
  struct View {
    uint32_t samples=0,edges=0,missing=0,invalid_reference=0,overwritten=0,session=0,duplicates=0;
    int32_t mean_ns=0;uint32_t rms_ns=UINT32_MAX,jitter_ns=UINT32_MAX,max_ns=UINT32_MAX;
    uint64_t last20=0,root=0,boot=0;uint32_t source=0,train=0,gps_epoch=0;uint8_t reason=Waiting;
  };
  static constexpr unsigned History=3,Tail=16;
 private:
  Map maps_[History]{};Record rows_[Tail]{};unsigned head_=0,count_=0,tail_=0,rows_count_=0;
  uint64_t observed_=0,replay_us_=0,permission_us_=0,last_edge_=0;
  View stats_{};int64_t sum_=0;double squares_=0;int32_t minimum_=0,maximum_=0;
  static void inc(uint32_t& v){if(v!=UINT32_MAX)++v;}
  static bool same_model(const Map& a,const Map& b){return a.clock.tick==b.clock.tick&&a.clock.epoch==b.clock.epoch&&
    a.clock.phase==b.clock.phase&&a.clock.rate==b.clock.rate&&a.clock.utc_offset==b.clock.utc_offset&&a.clock.output_correction_ns==b.clock.output_correction_ns&&
    a.root==b.root&&a.boot==b.boot&&a.source==b.source&&a.train==b.train&&a.scope==b.scope&&
    a.valid==b.valid&&a.following==b.following;}
  void remember(const Record& row){if(rows_count_==Tail)inc(stats_.overwritten);else ++rows_count_;rows_[tail_]=row;tail_=(tail_+1)%Tail;}
 public:
  const Map* latest()const{return count_?&maps_[(head_+History-1)%History]:nullptr;}
  void observe(Map map,uint64_t before,uint64_t seen,uint64_t replay_us,uint64_t permission_us){
    if(!before||seen<before)return;
    const auto prior=latest();const bool same=prior&&same_model(*prior,map);
    if(same&&replay_us==replay_us_&&permission_us==permission_us_){observed_=seen;return;}
    // Model computation is a lower availability bound; the completed read is
    // an upper bound. An edge within this interval is explicitly unscorable.
    map.lo=map.valid&&!same?map.clock.tick:(observed_?observed_:before);map.active=seen;
    if(map.lo>seen){map.valid=false;map.lo=before;}
    maps_[head_]=map;head_=(head_+1)%History;if(count_<History)++count_;
    observed_=seen;replay_us_=replay_us;permission_us_=permission_us;
  }
  Score edge(uint64_t edge,uint32_t number,uint32_t enclosure,bool mapped,const Reference& reference){
    Score s;s.row.edge20=edge;s.row.number=number;s.row.enclosure_ns=enclosure;inc(stats_.edges);
    if(edge&&edge<=last_edge_){s.row.reason=Duplicate;s.row.session=stats_.session;inc(stats_.duplicates);stats_.reason=Duplicate;remember(s.row);return s;}
    if(edge)last_edge_=edge;
    auto reason=Unmapped;
    if(mapped&&edge){
      reason=count_==History?Evicted:NoMap;
      for(unsigned i=0;i<count_;i++){
        const auto& m=maps_[(head_+History-1-i)%History];
        if(edge>=m.lo&&edge<=m.active){reason=Race;break;}
        if(m.active<edge){s.map=m;reason=!m.valid?NoMap:edge>=m.until?Expired:Ok;break;}
      }
      if(reason==Ok){
        s.relative_ns=s.map.clock.relative_at(edge);s.utc_ns=s.map.clock.at(edge);
        if(s.relative_ns<=0||s.utc_ns<=0)reason=Arithmetic;
        else {
          int64_t phase=s.relative_ns%INT64_C(1000000000);if(phase>=500000000)phase-=1000000000;
          s.row.phase_ns=int32_t(phase);s.map_valid=true;
          s.row.age_us=uint32_t(std::min<uint64_t>(UINT32_MAX,(edge-s.map.clock.tick)/20));
          s.row.following=s.map.following;s.row.permission=edge<s.map.permission;
        }
      }
    }
    const bool ready=reference.ready&&reference.epoch&&reference.rmc20&&reference.rmc20<edge&&edge-reference.rmc20<30000000;
    s.row.reference=ready;
    if(s.map_valid&&ready){
      if(stats_.root!=s.map.root||stats_.boot!=s.map.boot||stats_.source!=s.map.source||stats_.train!=s.map.train||stats_.gps_epoch!=reference.epoch){
        inc(stats_.session);stats_.samples=0;sum_=0;squares_=0;minimum_=maximum_=0;
        stats_.root=s.map.root;stats_.boot=s.map.boot;stats_.source=s.map.source;stats_.train=s.map.train;stats_.gps_epoch=reference.epoch;
      }
      if(stats_.samples!=UINT32_MAX){
        const auto p=s.row.phase_ns;sum_+=p;squares_+=double(p)*p;
        if(!stats_.samples){minimum_=maximum_=p;}else{minimum_=std::min(minimum_,p);maximum_=std::max(maximum_,p);}
        ++stats_.samples;stats_.last20=edge;
      }
    }else {if(!s.map_valid)inc(stats_.missing);if(!ready)inc(stats_.invalid_reference);}
    s.row.reason=s.map_valid&&!ready?NoFix:reason;s.row.session=stats_.session;stats_.reason=s.row.reason;
    remember(s.row);return s;
  }
  View view()const{
    auto v=stats_;if(v.samples){
      const double mean=double(sum_)/v.samples;v.mean_ns=int32_t(std::llround(mean));
      v.rms_ns=uint32_t(std::ceil(std::sqrt(squares_/v.samples)));
      v.jitter_ns=uint32_t(std::ceil(std::sqrt(std::max(0.0,squares_/v.samples-mean*mean))));
      v.max_ns=uint32_t(std::max(std::llabs(int64_t(minimum_)),std::llabs(int64_t(maximum_))));
    }return v;
  }
  bool read(unsigned offset,Record& row)const{
    if(offset>=rows_count_)return false;
    row=rows_[(tail_+Tail-rows_count_+offset)%Tail];return true;
  }
};
static_assert(sizeof(G1GpsShadow)<=1200,"C3 fixed witness RAM allocation");
}
