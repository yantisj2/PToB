#pragma once
#include "direct_auth.hpp"
namespace v12 {
constexpr size_t CredentialBytes=220;
struct NativeCredentials {
  AuthIdentity local,coordinator;uint8_t private_d[32]{};
  uint32_t config=0,registry=0,key_epoch=0;
};
bool credential_decode(const uint8_t* bytes,size_t length,const uint8_t factory_mac[6],uint8_t self,
                       uint32_t expected_config,uint32_t expected_registry,NativeCredentials& out);
bool credential_load(const uint8_t factory_mac[6],uint8_t self,NativeCredentials& out);
// Initial diagnostic build deliberately refuses persistent credentials and
// durable boot allocation. Neither NVS initialization nor any write is present.
// The typed decoder prepares a candidate future provisioning boundary only.
bool credential_allocate_boot(uint64_t& boot);
}
