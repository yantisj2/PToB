#include "public_receiver.hpp"
#include "public_crypto.hpp"
extern "C" {
#include "shared/public_codec.h"
}
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace v12;
static size_t load(uint8_t* bytes,const char* directory,const char* kind){char path[1024];
  assert(std::snprintf(path,sizeof(path),"%s/%s.bin",directory,kind)>0);FILE* f=std::fopen(path,"rb");assert(f);
  size_t n=std::fread(bytes,1,190,f);assert(std::fgetc(f)==EOF);std::fclose(f);return n;}
static void signed_test(uint8_t* bytes,size_t n,mbedtls_ctr_drbg_context& rng){
  struct v12_public_auth_view v{};assert(!v12_public_auth_view(&v,bytes,n));
  uint8_t xy[64];const uint8_t generator[]={
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96,
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5};std::memcpy(xy,generator,64);
  assert(!verify_public(xy,v.signature,v.body,v.body_bytes,v.domain,v.domain_bytes));
  uint8_t input[200],digest[32];std::memcpy(input,v.domain,v.domain_bytes);std::memcpy(input+v.domain_bytes,v.body,v.body_bytes);
  assert(!mbedtls_sha256(input,v.domain_bytes+v.body_bytes,digest,0));
  mbedtls_ecp_group g;mbedtls_ecp_group_init(&g);assert(!mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1));
  mbedtls_mpi d,r,s,twice;mbedtls_mpi_init(&d);mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);mbedtls_mpi_init(&twice);
  assert(!mbedtls_mpi_lset(&d,1));assert(!mbedtls_ecdsa_sign_det_ext(&g,&r,&s,&d,digest,32,MBEDTLS_MD_SHA256,mbedtls_ctr_drbg_random,&rng));
  assert(!mbedtls_mpi_mul_int(&twice,&s,2));if(mbedtls_mpi_cmp_mpi(&twice,&g.N)>0)assert(!mbedtls_mpi_sub_mpi(&s,&g.N,&s));
  uint8_t* signature=bytes+n-64;assert(!mbedtls_mpi_write_binary(&r,signature,32)&&!mbedtls_mpi_write_binary(&s,signature+32,32));
  assert(!v12_public_auth_view(&v,bytes,n)&&verify_public(xy,v.signature,v.body,v.body_bytes,v.domain,v.domain_bytes));
  bytes[7]^=1;assert(!verify_public(xy,v.signature,v.body,v.body_bytes,v.domain,v.domain_bytes));bytes[7]^=1;
  PublicReceiver passive;passive.receive(bytes,n);assert(passive.status.authenticated==0);
  if(bytes[5]>=129){passive.trust.issuer=4;passive.trust.provisioned=true;std::memcpy(passive.trust.xy,xy,64);
    passive.receive(bytes,n);assert(passive.status.authenticated==1);if(bytes[5]==129)assert(passive.status.freshness_unknown==1);}
  mbedtls_mpi_free(&twice);mbedtls_mpi_free(&s);mbedtls_mpi_free(&r);mbedtls_mpi_free(&d);mbedtls_ecp_group_free(&g);
}
int main(int argc,char** argv){assert(argc==2);uint8_t ad[190],packed[190];size_t n,m;
  mbedtls_entropy_context e;mbedtls_entropy_init(&e);mbedtls_ctr_drbg_context rng;mbedtls_ctr_drbg_init(&rng);
  const uint8_t tag[]="PUBLIC_TEST_ONLY_V12_CODEC";assert(!mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&e,tag,sizeof(tag)));
  v12_public_timing root{},relay{};v12_public_grant grant{},extension{};
  n=load(ad,argv[1],"ROOT_TIMING");assert(n==158&&!v12_public_timing_unpack(&root,ad,n));
  assert(root.publisher==0&&root.corrections[0].event==31&&root.corrections[1].payload_generation==31);
  assert(!v12_public_timing_pack(packed,&m,&root)&&m==n&&!std::memcmp(ad,packed,n));signed_test(ad,n,rng);
  n=load(ad,argv[1],"RELAY_TIMING");assert(n==182&&!v12_public_timing_unpack(&relay,ad,n));
  assert(relay.publisher==5&&relay.upstream_original_id==31);signed_test(ad,n,rng);
  n=load(ad,argv[1],"ROOT_GRANT");assert(n==190&&!v12_public_grant_unpack(&grant,ad,n));signed_test(ad,n,rng);
  n=load(ad,argv[1],"RELAY_GRANT");assert(n==190&&!v12_public_grant_unpack(&extension,ad,n));signed_test(ad,n,rng);
  assert(!v12_public_parent_match(&grant,&extension)&&!v12_public_timing_scope(&root,&grant,nullptr)&&!v12_public_timing_scope(&relay,&grant,&extension));
  extension.coordinator_boot++;assert(v12_public_parent_match(&grant,&extension));extension.coordinator_boot--;
  extension.expires_UTC_ns++;assert(v12_public_parent_match(&grant,&extension));extension.expires_UTC_ns--;
  uint64_t lower,upper;assert(!v12_public_created_bounds(&grant,&lower,&upper)&&upper==lower+100000);
  assert(v12_public_freshness(&grant,true,false,upper,upper)&&v12_public_freshness(&grant,true,true,lower,lower));
  assert(!v12_public_freshness(&grant,true,true,upper,upper)&&v12_public_freshness(&grant,true,true,upper,grant.expires_UTC_ns));
  root.corrections[1].event=root.corrections[0].event;assert(v12_public_timing_pack(packed,&m,&root));
  std::puts("PASS shared candidate2 codec: independent158/182/190 fixtures, real low-S signed-body verifier all4 domains, zero-id roster, malformed/parent/scope/UTC refusal; no physical authority");
  mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&e);
}
