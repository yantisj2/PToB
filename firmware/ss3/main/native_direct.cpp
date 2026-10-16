#include "native_direct.hpp"
#include "native_credentials.hpp"
#include "native_transport.hpp"
#include "public_receiver.hpp"
#include "bounded_log.hpp"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "esp_bt.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include <cstring>
namespace v12 {
namespace {
constexpr uint16_t NoHandle=UINT16_MAX;
enum class JobKind:uint8_t {Start,Hello,Confirm,Control,Grant,Reset};
enum class TxKind:uint8_t {Hello,Confirm,Established};
struct Job {JobKind kind=JobKind::Reset;uint32_t incarnation=0;uint16_t handle=NoHandle,length=0;
  LinkRole role=LinkRole::None;uint64_t root_permit=0;uint8_t bytes[244]{};};
struct Tx {TxKind kind=TxKind::Hello;uint32_t incarnation=0;uint16_t length=0;uint8_t bytes[244]{};};
struct Characteristic {uint16_t definition=0,value=0,end=0,cccd=0;uint8_t properties=0;uint16_t id=0;};
struct Host {
  DirectStage stage=DirectStage::Off;LinkRole role=LinkRole::None;ble_addr_t peer{};
  uint32_t incarnation=0;uint16_t handle=NoHandle,mtu=0,svc_start=0,svc_end=0;
  uint64_t lease_end=0,operation_end=0,root_permit=0;
  bool encrypted=false,confirmed=false,close_sent=false,cancel_sent=false;
  Characteristic chars[8]{};unsigned chr_count=0,descriptor=0,subscribe=0;
} host;
portMUX_TYPE direct_mux=portMUX_INITIALIZER_UNLOCKED;
Ring<Job,8> jobs;Ring<Tx,8> transmit;DirectStatus snapshot;
NativeCredentials credentials;bool credential_present=false,request_pending=false,stop_pending=false,abort_pending=false;
Host requested;TaskHandle_t worker_task=nullptr;
uint64_t micros(){const int64_t now=esp_timer_get_time();return now>0?uint64_t(now):0;}
uint64_t auth_clock(void*){return micros();}
void count(uint32_t DirectStatus::* field){portENTER_CRITICAL(&direct_mux);
  if(snapshot.*field!=UINT32_MAX)++(snapshot.*field);
  portEXIT_CRITICAL(&direct_mux);}
void publish(){portENTER_CRITICAL(&direct_mux);
  snapshot.stage=host.stage;snapshot.role=host.role;snapshot.incarnation=host.incarnation;snapshot.handle=host.handle;
  snapshot.mtu=host.mtu;snapshot.encrypted=host.encrypted;snapshot.confirmed=host.confirmed;
  snapshot.credentials=credential_present;snapshot.connected_until=host.lease_end;
  snapshot.queue_drops=jobs.drops+transmit.drops;snapshot.queue_peak=jobs.peak>transmit.peak?jobs.peak:transmit.peak;
  portEXIT_CRITICAL(&direct_mux);}
bool queue_job(const Job& job){portENTER_CRITICAL(&direct_mux);const bool ok=jobs.push(job);
  if(!ok)abort_pending=true;
  portEXIT_CRITICAL(&direct_mux);return ok;}
bool queue_tx(const Tx& tx){portENTER_CRITICAL(&direct_mux);const bool ok=transmit.push(tx);
  if(!ok)abort_pending=true;
  portEXIT_CRITICAL(&direct_mux);return ok;}
void abort_worker(){portENTER_CRITICAL(&direct_mux);abort_pending=true;portEXIT_CRITICAL(&direct_mux);}
void close(){
  if(host.stage==DirectStage::Off||host.stage==DirectStage::Fault||host.stage==DirectStage::Closing)return;
  host.stage=DirectStage::Closing;host.confirmed=false;
  Job j;j.kind=JobKind::Reset;j.incarnation=host.incarnation;queue_job(j);
  if(host.handle!=NoHandle&&!host.close_sent){host.close_sent=true;
    if(ble_gap_terminate(host.handle,BLE_ERR_REM_USER_CONN_TERM)){count(&DirectStatus::api_errors);host.stage=DirectStage::Fault;}}
  else if(host.handle==NoHandle&&!host.cancel_sent){host.cancel_sent=true;
    if(ble_gap_conn_cancel()){count(&DirectStatus::api_errors);host.stage=DirectStage::Fault;}}
  publish();
}
bool api(int rc){if(!rc)return true;count(&DirectStatus::api_errors);close();return false;}
bool current(uint16_t handle,void* arg){return handle==host.handle&&host.incarnation==uint32_t(uintptr_t(arg))&&
  host.stage!=DirectStage::Closing&&host.stage!=DirectStage::Fault&&host.stage!=DirectStage::Off&&micros()<host.lease_end;}
void* tag(){return reinterpret_cast<void*>(uintptr_t(host.incarnation));}
ble_uuid128_t uuid(uint16_t id){ble_uuid128_t value=BLE_UUID128_INIT(0,0xd9,0x5d,0x2b,0x81,0x51,0x28,0xad,0xe4,0x4e,0x9f,0x22,0x94,0xb2,0x12,0x7e);
  value.value[0]=uint8_t(id);value.value[1]=uint8_t(id>>8);return value;}
Characteristic* characteristic(uint16_t id){for(auto& c:host.chars)if(c.id==id&&c.value)return &c;return nullptr;}
uint16_t hello_id(){return host.role==LinkRole::GM?0xd911:0xd921;}
uint16_t confirm_id(){return host.role==LinkRole::GM?0xd912:0xd922;}
uint16_t grant_id(){return host.role==LinkRole::GM?0xd914:0xd924;}
bool link_evidence(void*,uint64_t incarnation){
  const auto s=direct_status();if(s.incarnation!=incarnation||s.handle==NoHandle||!s.encrypted||micros()>=s.connected_until||
    s.stage==DirectStage::Closing||s.stage==DirectStage::Off||s.stage==DirectStage::Fault)return false;
  ble_gap_conn_desc d{};if(ble_gap_conn_find(s.handle,&d)||d.conn_handle!=s.handle||d.role!=BLE_GAP_ROLE_MASTER||
    !d.sec_state.encrypted||d.sec_state.key_size!=16)return false;
  // Recheck after the host lookup to close handle reuse/disconnect races.
  const auto after=direct_status();return after.incarnation==incarnation&&after.handle==s.handle&&after.encrypted&&
    after.stage!=DirectStage::Closing&&after.stage!=DirectStage::Off&&after.stage!=DirectStage::Fault;
}
int true_random(void*,uint8_t* bytes,size_t n){
  if(!bytes||esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED)return -1;
  esp_fill_random(bytes,n);return 0;
}
int value_callback(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*);
int write_callback(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr*,void* arg){
  if(!current(handle,arg))return 0;
  if(!error||error->status){count(&DirectStatus::refusals);close();return 0;}
  if(host.stage==DirectStage::WriteHello){host.stage=DirectStage::ReadHello;
    api(ble_gattc_read(handle,characteristic(hello_id())->value,value_callback,tag()));}
  else if(host.stage==DirectStage::WriteConfirm){host.stage=DirectStage::ReadConfirm;
    api(ble_gattc_read(handle,characteristic(confirm_id())->value,value_callback,tag()));}
  else if(host.stage==DirectStage::Subscribe){++host.subscribe;}
  else {count(&DirectStatus::refusals);close();}
  publish();return 0;
}
int value_callback(uint16_t handle,const ble_gatt_error* error,ble_gatt_attr* attr,void* arg){
  if(!current(handle,arg))return 0;
  Job j;j.incarnation=host.incarnation;j.handle=handle;j.role=host.role;
  if(!error||error->status||!attr||!attr->om){count(&DirectStatus::refusals);close();return 0;}
  j.kind=host.stage==DirectStage::ReadHello?JobKind::Hello:JobKind::Confirm;
  const uint16_t expected=j.kind==JobKind::Hello?244:72;
  uint16_t copied=0;
  if(OS_MBUF_PKTLEN(attr->om)!=expected||ble_hs_mbuf_to_flat(attr->om,j.bytes,sizeof(j.bytes),&copied)||copied!=expected){
    count(&DirectStatus::refusals);close();return 0;}
  j.length=copied;host.stage=j.kind==JobKind::Hello?DirectStage::WorkerConfirm:DirectStage::Subscribe;
  queue_job(j);publish();return 0;
}
int descriptor_callback(uint16_t handle,const ble_gatt_error* error,uint16_t value,const ble_gatt_dsc* dsc,void* arg){
  if(!current(handle,arg))return 0;
  if(error&&error->status==BLE_HS_EDONE){++host.descriptor;return 0;}
  if(!error||error->status||!dsc||host.descriptor>=host.chr_count){count(&DirectStatus::refusals);close();return 0;}
  auto& c=host.chars[host.descriptor];
  if(value!=c.value||dsc->handle<=c.value||dsc->handle>c.end){count(&DirectStatus::refusals);close();return 0;}
  if(ble_uuid_u16(&dsc->uuid.u)==BLE_GATT_DSC_CLT_CFG_UUID16){
    if(c.cccd){count(&DirectStatus::refusals);close();}else c.cccd=dsc->handle;
  }return 0;
}
int characteristic_callback(uint16_t handle,const ble_gatt_error* error,const ble_gatt_chr* chr,void* arg){
  if(!current(handle,arg))return 0;
  if(error&&error->status==BLE_HS_EDONE){
    for(unsigned i=0;i<host.chr_count;++i)host.chars[i].end=i+1<host.chr_count?host.chars[i+1].definition-1:host.svc_end;
    const auto* h=characteristic(hello_id());const auto* c=characteristic(confirm_id());const auto* g=characteristic(grant_id());
    const auto* extra=characteristic(host.role==LinkRole::GM?0xd913:0xd925);
    if(!h||!c||!g||!extra||(h->properties&0x0a)!=0x0a||(c->properties&0x0a)!=0x0a||
       !(g->properties&0x20)||!(extra->properties&0x20)||(host.role==LinkRole::TimeRelay&&characteristic(0xd913))){
      count(&DirectStatus::refusals);close();return 0;}
    host.stage=DirectStage::Descriptors;host.descriptor=0;host.operation_end=micros()+1000000;publish();return 0;
  }
  if(!error||error->status||!chr||host.chr_count>=8||chr->def_handle<host.svc_start||chr->val_handle>host.svc_end||
     chr->val_handle<=chr->def_handle){count(&DirectStatus::refusals);close();return 0;}
  if(host.chr_count&&chr->def_handle<=host.chars[host.chr_count-1].definition){count(&DirectStatus::refusals);close();return 0;}
  auto& c=host.chars[host.chr_count++];c.definition=chr->def_handle;c.value=chr->val_handle;c.properties=chr->properties;
  for(uint16_t id=0xd911;id<=0xd926;++id){auto candidate=uuid(id);if(!ble_uuid_cmp(&chr->uuid.u,&candidate.u)){c.id=id;break;}}
  return 0;
}
int service_callback(uint16_t handle,const ble_gatt_error* error,const ble_gatt_svc* service,void* arg){
  if(!current(handle,arg))return 0;
  if(error&&error->status==BLE_HS_EDONE){
    if(!host.svc_start||host.svc_end<=host.svc_start){count(&DirectStatus::refusals);close();return 0;}
    host.stage=DirectStage::Characteristics;host.operation_end=micros()+1000000;
    api(ble_gattc_disc_all_chrs(handle,host.svc_start,host.svc_end,characteristic_callback,tag()));publish();return 0;
  }
  if(!error||error->status||!service||host.svc_start){count(&DirectStatus::refusals);close();return 0;}
  host.svc_start=service->start_handle;host.svc_end=service->end_handle;return 0;
}
int mtu_callback(uint16_t handle,const ble_gatt_error* error,uint16_t mtu,void* arg){
  if(!current(handle,arg))return 0;
  if(!error||error->status||mtu<247){count(&DirectStatus::refusals);close();return 0;}
  host.mtu=mtu;host.stage=DirectStage::Service;host.operation_end=micros()+1000000;
  const auto service=uuid(host.role==LinkRole::GM?0xd901:0xd902);
  api(ble_gattc_disc_svc_by_uuid(handle,&service.u,service_callback,tag()));publish();return 0;
}
int gap(ble_gap_event* event,void* arg){
  if(host.incarnation!=uint32_t(uintptr_t(arg)))return 0;
  if(event->type==BLE_GAP_EVENT_CONNECT){
    if(event->connect.status){count(&DirectStatus::refusals);host.stage=DirectStage::Off;publish();return 0;}
    host.handle=event->connect.conn_handle;
    if(host.stage==DirectStage::Closing||micros()>=host.lease_end){host.stage=DirectStage::Security;host.close_sent=false;close();return 0;}
    host.stage=DirectStage::Security;host.operation_end=micros()+5000000;
    if(api(ble_gap_set_data_len(host.handle,251,2120)))api(ble_gap_security_initiate(host.handle));
  }else if(event->type==BLE_GAP_EVENT_ENC_CHANGE){
    ble_gap_conn_desc d{};
    if(event->enc_change.status||event->enc_change.conn_handle!=host.handle||ble_gap_conn_find(host.handle,&d)||
       !d.sec_state.encrypted||d.sec_state.key_size!=16||d.role!=BLE_GAP_ROLE_MASTER){count(&DirectStatus::refusals);close();return 0;}
    if(host.stage!=DirectStage::Security){count(&DirectStatus::refusals);close();return 0;}
    host.encrypted=true;host.stage=DirectStage::Mtu;host.operation_end=micros()+1000000;
    api(ble_gattc_exchange_mtu(host.handle,mtu_callback,tag()));
  }else if(event->type==BLE_GAP_EVENT_DISCONNECT){
    if(event->disconnect.conn.conn_handle==host.handle){Job j;j.kind=JobKind::Reset;j.incarnation=host.incarnation;queue_job(j);
      host.handle=NoHandle;host.encrypted=host.confirmed=false;host.stage=DirectStage::Off;}
  }else if(event->type==BLE_GAP_EVENT_NOTIFY_RX){
    const auto& r=event->notify_rx;if(r.conn_handle!=host.handle||host.stage!=DirectStage::Established||!r.indication){
      count(&DirectStatus::refusals);close();return 0;}
    Job j;j.incarnation=host.incarnation;j.handle=host.handle;j.role=host.role;
    const auto* control=characteristic(0xd913);const auto* grant=characteristic(grant_id());const auto* parent=characteristic(0xd925);
    if(host.role==LinkRole::GM&&control&&r.attr_handle==control->value){j.kind=JobKind::Control;j.length=110;}
    else if((grant&&r.attr_handle==grant->value)||(host.role==LinkRole::TimeRelay&&parent&&r.attr_handle==parent->value)){
      j.kind=JobKind::Grant;j.length=190;
    }else {count(&DirectStatus::refusals);close();return 0;}
    uint16_t copied=0;if(!r.om||OS_MBUF_PKTLEN(r.om)!=j.length||ble_hs_mbuf_to_flat(r.om,j.bytes,sizeof(j.bytes),&copied)||copied!=j.length){
      count(&DirectStatus::refusals);close();return 0;}
    queue_job(j);
  }else if(event->type==BLE_GAP_EVENT_REPEAT_PAIRING)return BLE_GAP_REPEAT_PAIRING_IGNORE;
  publish();return 0;
}
void worker(void*){
  DirectAuth auth;ControlState control;PublicReceiver receiver;uint64_t boot=0,train=0;
  while(true){Job j;portENTER_CRITICAL(&direct_mux);const bool have=jobs.pop(j);portEXIT_CRITICAL(&direct_mux);
    if(have){const uint64_t begin=micros();bool ok=true;
      if(j.kind==JobKind::Reset){if(j.incarnation==auth.incarnation){auth.clear();control.fence();receiver=PublicReceiver();}}
      else if(j.kind==JobKind::Start){
        auth.clear();control.fence();receiver=PublicReceiver();
        if(!credential_present||(!boot&&!credential_allocate_boot(boot))||train==UINT64_MAX)ok=false;
        else{
          auth.local.identity=credentials.local;std::memcpy(auth.local.private_d,credentials.private_d,32);
          auth.trusted_peer=credentials.coordinator;auth.role=j.role;auth.config=credentials.config;auth.registry_epoch=credentials.registry;
          auth.local.boot=boot;auth.local.train=++train;auth.local.root_permit=j.root_permit;
          auth.incarnation=j.incarnation;auth.clock=auth_clock;auth.check_link=link_evidence;
          ok=!true_random(nullptr,reinterpret_cast<uint8_t*>(&auth.local.challenge),8)&&auth.local.challenge&&auth.begin(true_random,nullptr);
          if(ok){Tx tx;tx.kind=TxKind::Hello;tx.incarnation=j.incarnation;tx.length=244;std::memcpy(tx.bytes,auth.local_hello,244);ok=queue_tx(tx);}
        }
      }else if(j.incarnation!=auth.incarnation||!link_evidence(nullptr,j.incarnation))ok=false;
      else if(j.kind==JobKind::Hello){
        ok=auth.receive_hello(j.bytes,j.length,true_random,nullptr);
        if(ok){Tx tx;tx.kind=TxKind::Confirm;tx.incarnation=j.incarnation;tx.length=72;
          ok=auth.confirmation(tx.bytes)&&queue_tx(tx);}
      }else if(j.kind==JobKind::Confirm){
        ok=auth.receive_confirmation(j.bytes,j.length)&&control.bind(auth.binding());
        if(ok){receiver.trust.issuer=4;receiver.trust.provisioned=true;std::memcpy(receiver.trust.xy,credentials.coordinator.public_xy,64);
          Tx tx;tx.kind=TxKind::Established;tx.incarnation=j.incarnation;ok=queue_tx(tx);}
      }else if(j.kind==JobKind::Control){
        // Missing measured conservative coordinator-clock map prevents TTL
        // admission. Task arrival does not substitute a coordinator clock.
        count(&DirectStatus::control_refused);
        logf("W12_V12_CONTROL_REFUSE incarnation=%lu reason=COORDINATOR_CLOCK_MAP_UNKNOWN source=UNKNOWN capture=UNSUPPORTED phase=UNSUPPORTED authority=NONE\n",(unsigned long)j.incarnation);
      }else if(j.kind==JobKind::Grant){
        const auto before=receiver.status;receiver.receive(j.bytes,j.length);
        if(receiver.status.authenticated>before.authenticated)count(&DirectStatus::grant_verified);
        if(receiver.status.freshness_unknown>before.freshness_unknown)count(&DirectStatus::grant_freshness_unknown);
        logf("W12_V12_DIRECT_GRANT incarnation=%lu bytes=%u authenticated=%lu current_UTC=UNKNOWN authority=NONE source=UNKNOWN precision=UNAVAILABLE\n",
          (unsigned long)j.incarnation,unsigned(j.length),(unsigned long)receiver.status.authenticated);
      }
      const uint64_t work=micros()-begin;portENTER_CRITICAL(&direct_mux);
      if(work>snapshot.crypto_max_us)snapshot.crypto_max_us=work;
      portEXIT_CRITICAL(&direct_mux);
      if(!ok){auth.clear();control.fence();receiver=PublicReceiver();count(&DirectStatus::crypto_errors);abort_worker();}
      mbedtls_platform_zeroize(&j,sizeof(j));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}
bool direct_init(const uint8_t mac[6],uint8_t self){
  credential_present=credential_load(mac,self,credentials);publish();
  return xTaskCreatePinnedToCore(worker,"v12_auth",16384,nullptr,1,&worker_task,1)==pdPASS;
}
void direct_host_poll(){
  bool stop,abort,request;Host command;
  portENTER_CRITICAL(&direct_mux);stop=stop_pending;abort=abort_pending;request=request_pending;command=requested;
  stop_pending=abort_pending=request_pending=false;portEXIT_CRITICAL(&direct_mux);
  if(stop||abort)close();
  if(request&&host.stage==DirectStage::Off&&!stop&&!abort){
    host=command;host.stage=DirectStage::Connecting;host.operation_end=micros()+5000000;
    ble_gap_conn_params p{};p.scan_itvl=p.scan_window=32;p.itvl_min=p.itvl_max=24;p.supervision_timeout=200;
    if(!api(ble_gap_connect(BLE_OWN_ADDR_PUBLIC,&host.peer,5000,&p,gap,tag())))host.stage=DirectStage::Fault;
  }
  const uint64_t now=micros();
  if(host.stage!=DirectStage::Off&&host.stage!=DirectStage::Fault&&host.stage!=DirectStage::Closing&&
     (now>=host.lease_end||(host.stage!=DirectStage::Established&&now>=host.operation_end)))close();
  // Descriptor and CCCD work is single-flight, bounded and performed only on
  // the NimBLE host queue. No dynamic lookup/cache is trusted across connects.
  static unsigned pending_descriptor=UINT32_MAX,pending_subscribe=UINT32_MAX;
  if(host.stage==DirectStage::Descriptors){
    if(host.descriptor==host.chr_count){
      const auto* grant=characteristic(grant_id());const auto* extra=characteristic(host.role==LinkRole::GM?0xd913:0xd925);
      if(!grant||!extra||!grant->cccd||!extra->cccd){count(&DirectStatus::refusals);close();}
      else{host.stage=DirectStage::WorkerHello;host.operation_end=now+2000000;Job j;j.kind=JobKind::Start;j.incarnation=host.incarnation;
        j.handle=host.handle;j.role=host.role;j.root_permit=host.root_permit;queue_job(j);}
    }else if(pending_descriptor!=host.descriptor){pending_descriptor=host.descriptor;auto& c=host.chars[host.descriptor];
      if(c.end<=c.value)++host.descriptor;else api(ble_gattc_disc_all_dscs(host.handle,c.value,c.end,descriptor_callback,tag()));}
  }else pending_descriptor=UINT32_MAX;
  Tx tx;portENTER_CRITICAL(&direct_mux);const bool have=transmit.pop(tx);portEXIT_CRITICAL(&direct_mux);
  if(have&&tx.incarnation==host.incarnation&&host.stage!=DirectStage::Closing&&host.stage!=DirectStage::Fault){
    if((tx.kind==TxKind::Hello&&host.stage==DirectStage::WorkerHello)||(tx.kind==TxKind::Confirm&&host.stage==DirectStage::WorkerConfirm)){
      auto* c=characteristic(tx.kind==TxKind::Hello?hello_id():confirm_id());
      if(!c||host.mtu<tx.length+3){count(&DirectStatus::refusals);close();}
      else{host.stage=tx.kind==TxKind::Hello?DirectStage::WriteHello:DirectStage::WriteConfirm;
        api(ble_gattc_write_flat(host.handle,c->value,tx.bytes,tx.length,write_callback,tag()));}
    }else if(tx.kind==TxKind::Established&&host.stage==DirectStage::Subscribe){host.confirmed=true;host.subscribe=0;host.operation_end=now+1000000;}
    else{count(&DirectStatus::refusals);close();}
  }
  mbedtls_platform_zeroize(&tx,sizeof(tx));
  if(host.stage==DirectStage::Subscribe&&host.confirmed){
    if(host.subscribe==2){host.stage=DirectStage::Established;host.operation_end=0;
    }
    else if(pending_subscribe!=host.subscribe){pending_subscribe=host.subscribe;
      auto* c=characteristic(host.subscribe==0?grant_id():(host.role==LinkRole::GM?0xd913:0xd925));const uint8_t indicate[2]={2,0};
      if(!c||!c->cccd){count(&DirectStatus::refusals);close();}
      else api(ble_gattc_write_flat(host.handle,c->cccd,indicate,2,write_callback,tag()));}
  }else pending_subscribe=UINT32_MAX;
  publish();
}
void direct_host_reset(){
  if(!host.incarnation&&host.stage==DirectStage::Off&&host.handle==NoHandle&&!request_pending&&!stop_pending&&!abort_pending){publish();return;}
  Job j;j.kind=JobKind::Reset;j.incarnation=host.incarnation;queue_job(j);
  host.stage=DirectStage::Fault;host.encrypted=host.confirmed=false;host.handle=NoHandle;publish();}
bool direct_request(LinkRole role,uint8_t address_type,const uint8_t addr[6],uint64_t root_permit,uint32_t duration_ms){
  if(!addr||(role!=LinkRole::GM&&role!=LinkRole::TimeRelay)||address_type>1||duration_ms<1000||duration_ms>10000||
     (role==LinkRole::GM?root_permit!=0:root_permit==0)){count(&DirectStatus::refusals);return false;}
  portENTER_CRITICAL(&direct_mux);
  const bool ok=credential_present&&!request_pending&&!stop_pending&&!abort_pending&&snapshot.stage==DirectStage::Off&&snapshot.incarnation<UINT32_MAX;
  if(ok){requested={};requested.role=role;requested.incarnation=snapshot.incarnation+1;requested.root_permit=root_permit;
    requested.peer.type=address_type;std::memcpy(requested.peer.val,addr,6);requested.lease_end=micros()+uint64_t(duration_ms)*1000;request_pending=true;}
  portEXIT_CRITICAL(&direct_mux);if(!ok)count(&DirectStatus::refusals);return ok;
}
void direct_stop(){portENTER_CRITICAL(&direct_mux);stop_pending=true;request_pending=false;portEXIT_CRITICAL(&direct_mux);}
DirectStatus direct_status(){portENTER_CRITICAL(&direct_mux);const auto s=snapshot;portEXIT_CRITICAL(&direct_mux);return s;}
}
