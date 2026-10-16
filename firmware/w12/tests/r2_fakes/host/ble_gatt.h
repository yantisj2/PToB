#pragma once
#include "ble_hs.h"
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_GATT_CHR_F_WRITE_NO_RSP 4
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_SVC_TYPE_PRIMARY 1
struct ble_gatt_error {int status=0;};
struct ble_gatt_access_ctxt {unsigned op=BLE_GATT_ACCESS_OP_WRITE_CHR;os_mbuf* om=nullptr;uint16_t offset=0;};
struct ble_gatt_chr_def {const ble_uuid_t* uuid;int(*access_cb)(uint16_t,uint16_t,ble_gatt_access_ctxt*,void*);void* arg;void* descriptors;unsigned flags,min_key_size;uint16_t* val_handle;void* cpfd=nullptr;};
struct ble_gatt_svc_def {unsigned type;const ble_uuid_t* uuid;void* includes;const ble_gatt_chr_def* characteristics;};
struct ble_gatt_svc {uint16_t start_handle=10,end_handle=30;};
struct ble_gatt_chr {uint16_t val_handle=20;uint8_t properties=4;ble_uuid_any_t uuid;};
struct ble_gatt_attr {uint16_t handle=20;os_mbuf* om=nullptr;};
int ble_gattc_read_by_uuid(uint16_t,uint16_t,uint16_t,const ble_uuid_t*,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*);
int ble_gatts_count_cfg(const ble_gatt_svc_def*);int ble_gatts_add_svcs(const ble_gatt_svc_def*);
int ble_gattc_exchange_mtu(uint16_t,int(*)(uint16_t,const ble_gatt_error*,uint16_t,void*),void*);
int ble_gattc_disc_svc_by_uuid(uint16_t,const ble_uuid_t*,int(*)(uint16_t,const ble_gatt_error*,const ble_gatt_svc*,void*),void*);
int ble_gattc_disc_all_chrs(uint16_t,uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,const ble_gatt_chr*,void*),void*);
uint16_t ble_att_mtu(uint16_t);int ble_gattc_write_no_rsp_flat(uint16_t,uint16_t,const void*,unsigned);
int ble_gattc_read(uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*);
int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*);
