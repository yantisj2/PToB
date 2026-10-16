#include "rc7_relay_estimate.hpp"
#include "rc7_client.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
int main(){
  static_assert(RC_DYNAMIC_RENEW_US==300000000&&RC_DYNAMIC_LEASE_US==360000000);
  Rc7Client traffic;traffic.renew_at=300000000;traffic.progress_at=1000000;
  for(uint64_t second=1;second<300;second++){
    const auto now=second*1000000;traffic.progress_at=now;
    assert(!traffic.admission_due(now,false,false));
    assert(!traffic.admission_due(now,true,true)); // Poor delivery alone cannot churn a lock.
  }
  assert(traffic.admission_due(300000000,false,true));
  traffic.progress_at=1000000;
  assert(traffic.admission_due(3000000,false,false)); // Real missing progress still recovers.
  traffic.retry_at=13000000;
  assert(!traffic.admission_due(12999999,true,false));
  assert(traffic.admission_due(13000000,true,false));
  const r2_group_config group={{1,2,3},7,9};
  rc7_path root{};root.source_epoch=9;root.node[0]={11,22,1,1,0,0};
  const rc5_origin origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
  const G1ClockPrediction map={20000100,1000005000,0,0,origin.utc_offset};
  Rc7RelayEstimate relay;
  assert(relay.input(root,origin,nullptr,map,20000000,1,20000100,0,100,200,group));
  assert(relay.path.depth==1&&relay.path.node[1].slot==1);
  uint8_t first[190];assert(relay.packet(first,20400000,group));
  assert(!relay.ready(20400000));
  assert(!relay.selected(first,1,255,20400000,20400000,group));
  assert(relay.selected(first,1,12,20400000,20400000,group));
  assert(relay.sent[0].bound==UINT32_MAX&&relay.ready(20400000));
  const auto cap=relay.capability(100,200,7,true);
  uint8_t control[212];assert(!rc7_control_encode(control,&cap,&group));
  Rc7Client client;assert(client.make_request(cap,300,400,5,1020000,false));
  rc7_control response{};assert(relay.response(response,client.request,8,20400000));
  assert(response.ready&&!rc7_control_encode(control,&response,&group));
  assert(client.bind(response,1020000,20400000));
  uint8_t second[190];assert(relay.packet(second,21600000,group));
  rc7_packet p{};assert(!rc7_decode(&p,second,190,&relay.path,&group));
  periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.epoch=1;s.sequence=1;
  s.tick20=s.available20=21600000;s.map_enclosure_ns=100;s.event=2;s.channel=13;
  assert(!client.receive(p,s,s.available20));
  assert(relay.selected(second,2,13,s.tick20,s.tick20,group));
  uint8_t third[190];assert(relay.packet(third,22800000,group));
  assert(!rc7_decode(&p,third,190,&relay.path,&group));
  s.sequence=2;s.tick20=s.available20=22800000;s.event=3;s.channel=14;
  const auto* completed=client.receive(p,s,s.available20);
  assert(completed&&completed->stamp.event==2&&client.receiver.clock.reference.bound==UINT32_MAX);
  assert(client.receiver.clock.reference.parent_tx_ns==1080000000);
  // Same parent renewal preserves train, publication, and physical phase.
  const auto train=relay.path.node[1].train;
  auto next=map;next.tick=23000100;next.epoch=1150005000;
  assert(relay.input(root,origin,nullptr,next,23000000,1,23000100,0,100,200,group));
  assert(relay.path.node[1].train==train&&relay.publication==3);
  // The completed TX predates a newer parent map. Use its frozen
  // publication model, while requiring the current source to remain live.
  assert(!next.relative_at(21600000));
  assert(relay.selected(second,2,13,21600000,23000100,group));
  assert(relay.sent[0].tx_ns==1080000000&&relay.sent[0].age_us==80000);
  const auto held_original=relay.phase.original20();
  assert(!relay.input(root,origin,nullptr,next,23000000,1,23000100,0,100,200,group));
  assert(relay.phase.original20()==held_original);
  assert(!relay.live(33000000));
  // A map gap withdraws every correction/snapshot while preserving path,
  // train, physical synchronization and publication monotonicity.
  assert(relay.withdraw_time()&&!relay.withdraw_time());
  assert(relay.valid&&!relay.ready(33000000)&&!relay.sent[0].publication);
  uint8_t standby[190];assert(relay.packet(standby,33000000,group));
  assert(!rc7_decode(&p,standby,190,&relay.path,&group)&&!p.count&&p.train==train&&p.publication==4);
  auto recovered=next;recovered.tick=34000100;recovered.epoch=1700005000;
  assert(relay.input(root,origin,nullptr,recovered,34000000,1,34000100,0,100,200,group));
  assert(relay.path.node[1].train==train&&relay.publication==4&&!relay.ready(34000100));
  assert(!relay.selected(second,2,13,21600000,34000100,group)); // No revived pre-gap snapshot.
  auto idle=relay;const auto prior_path=idle.path;
  assert(idle.retire_publication()&&idle.live(34000100)&&!idle.ready(34000100));
  assert(idle.map.tick==relay.map.tick&&idle.original20==relay.original20&&idle.phase.original20()==relay.phase.original20());
  assert(idle.path.node[1].train==train+1&&!idle.publication&&!idle.sent[0].publication);
  assert(!rc7_path_same(&idle.path,&prior_path)&&!idle.selected(standby,4,12,34000100,34000100,group));
  uint8_t restarted[190];assert(idle.packet(restarted,34400000,group));
  assert(!rc7_decode(&p,restarted,190,&idle.path,&group)&&!p.count&&p.train==train+1&&p.publication==1);
  assert(idle.selected(restarted,5,12,34400000,34400000,group)&&idle.ready(34400000));
  assert(!idle.live(44000000)); // Train retirement never rebases upstream age.
  // RC1 may append RC2, while RC2 and a repeated identity cannot publish.
  Rc7RelayEstimate second_relay;
  assert(second_relay.input(relay.path,origin,relay.proof,recovered,34000000,1,34000100,1000,300,400,group));
  assert(second_relay.path.depth==2&&second_relay.path.node[2].slot==2);
  Rc7RelayEstimate third_relay;
  assert(!third_relay.input(second_relay.path,origin,relay.proof,recovered,34000000,1,34000100,0,500,600,group));
  assert(!third_relay.input(relay.path,origin,relay.proof,recovered,34000000,1,34000100,0,100,600,group));
  root.node[0].boot=23;auto changed=origin;changed.boot=23;
  assert(relay.input(root,changed,nullptr,recovered,34000001,1,34000100,0,100,200,group));
  assert(relay.path.node[1].train==train+1&&!relay.publication&&!relay.sent[0].publication);
  assert(!relay.selected(first,1,12,34000100,34000100,group));
  const auto placement_train=relay.path.node[1].train;
  assert(relay.select_slot(1)&&relay.valid&&relay.path.node[1].train==placement_train&&!relay.publication);
  assert(relay.select_slot(2)&&!relay.ready(34000100));
  assert(relay.input(root,changed,nullptr,recovered,34000002,1,34000100,0,100,200,group));
  assert(relay.path.node[1].slot==2&&relay.path.node[1].train==train+2);
  relay.fence();assert(!relay.ready(34000100)&&!relay.live(34000100));
  std::printf("PASS RC7 estimated functional server -> actual client codec match; UNKNOWN precision; renewal, stale-source, RC2, loop and boot fences; state=%zuB\n",sizeof relay);
}
