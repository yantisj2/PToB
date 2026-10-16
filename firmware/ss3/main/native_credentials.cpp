#include "native_credentials.hpp"
#include "public_grants.hpp"
#include <cstring>
#ifndef V12_CONFIG_EPOCH
#define V12_CONFIG_EPOCH 1
#endif
#ifndef V12_REGISTRY_EPOCH
#define V12_REGISTRY_EPOCH 1
#endif
namespace v12 {
// Initial live diagnostic build preserves the entire installed protected
// prefix, including legacy NVS. Persistent provisioning/migration is a later
// primary-reviewed gate; this build neither initializes nor writes NVS.
namespace {
uint32_t crc(const uint8_t* b,size_t n){uint32_t c=~uint32_t(0);
  for(size_t i=0;i<n;++i){c^=b[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return ~c;}
bool nonzero(const uint8_t* b,size_t n){uint8_t value=0;for(size_t i=0;i<n;++i)value|=b[i];return value!=0;}
}
bool credential_decode(const uint8_t* b,size_t n,const uint8_t mac[6],uint8_t self,
                       uint32_t config,uint32_t registry,NativeCredentials& out){
  out={};const uint8_t coor_silicon[8]={0}; // Legacy storage is unavailable; no embedded coordinator enrollment.
  if(!b||!mac||n!=CredentialBytes||std::memcmp(b,"V12K",4)||b[4]!=1||b[5]!=self||b[6]!=2||b[7]!=1||
     (self!=2&&self!=5)||u32(b+8)!=config||!config||u32(b+12)!=registry||!registry||!u32(b+16)||
     std::memcmp(b+20,mac,6)||b[26]||b[27]||std::memcmp(b+124,coor_silicon,8)||b[196]!=4||b[197]!=1||
     !nonzero(b+28,64)||!nonzero(b+92,32)||!nonzero(b+132,64)||u32(b+216)!=crc(b,216))return false;
  for(size_t i=198;i<216;++i)if(b[i])return false;
  out.local.id=self;out.local.type=2;std::memcpy(out.local.silicon,b+20,8);std::memcpy(out.local.public_xy,b+28,64);
  std::memcpy(out.private_d,b+92,32);out.coordinator.id=4;out.coordinator.type=1;
  std::memcpy(out.coordinator.silicon,b+124,8);std::memcpy(out.coordinator.public_xy,b+132,64);
  out.config=config;out.registry=registry;out.key_epoch=u32(b+16);return true;
}
bool credential_load(const uint8_t mac[6],uint8_t self,NativeCredentials& out){
  (void)mac;(void)self;out={};return false;
}
bool credential_allocate_boot(uint64_t& boot){boot=0;return false;}
}
