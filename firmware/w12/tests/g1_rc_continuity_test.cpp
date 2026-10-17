#include "g1_admission_policy.hpp"
#include "g1_client_policy.hpp"
#include "g1_retained_estimate.hpp"
#include "g1_receive_loss.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace v12;
int main(){
  r2_join_record r{};r.sender=11;r.sender_boot=22;
  assert(g1_admission_reply(r)==G1AdmissionReply::NoSource);
  r.root=33;r.root_boot=44;r.source_flags=1;assert(g1_admission_reply(r)==G1AdmissionReply::ForeignRoot);
  r.root=11;assert(g1_admission_reply(r)==G1AdmissionReply::Unusable);
  r.root_boot=22;assert(g1_admission_reply(r)==G1AdmissionReply::Direct);
  r.control=1;assert(g1_admission_reply(r)==G1AdmissionReply::Unusable);
  G1AdmissionPolicy a;assert(a.allowed(1000000));a.started(1000000);a.acquired();
  assert(!a.allowed(20999999)&&a.allowed(21000000));a.started(21000000);
  assert(!a.allowed(41000000)&&!a.allowed(60999999)&&a.allowed(61000000));
  a.started(61000000);assert(!a.allowed(80999999)&&a.allowed(81000000));
  a.failed(81000000,0);assert(a.retry_at==101000000&&!a.allowed(100999999));
  a.failed(81000000,20000000);assert(a.retry_at==121000000);
  assert(!a.allowed(60000000)); // Backwards clock cannot reset rolling starts.
  for(auto reply:{G1AdmissionReply::NoSource,G1AdmissionReply::ForeignRoot,G1AdmissionReply::Unusable})a.reply(reply);
  assert(a.no_source==1&&a.foreign_root==1&&a.unusable==1&&a.warm);
  const auto starts=a.starts[0];a.reply(G1AdmissionReply::Direct);assert(!a.failures&&a.starts[0]==starts&&a.warm);
  // A direct replay refresh must allow one failed full exchange, maximum
  // existing jittered backoff, and a second full exchange before replay dies.
  // Exercise the production refresh and rolling-start policies together,
  // across the accepted bootstrap uncertainty range. Fresh PA permission is
  // independent here; no retry may lengthen its original replay anchor.
  for(uint32_t uncertainty:{40000u,60000u,80000u,99999u}){
    G1Anchor anchor{true,11,22,1700000000000000,1000000,200000000,3,4,uncertainty};
    const auto deadline=anchor.deadline(),permission=anchor.until;
    G1AdmissionPolicy retry;retry.started(anchor.at);retry.acquired();
    auto first=anchor.at;
    while(!anchor.refresh(first,true))first+=1000;
    assert(anchor.valid(first)&&retry.allowed(first));retry.started(first);
    retry.failed(first+2000000,20000000); // failed exchange + worst40s backoff
    const auto second=retry.retry_at;
    assert(retry.allowed(second)&&anchor.valid(second+2000000));
    retry.started(second);assert(!retry.allowed(second+2000000));
    assert(anchor.deadline()==deadline&&anchor.until==permission&&!anchor.valid(deadline));
    // Negative control: starting at the previous200ms uncertainty threshold
    // cannot fit this same unchanged retry schedule inside the old deadline.
    auto late=anchor.at;while(anchor.guard(late)<200000)late+=1000;
    assert(!anchor.valid(late+2000000+40000000+2000000));
    assert(anchor.refresh(first,true)&&!anchor.urgent(first,true));
  }
  G1RetainedEstimate e;uint64_t utc=0;assert(!e.deliver(1,utc));
  G1ClockPrediction p{20000000,1000000000,500,2000,1700000000000000000};
  assert(e.retain(p,11,22,3,19999000,1000000));
  assert(e.deliver(20000000,utc));const auto initial=utc;
  assert(e.deliver(20ULL*1800000000,utc)&&utc>initial); // No fabricated source lease or numeric quality.
  assert(e.clock.tick==p.tick&&e.clock.rate==p.rate&&e.observed_us==1000000);
  assert(!e.deliver(p.tick-1,utc));e.clear();assert(!e.deliver(p.tick,utc));
  G1ReceiveLoss c;assert(sizeof(c)==160);c.begin();
  for(unsigned i=0;i<12;i++)c.observe(100+i,G1ReceiveLoss::Report|(i&1?G1ReceiveLoss::Mapped:0),7);
  assert(c.sequence==12&&c.count==8&&c.entries[(c.sequence-c.count)%8].at==104);
  c.freeze(999);c.observe(1000,0,9);assert(c.sequence==12&&c.frozen_at==999);
  const auto first=c;c.reacquired();c.begin();c.observe(1001,0,8);c.freeze(1002);
  assert(c.generation==first.generation&&c.frozen&&c.count==first.count&&c.sequence==first.sequence&&c.frozen_at==999);
  assert(!std::memcmp(c.entries,first.entries,sizeof c.entries)&&c.expiries==2&&c.reacquisitions==1);
  std::puts("PASS NO_SOURCE classification, warm rolling retry budget, retained local estimate and passive loss lifecycle");
}
