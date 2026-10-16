#include "public_crypto.hpp"
#include "public_grants.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(){
  // Public test-only d=1: never linked into firmware or used as trust material.
  mbedtls_ecp_group group;mbedtls_ecp_group_init(&group);assert(!mbedtls_ecp_group_load(&group,MBEDTLS_ECP_DP_SECP256R1));
  mbedtls_mpi d,r,s,twice;mbedtls_mpi_init(&d);mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);mbedtls_mpi_init(&twice);
  mbedtls_entropy_context entropy;mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_context random;mbedtls_ctr_drbg_init(&random);
  const uint8_t personalization[]="V12-public-d1-fixture-blinding";
  assert(!mbedtls_ctr_drbg_seed(&random,mbedtls_entropy_func,&entropy,personalization,sizeof(personalization)));
  assert(!mbedtls_mpi_lset(&d,1));
  const uint8_t bytes[]={0xff,0xff,12,129,4,0,0,0,1,1};
  const auto& domain=v12::RootGrantDomain;
  uint8_t digest[32],combined[sizeof(domain)+sizeof(bytes)]{};
  std::memcpy(combined,domain,sizeof(domain));std::memcpy(combined+sizeof(domain),bytes,sizeof(bytes));
  assert(!mbedtls_sha256(combined,sizeof(combined),digest,0));
  assert(!mbedtls_ecdsa_sign_det_ext(&group,&r,&s,&d,digest,32,MBEDTLS_MD_SHA256,mbedtls_ctr_drbg_random,&random));
  assert(!mbedtls_mpi_mul_int(&twice,&s,2));
  if(mbedtls_mpi_cmp_mpi(&twice,&group.N)>0)assert(!mbedtls_mpi_sub_mpi(&s,&group.N,&s));
  uint8_t xy[64],signature[64],sec1[65];size_t sec1_length=0;
  assert(!mbedtls_ecp_point_write_binary(&group,&group.G,MBEDTLS_ECP_PF_UNCOMPRESSED,&sec1_length,sec1,sizeof(sec1)));
  assert(sec1_length==65&&sec1[0]==4);std::memcpy(xy,sec1+1,64);
  assert(!mbedtls_mpi_write_binary(&r,signature,32));assert(!mbedtls_mpi_write_binary(&s,signature+32,32));
  assert(v12::verify_public(xy,signature,bytes,sizeof(bytes),domain,sizeof(domain)));
  uint8_t changed[sizeof(bytes)];std::memcpy(changed,bytes,sizeof(bytes));changed[2]^=1;
  assert(!v12::verify_public(xy,signature,changed,sizeof(changed),domain,sizeof(domain)));
  assert(!v12::verify_public(xy,signature,bytes,sizeof(bytes),domain,sizeof(domain)-1));
  uint8_t sig[64];std::memcpy(sig,signature,64);assert(!mbedtls_mpi_sub_mpi(&s,&group.N,&s));
  assert(!mbedtls_mpi_write_binary(&s,sig+32,32));assert(!v12::verify_public(xy,sig,bytes,sizeof(bytes),domain,sizeof(domain)));
  std::memset(sig,0,64);assert(!v12::verify_public(xy,sig,bytes,sizeof(bytes),domain,sizeof(domain)));
  assert(!mbedtls_mpi_write_binary(&group.N,sig,32));assert(!v12::verify_public(xy,sig,bytes,sizeof(bytes),domain,sizeof(domain)));
  uint8_t point[64]{};assert(!v12::verify_public(point,signature,bytes,sizeof(bytes),domain,sizeof(domain)));
  std::memcpy(point,xy,64);point[63]^=1;assert(!v12::verify_public(point,signature,bytes,sizeof(bytes),domain,sizeof(domain)));
  assert(!v12::verify_public(xy,signature,bytes,192,domain,sizeof(domain)));
  std::puts("PASS actual MbedTLS P256 verifier:low-S valid; mutated bytes/domain,high-S,zero/order scalars,invalid/off-curve XY,length refused; test keys not linked to firmware");
  mbedtls_ctr_drbg_free(&random);mbedtls_entropy_free(&entropy);
  mbedtls_mpi_free(&twice);mbedtls_mpi_free(&s);mbedtls_mpi_free(&r);mbedtls_mpi_free(&d);mbedtls_ecp_group_free(&group);
}
