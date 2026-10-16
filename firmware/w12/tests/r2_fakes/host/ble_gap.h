#pragma once
#include "ble_hs.h"
#define BLE_GAP_ROLE_MASTER 0
#define BLE_GAP_ROLE_SLAVE 1
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ADV_COMPLETE 3
#define BLE_GAP_EVENT_EXT_DISC 4
#define BLE_GAP_EVENT_DISC_COMPLETE 5
#define BLE_GAP_EVENT_PERIODIC_SYNC 6
#define BLE_GAP_EVENT_PERIODIC_SYNC_LOST 7
#define BLE_GAP_EVENT_PERIODIC_REPORT 8
#define BLE_HCI_ADV_DATA_STATUS_COMPLETE 0
#define BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE 0
struct ble_gap_conn_desc {uint16_t conn_handle=22,conn_itvl=6,conn_latency=0,supervision_timeout=10;uint8_t role=BLE_GAP_ROLE_MASTER;ble_addr_t peer_id_addr;};
struct ble_gap_event {int type=0;
  struct {int status=0;uint16_t conn_handle=22;}connect;
  struct {ble_gap_conn_desc conn;int reason=0;}disconnect;
  struct {unsigned instance=1;}adv_complete;
  struct {ble_addr_t addr;uint8_t sid=12,props=0,data_status=0,legacy_event_type=0,prim_phy=1,sec_phy=1;int8_t rssi=-45,tx_power=0;uint16_t periodic_adv_itvl=25,length_data=0;const uint8_t* data=nullptr;}ext_disc;
  struct {uint8_t status=0,sid=12,adv_phy=1,adv_clk_accuracy=7;uint16_t sync_handle=9,per_adv_ival=25;ble_addr_t adv_addr;}periodic_sync;
  struct {uint16_t sync_handle=9;}periodic_sync_lost;
  struct {uint16_t sync_handle=9,data_length=0;uint8_t data_status=0;int8_t rssi=-45;const uint8_t* data=nullptr;}periodic_report;
};
struct ble_gap_ext_adv_params {unsigned connectable=0,scannable=0,legacy_pdu=0;uint8_t own_addr_type=1,primary_phy=1,secondary_phy=1,sid=0;uint16_t itvl_min=0,itvl_max=0;int8_t tx_power=0;};
struct ble_gap_ext_disc_params {unsigned passive=1;uint16_t itvl=0,window=0;};
struct ble_gap_periodic_adv_params {uint16_t itvl_min=0,itvl_max=0;};
int ble_gap_periodic_adv_configure(unsigned,const ble_gap_periodic_adv_params*);
int ble_gap_periodic_adv_set_data(unsigned,os_mbuf*);int ble_gap_periodic_adv_start(unsigned);int ble_gap_periodic_adv_stop(unsigned);
struct ble_gap_periodic_sync_params {uint16_t skip=0,sync_timeout=0;};
struct ble_gap_conn_params {uint16_t itvl_min=0,itvl_max=0,supervision_timeout=0,scan_itvl=0,scan_window=0;};
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc*);
int ble_gap_conn_find_by_addr(const ble_addr_t*,ble_gap_conn_desc*);
int ble_gap_conn_active();
int ble_gap_ext_adv_active(unsigned);int ble_gap_ext_adv_stop(unsigned);
int ble_gap_ext_adv_remove(unsigned);
int ble_gap_ext_adv_configure(unsigned,const ble_gap_ext_adv_params*,int8_t*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_ext_adv_set_data(unsigned,os_mbuf*);int ble_gap_ext_adv_start(unsigned,int,unsigned);
int ble_gap_disc_cancel();int ble_gap_terminate(uint16_t,int);int ble_gap_conn_cancel();
int ble_gap_periodic_adv_sync_create_cancel();int ble_gap_periodic_adv_sync_terminate(uint16_t);
int ble_gap_ext_disc(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const ble_gap_ext_disc_params*,void*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_set_data_len(uint16_t,uint16_t,uint16_t);
int ble_gap_connect(unsigned,const ble_addr_t*,unsigned,const ble_gap_conn_params*,int(*)(ble_gap_event*,void*),void*);

int ble_gap_disc_active();
