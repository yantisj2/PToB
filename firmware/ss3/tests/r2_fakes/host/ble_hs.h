#pragma once
#define BLE_HS_HCI_ERR(x) ((x) ? 0x200+(x) : 0)
#define BLE_ERR_MEM_CAPACITY 0x07
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#define BLE_HS_EALREADY 2
#define BLE_HS_ENOTCONN 3
#define BLE_HS_EDONE 4
#define BLE_HS_EINVAL 5
#define BLE_HS_EBUSY 6
#define BLE_HS_ENOMEM 7
#define BLE_HS_ENOENT 8
#define BLE_OWN_ADDR_RANDOM 1
#define BLE_ADDR_RANDOM 1
#define BLE_ADDR_PUBLIC 0
#define BLE_HCI_LE_PHY_1M 1
#define BLE_ERR_REM_USER_CONN_TERM 0x13
struct ble_addr_t {uint8_t type=0,val[6]{};};
struct os_mbuf {std::vector<uint8_t> bytes;};
inline int os_mbuf_append(os_mbuf* m,const void* p,unsigned n){auto* b=static_cast<const uint8_t*>(p);m->bytes.insert(m->bytes.end(),b,b+n);return 0;}
#define OS_MBUF_PKTLEN(m) ((m)->bytes.size())
inline int os_mbuf_copydata(os_mbuf* m,unsigned at,unsigned n,void* out){if(at+n>m->bytes.size())return -1;std::memcpy(out,m->bytes.data()+at,n);return 0;}
inline os_mbuf* ble_hs_mbuf_from_flat(const void* p,unsigned n){auto* m=new os_mbuf;m->bytes.assign(static_cast<const uint8_t*>(p),static_cast<const uint8_t*>(p)+n);return m;}
inline void os_mbuf_free_chain(os_mbuf* m){delete m;}
struct ble_uuid_t {uint8_t type;};
struct ble_uuid128_t {ble_uuid_t u;uint8_t value[16];};
#define BLE_UUID128_INIT(...) {{128},{__VA_ARGS__}}
union ble_uuid_any_t {ble_uuid_t u;ble_uuid128_t u128;};
inline int ble_uuid_cmp(const ble_uuid_t* a,const ble_uuid_t* b){return std::memcmp(reinterpret_cast<const ble_uuid128_t*>(a)->value,reinterpret_cast<const ble_uuid128_t*>(b)->value,16);}

#define BLE_HS_EPREEMPTED 29
#define BLE_HS_ETIMEOUT 13

#define BLE_HS_FOREVER 0x7fffffff

#define BLE_HS_ETIMEOUT_HCI 19
