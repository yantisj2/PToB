#include <cassert>
#include <cstdio>
#include <cstdint>
uint64_t fake_now=1000,fake_step=0;
int64_t advancing_clock(){const auto value=fake_now;fake_now+=fake_step;return int64_t(value);}
#define FAKE_ESP_TIMER_READ advancing_clock
#define V12_UNIT "C1"
#define V12_BUILD_ID "0000000000000000000000000000000000000000000000000000000000000000"
#define V12_SOURCE_REVISION "0000000000000000000000000000000000000000"
#include "../main/native_diagnostic_tx.cpp"
unsigned pa_starts=0,ad_starts=0,pa_stops=0,ad_stops=0;int last_duration=0;unsigned scenario=0;bool witnessed=false;
namespace v12 {
TransportStatus transport_status(){TransportStatus s;s.host_ready=true;s.off_acknowledged=true;return s;}
DirectStatus direct_status(){return {};}
}
os_mbuf* ble_hs_mbuf_from_flat(const void*,size_t){static os_mbuf m;return &m;}
void during_blocked_api(){
  fake_now=v12::lease.deadline_us+1;v12::diagnostic_tx_observe_lease(fake_now);
  const auto s=v12::diagnostic_tx_status();
  assert(s.lease.deadline_exceeded&&!s.off_acknowledged&&s.owner_busy);witnessed=true;
}
int ble_gap_ext_adv_configure(unsigned,const ble_gap_ext_adv_params* p,int8_t* power,int(*)(ble_gap_event*,void*),void*){
  assert(p->primary_phy==1&&p->secondary_phy==1&&p->sid==0&&p->tx_power==0&&power);
  *power=scenario==5?127:-7;if(scenario==1)during_blocked_api();return 0;}
int ble_hs_id_copy_addr(uint8_t type,uint8_t* address,int* nrpa){assert(type==BLE_ADDR_PUBLIC);
  const uint8_t public_address[]={0x6a,0x06,0xad,0x76,0xb1,0x44};std::memcpy(address,public_address,6);*nrpa=scenario==6?1:0;return scenario==7?4:0;}
int ble_gap_ext_adv_set_data(unsigned,os_mbuf*){return 0;}
int ble_gap_periodic_adv_configure(unsigned,const ble_gap_periodic_adv_params*){return 0;}
int ble_gap_periodic_adv_set_data(unsigned,os_mbuf*){return 0;}
int ble_gap_periodic_adv_start(unsigned){++pa_starts;if(scenario==2)during_blocked_api();if(scenario==3){fake_now=v12::lease.deadline_us-1;fake_step=2;}return 0;}
int ble_gap_ext_adv_start(unsigned,int duration,unsigned){++ad_starts;last_duration=duration;assert(duration>0&&duration<=1000);return 0;}
int ble_gap_periodic_adv_stop(unsigned){++pa_stops;if(scenario==4)during_blocked_api();return 0;}
int ble_gap_ext_adv_stop(unsigned){++ad_stops;return 0;}
void reset(unsigned mode){
  scenario=mode;fake_now=1000;fake_step=0;pa_starts=ad_starts=pa_stops=ad_stops=0;witnessed=false;
  v12::owned={};v12::snapshot={};v12::lease={};v12::pending=v12::stop_pending=false;v12::run_id=0;
  v12::periodic_stop_sent=v12::discovery_stop_sent=false;
  const uint8_t factory[]={0x44,0xb1,0x76,0xad,6,0x68};v12::diagnostic_tx_identity(factory,1);v12::diagnostic_tx_init();
}
int main(){
  reset(0);assert(v12::diagnostic_tx_request(158,8000,123));const auto deadline=v12::diagnostic_tx_status().lease.deadline_us;
  fake_now+=3000000;v12::diagnostic_tx_poll();auto s=v12::diagnostic_tx_status();
  assert(s.requested&&s.periodic&&s.discovery&&s.lease.deadline_us==deadline&&last_duration==500);
  assert(s.selected_power_available&&s.selected_tx_power==-7&&s.public_address_available);
  const uint8_t expected_address[]={0x6a,0x06,0xad,0x76,0xb1,0x44};assert(!std::memcmp(s.public_address_le,expected_address,6));
  fake_now=deadline-5000;v12::diagnostic_tx_observe_lease(fake_now);v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
  assert(s.off_acknowledged&&s.lease.stop_generation==s.lease.stop_completed&&!s.lease.deadline_exceeded);
  assert(pa_stops==1&&ad_stops==1);
  reset(0);assert(v12::diagnostic_tx_request(182,1000,55));fake_now=v12::lease.deadline_us+1;v12::diagnostic_tx_observe_lease(fake_now);v12::diagnostic_tx_poll();
  s=v12::diagnostic_tx_status();assert(!pa_starts&&!ad_starts&&s.lease.deadline_exceeded&&s.off_acknowledged);
  reset(1);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
  assert(witnessed&&s.lease.deadline_exceeded&&!pa_starts&&!ad_starts);v12::diagnostic_tx_poll();assert(v12::diagnostic_tx_status().off_acknowledged);
  reset(2);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
  assert(witnessed&&s.lease.deadline_exceeded&&pa_starts==1&&pa_stops==1&&!ad_starts);v12::diagnostic_tx_poll();assert(v12::diagnostic_tx_status().off_acknowledged&&pa_stops==1);
  reset(3);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();assert(!ad_starts&&pa_stops==1);
  // Actual stop body stays busy past its original deadline; expiry is visible
  // from the independent observer BEFORE that API returns, then one stop ACK.
  reset(0);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();scenario=4;
  fake_now=v12::lease.deadline_us-5000;v12::diagnostic_tx_observe_lease(fake_now);v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
  assert(witnessed&&s.off_acknowledged&&s.lease.deadline_exceeded&&pa_stops==1&&ad_stops==1);
  assert(s.lease.completed_us>s.lease.deadline_us);
  reset(5);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
  assert(s.requested&&!s.selected_power_available&&s.selected_tx_power==127&&s.public_address_available);
  ble_gap_event complete{};complete.type=BLE_GAP_EVENT_ADV_COMPLETE;complete.adv_complete.reason=6;complete.adv_complete.num_ext_adv_events=9;
  v12::gap(&complete,nullptr);s=v12::diagnostic_tx_status();
  assert(s.completion_reason_available&&s.completion_reason==6&&s.reported_completed_events==9&&!s.completed_events_available&&s.completed==1);
  for(unsigned mode=6;mode<=7;++mode){reset(mode);assert(v12::diagnostic_tx_request(158,1000,1));v12::diagnostic_tx_poll();s=v12::diagnostic_tx_status();
    assert(s.fault&&!s.public_address_available&&!pa_starts&&!ad_starts);}
  std::puts("PASS actual diagnostic_tx bodies: original/blocked leases, selected power sentinel and distinct public address, unavailable max_events=0 count, identity refusal before RF");
}
