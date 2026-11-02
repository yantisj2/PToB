/* Copyright 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Reciprocal GM admission return path. The shared RC owner owns the exact
 * connection, wire length, authenticated exchange and original deadlines.
 */
#include "ble_service.h"
#include <zephyr/bluetooth/gatt.h>
static struct bt_uuid_128 service_uuid=BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x850d0400,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
static struct bt_uuid_128 data_uuid=BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x850d0401,0xa6ef,0x4775,0x974b,0x188a7d4ed45c));
static const uint8_t marker[]={'R','2','G','1',1};
static ssize_t read_marker(struct bt_conn *conn,const struct bt_gatt_attr *attr,
        void *buf,uint16_t len,uint16_t offset){
    return bt_gatt_attr_read(conn,attr,buf,len,offset,marker,sizeof marker);
}
static ssize_t receive(struct bt_conn *conn,const struct bt_gatt_attr *attr,
        const void *buf,uint16_t len,uint16_t offset,uint8_t flags){
    return r2_rc7_gm_receive(conn,buf,len,offset);
}
BT_GATT_SERVICE_DEFINE(mhat_gm_control_service,
    BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&data_uuid.uuid,
        BT_GATT_CHRC_READ|BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_READ|BT_GATT_PERM_WRITE,read_marker,receive,NULL));
