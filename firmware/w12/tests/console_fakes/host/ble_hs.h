#pragma once
#define BLE_HS_EALREADY 2
#define BLE_OWN_ADDR_PUBLIC 0
#define BLE_ADDR_PUBLIC 0
#define BLE_HCI_LE_PHY_1M 1
struct os_mbuf {};
#define BLE_HS_ENOTCONN 3
#define BLE_HS_IO_NO_INPUT_OUTPUT 0
struct ble_addr_t {uint8_t type=0,val[6]{};};
struct FakeHsCfg {void(*reset_cb)(int)=nullptr;void(*sync_cb)()=nullptr;uint8_t sm_io_cap=0,sm_sc=0,sm_mitm=0,sm_bonding=0,sm_our_key_dist=0,sm_their_key_dist=0;};
extern FakeHsCfg ble_hs_cfg;
inline int ble_hs_util_ensure_addr(unsigned){return 0;}
int ble_hs_id_copy_addr(uint8_t,uint8_t*,int*);
