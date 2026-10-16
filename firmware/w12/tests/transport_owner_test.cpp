#include <cassert>
#include <cstdio>
#include <cstdint>
uint64_t fake_now=1000;
#include "../main/native_transport.cpp"
FakeHsCfg ble_hs_cfg;
unsigned scan_starts=0,scan_stops=0;unsigned scenario=0;bool witnessed=false;
namespace v12 {
DirectStatus direct_status(){return {};}
void direct_host_poll(){}void direct_host_reset(){}
void diagnostic_tx_poll(){}void diagnostic_tx_reset(){}void diagnostic_tx_init(){}
DiagnosticTxStatus diagnostic_tx_status(){return {};}
}
void during_blocked_api(){fake_now=v12::lease.deadline_us+1;v12::transport_observe_lease(fake_now);
  const auto s=v12::transport_status();assert(s.lease.deadline_exceeded&&s.host_work_busy&&!s.off_acknowledged);witnessed=true;}
int ble_gap_disc_cancel(){++scan_stops;if(scenario==2)during_blocked_api();return 0;}
int ble_gap_periodic_adv_sync_create_cancel(){return 0;}
int ble_gap_periodic_adv_sync_terminate(uint16_t){return 0;}
int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*){return 0;}
int ble_gap_ext_disc(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const ble_gap_ext_disc_params*,void*,int(*)(ble_gap_event*,void*),void*){++scan_starts;if(scenario==1)during_blocked_api();return 0;}
void fixture_reset(unsigned mode){scenario=mode;fake_now=1000;scan_starts=scan_stops=0;witnessed=false;
  v12::snapshot={};v12::client={};v12::lease={};v12::queue={};v12::lease_end=0;v12::host_depth=0;
  v12::command_pending=v12::command_stop=false;v12::stop_generation=v12::stop_processed=0;v12::stop_exhausted=false;
  v12::scan_cancel_sent=v12::cancel_sent=v12::terminate_sent=false;assert(v12::transport_init());v12::sync();}
int main(){
  using namespace v12;
  fixture_reset(0);assert(transport_request(Mode::Full,8000,0x44));const auto deadline=transport_status().lease.deadline_us;
  fake_now+=3000000;command(nullptr);poll(nullptr);assert(scan_starts==1&&transport_status().lease.deadline_us==deadline);
  fake_now=deadline-5000;transport_observe_lease(fake_now);command(nullptr);auto s=transport_status();
  assert(s.off_acknowledged&&s.lease.stop_generation==s.lease.stop_completed&&!s.lease.deadline_exceeded&&scan_stops==1);
  fixture_reset(0);assert(transport_request(Mode::Full,1000,1));fake_now=lease.deadline_us+1;transport_observe_lease(fake_now);command(nullptr);
  s=transport_status();assert(!scan_starts&&s.off_acknowledged&&s.lease.deadline_exceeded&&s.lease.completed_us>0);
  fixture_reset(1);assert(transport_request(Mode::Full,1000,1));command(nullptr);poll(nullptr);assert(witnessed&&scan_starts==1&&scan_stops==1);
  assert(transport_status().lease.deadline_exceeded);command(nullptr);s=transport_status();assert(s.off_acknowledged&&s.lease.deadline_exceeded&&scan_stops==1);
  fixture_reset(0);assert(transport_request(Mode::Full,1000,1));command(nullptr);poll(nullptr);scenario=2;
  fake_now=lease.deadline_us-5000;transport_observe_lease(fake_now);command(nullptr);s=transport_status();
  assert(witnessed&&s.off_acknowledged&&s.lease.deadline_exceeded&&scan_stops==1&&s.lease.completed_us>s.lease.deadline_us);
  fixture_reset(0);uint8_t bytes[190]{};bytes[0]=189;bytes[1]=0xff;
  ble_gap_event discovery{};discovery.type=BLE_GAP_EVENT_EXT_DISC;auto& d=discovery.ext_disc;
  d.sid=0;d.props=0;d.legacy_event_type=4;d.prim_phy=1;d.sec_phy=2;d.tx_power=-7;d.periodic_adv_itvl=25;
  d.length_data=190;d.data=bytes;d.rssi=-61;d.addr.val[0]=0x46;
  gap(&discovery,nullptr);Report row;assert(transport_pop(row));
  assert(row.kind==1&&row.metadata_mask==1&&row.discovery_props==0&&row.legacy_event_type==4&&row.primary_phy==1&&row.secondary_phy==2&&row.advertised_tx_power==-7&&row.periodic_interval==25);
  assert(row.address_le[0]==0x46&&row.rssi==-61&&row.actual_length==190&&!row.refusal&&!std::memcmp(row.bytes,bytes,190));
  d.props=0x10;d.sid=255;d.sec_phy=0;d.tx_power=127;d.periodic_adv_itvl=0;d.length_data=17;
  gap(&discovery,nullptr);assert(transport_pop(row));assert(row.metadata_mask==1&&row.discovery_props==0x10&&row.advertised_tx_power==127&&row.periodic_interval==0);
  discovery.type=BLE_GAP_EVENT_PERIODIC_REPORT;discovery.periodic_report.data_length=17;discovery.periodic_report.data=bytes;
  gap(&discovery,nullptr);assert(transport_pop(row));assert(row.kind==3&&row.metadata_mask==0&&row.advertised_tx_power==127);
  std::puts("PASS actual transport bodies: original/blocked leases; complete public extended/legacy PHY, power and PA interval retained; absent metadata stays absent");
}
