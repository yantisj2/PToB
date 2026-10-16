#pragma once
#include <cstdint>
#include "ble_hs.h"
#define BLE_GAP_EVENT_ADV_COMPLETE 1
#define BLE_GAP_EVENT_EXT_DISC 2
#define BLE_GAP_EVENT_PERIODIC_SYNC 3
#define BLE_GAP_EVENT_PERIODIC_REPORT 4
#define BLE_GAP_EVENT_PERIODIC_SYNC_LOST 5
#define BLE_GAP_EVENT_DISC_COMPLETE 6
#define BLE_HCI_ADV_DATA_STATUS_COMPLETE 0
#define BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE 0
struct ble_gap_event {int type;struct {unsigned instance=0;int reason=0;uint8_t num_ext_adv_events=0;}adv_complete;
  struct {uint8_t sid=0;ble_addr_t addr;int8_t rssi=0,tx_power=127;uint8_t data_status=0,props=0,legacy_event_type=0,prim_phy=0,sec_phy=0;unsigned length_data=0;uint16_t periodic_adv_itvl=25;const uint8_t* data=nullptr;}ext_disc;
  struct {uint8_t status=0,sid=0;uint16_t sync_handle=0;}periodic_sync;
  struct {uint16_t sync_handle=0;int8_t rssi=0;uint8_t data_status=0;unsigned data_length=0;const uint8_t* data=nullptr;}periodic_report;
  struct {uint16_t sync_handle=0;int reason=0;}periodic_sync_lost;
};
struct ble_gap_ext_adv_params {uint8_t own_addr_type,primary_phy,secondary_phy,sid;uint16_t itvl_min,itvl_max;int8_t tx_power;};
struct ble_gap_periodic_adv_params {uint16_t itvl_min,itvl_max;};
int ble_gap_periodic_adv_stop(unsigned);int ble_gap_ext_adv_stop(unsigned);
int ble_gap_ext_adv_configure(unsigned,const ble_gap_ext_adv_params*,int8_t*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_ext_adv_set_data(unsigned,os_mbuf*);int ble_gap_periodic_adv_configure(unsigned,const ble_gap_periodic_adv_params*);
int ble_gap_periodic_adv_set_data(unsigned,os_mbuf*);int ble_gap_periodic_adv_start(unsigned);int ble_gap_ext_adv_start(unsigned,int,unsigned);

struct ble_gap_ext_disc_params {uint8_t passive;uint16_t itvl,window;};
struct ble_gap_periodic_sync_params {uint16_t skip,sync_timeout;};
int ble_gap_disc_cancel();int ble_gap_periodic_adv_sync_create_cancel();int ble_gap_periodic_adv_sync_terminate(uint16_t);
int ble_gap_ext_disc(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const ble_gap_ext_disc_params*,void*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*);
