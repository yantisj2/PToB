#include "direct_auth.hpp"
#include "public_crypto.hpp"
#include "public_grants.hpp"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/platform_util.h"
#include <cstring>
namespace v12 {
namespace {
void p64(uint8_t* b,uint64_t n){for(unsigned i=0;i<8;++i)b[i]=uint8_t(n>>(8*i));}
void p32(uint8_t* b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=uint8_t(n>>(8*i));}
void auth_domain(uint8_t out[13],uint32_t registry){std::memcpy(out,HelloDomain,9);p32(out+9,registry);}
bool hash2(const uint8_t* domain,size_t dn,const uint8_t* bytes,size_t n,uint8_t digest[32]){
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
  const bool ok=!mbedtls_sha256_starts(&sha,0)&&!mbedtls_sha256_update(&sha,domain,dn)&&
    !mbedtls_sha256_update(&sha,bytes,n)&&!mbedtls_sha256_finish(&sha,digest);
  mbedtls_sha256_free(&sha);return ok;
}
bool point_write(mbedtls_ecp_group& group,mbedtls_ecp_point& point,uint8_t xy[64]){
  uint8_t sec1[65];size_t n=0;
  if(mbedtls_ecp_point_write_binary(&group,&point,MBEDTLS_ECP_PF_UNCOMPRESSED,&n,sec1,sizeof(sec1))||n!=65||sec1[0]!=4)return false;
  std::memcpy(xy,sec1+1,64);return true;
}
bool sign(const uint8_t d[32],const uint8_t digest[32],uint8_t sig[64],DirectAuth::Random rng,void* context){
  if(!rng)return false;
  mbedtls_ecp_group g;mbedtls_ecp_group_init(&g);
  mbedtls_mpi key,r,s,twice;mbedtls_mpi_init(&key);mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);mbedtls_mpi_init(&twice);
  bool ok=!mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1)&&!mbedtls_mpi_read_binary(&key,d,32)&&
    !mbedtls_ecp_check_privkey(&g,&key)&&!mbedtls_ecdsa_sign_det_ext(&g,&r,&s,&key,digest,32,MBEDTLS_MD_SHA256,rng,context)&&
    !mbedtls_mpi_mul_int(&twice,&s,2);
  if(ok&&mbedtls_mpi_cmp_mpi(&twice,&g.N)>0)ok=!mbedtls_mpi_sub_mpi(&s,&g.N,&s);
  if(ok)ok=!mbedtls_mpi_write_binary(&r,sig,32)&&!mbedtls_mpi_write_binary(&s,sig+32,32);
  mbedtls_mpi_free(&twice);mbedtls_mpi_free(&s);mbedtls_mpi_free(&r);mbedtls_mpi_free(&key);mbedtls_ecp_group_free(&g);return ok;
}
bool gen_key(uint8_t d[32],uint8_t xy[64],DirectAuth::Random rng,void* context){
  if(!rng)return false;
  mbedtls_ecp_group g;mbedtls_ecp_group_init(&g);mbedtls_ecp_point p;mbedtls_ecp_point_init(&p);
  mbedtls_mpi key;mbedtls_mpi_init(&key);
  const bool ok=!mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1)&&
    !mbedtls_ecp_gen_keypair(&g,&key,&p,rng,context)&&!mbedtls_mpi_write_binary(&key,d,32)&&point_write(g,p,xy);
  mbedtls_mpi_free(&key);mbedtls_ecp_point_free(&p);mbedtls_ecp_group_free(&g);return ok;
}
bool shared(const uint8_t d[32],const uint8_t xy[64],uint8_t secret[32],DirectAuth::Random rng,void* context){
  if(!rng)return false;
  mbedtls_ecp_group g;mbedtls_ecp_group_init(&g);mbedtls_ecp_point p;mbedtls_ecp_point_init(&p);
  mbedtls_mpi key,z;mbedtls_mpi_init(&key);mbedtls_mpi_init(&z);uint8_t sec1[65]={4};std::memcpy(sec1+1,xy,64);
  const bool ok=!mbedtls_ecp_group_load(&g,MBEDTLS_ECP_DP_SECP256R1)&&!mbedtls_mpi_read_binary(&key,d,32)&&
    !mbedtls_ecp_check_privkey(&g,&key)&&!mbedtls_ecp_point_read_binary(&g,&p,sec1,65)&&
    !mbedtls_ecp_check_pubkey(&g,&p)&&!mbedtls_ecdh_compute_shared(&g,&z,&p,&key,rng,context)&&
    !mbedtls_mpi_write_binary(&z,secret,32);
  mbedtls_mpi_free(&z);mbedtls_mpi_free(&key);mbedtls_ecp_point_free(&p);mbedtls_ecp_group_free(&g);return ok;
}
bool confirm_mac(const uint8_t key[32],const uint8_t body[40],uint8_t mac[32]){
  uint8_t bytes[sizeof(ConfirmDomain)+40];std::memcpy(bytes,ConfirmDomain,sizeof(ConfirmDomain));
  std::memcpy(bytes+sizeof(ConfirmDomain),body,40);
  return !mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,32,bytes,sizeof(bytes),mac);
}
}
bool DirectAuth::live()const{
  if(!clock||!incarnation||!check_link||!check_link(link_context,incarnation))return false;
  const uint64_t now=clock(clock_context);
  return now>=started_us&&now<expires_us;
}
bool DirectAuth::begin(Random rng,void* context){
  mbedtls_platform_zeroize(local_hello,sizeof(local_hello));
  mbedtls_platform_zeroize(peer_hello,sizeof(peer_hello));
  mbedtls_platform_zeroize(ephemeral,sizeof(ephemeral));
  mbedtls_platform_zeroize(transcript,sizeof(transcript));
  mbedtls_platform_zeroize(confirmation_key,sizeof(confirmation_key));
  mbedtls_platform_zeroize(sender_key,sizeof(sender_key));
  mbedtls_platform_zeroize(peer_key,sizeof(peer_key));
  confirmed=peer_verified=hello_ready=false;
  const uint64_t now_us=clock?clock(clock_context):UINT64_MAX;
  if(!rng||!clock||!incarnation||!check_link||!check_link(link_context,incarnation)||local.identity.id==255||trusted_peer.id==255||local.identity.id==trusted_peer.id||
     (role!=LinkRole::GM&&role!=LinkRole::TimeRelay)||!config||!registry_epoch||now_us>UINT64_MAX-2000000||
     !local.boot||!local.train||!local.challenge||
     (local.identity.type!=1&&local.identity.type!=2)||
     (role==LinkRole::GM?local.root_permit!=0:local.root_permit==0)){clear();return false;}
  auto* b=local_hello;b[0]=12;b[1]=0xa1;b[2]=local.identity.id;b[3]=trusted_peer.id;
  b[4]=uint8_t(role);b[5]=local.identity.type;b[6]=0;b[7]=1;
  std::memcpy(b+8,local.identity.silicon,8);p64(b+16,local.boot);p64(b+24,local.train);
  p64(b+32,local.challenge);p64(b+40,local.root_permit);std::memcpy(b+48,local.identity.public_xy,64);
  p32(b+176,config);started_us=now_us;expires_us=now_us+2000000;
  if(!gen_key(ephemeral,b+112,rng,context)){clear();return false;}
  uint8_t digest[32],domain[13];auth_domain(domain,registry_epoch);
  hello_ready=hash2(domain,sizeof(domain),b,HelloBody,digest)&&sign(local.private_d,digest,b+HelloBody,rng,context)&&
    verify_public(local.identity.public_xy,b+HelloBody,b,HelloBody,domain,sizeof(domain));
  if(!hello_ready||!live()){clear();return false;}
  return true;
}
bool DirectAuth::receive_hello(const uint8_t* b,size_t n,Random rng,void* context){
  uint8_t domain[13];auth_domain(domain,registry_epoch);
  if(!live()||!hello_ready||peer_verified||!b||n!=HelloBytes||b[0]!=12||b[1]!=0xa1||b[2]!=trusted_peer.id||
     b[3]!=local.identity.id||b[4]!=uint8_t(role)||b[5]!=trusted_peer.type||b[6]!=0||b[7]!=1||
     std::memcmp(b+8,trusted_peer.silicon,8)||std::memcmp(b+48,trusted_peer.public_xy,64)||
     !u64(b+16)||!u64(b+24)||!u64(b+32)||u64(b+40)!=local.root_permit||u32(b+176)!=config||
     (local.identity.id!=4&&trusted_peer.id!=4)||
     !verify_public(trusted_peer.public_xy,b+HelloBody,b,HelloBody,domain,sizeof(domain))){clear();return false;}
  std::memcpy(peer_hello,b,HelloBytes);
  const bool self_first=local.identity.id==4;uint8_t ordered[HelloBytes*2];
  std::memcpy(ordered,self_first?local_hello:peer_hello,HelloBytes);
  std::memcpy(ordered+HelloBytes,self_first?peer_hello:local_hello,HelloBytes);
  uint8_t secret[32]{},keys[64]{};
  const uint8_t info[]={'P','T','O','B','1','2','K','D',1};
  bool ok=hash2(domain,sizeof(domain),ordered,sizeof(ordered),transcript)&&
    shared(ephemeral,b+112,secret,rng,context)&&
    !mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),transcript,32,secret,32,info,sizeof(info),keys,sizeof(keys));
  if(ok){std::memcpy(sender_key,keys+(self_first?0:16),16);std::memcpy(peer_key,keys+(self_first?16:0),16);
    std::memcpy(confirmation_key,keys+32,32);peer_verified=true;}
  mbedtls_platform_zeroize(secret,sizeof(secret));mbedtls_platform_zeroize(keys,sizeof(keys));
  mbedtls_platform_zeroize(ephemeral,sizeof(ephemeral));
  if(!ok||!live()){clear();return false;}
  return true;
}
bool DirectAuth::confirmation(uint8_t out[ConfirmBytes]){
  if(!live()||!peer_verified||!out){clear();return false;}
  out[0]=12;out[1]=0xa2;out[2]=local.identity.id;out[3]=trusted_peer.id;
  out[4]=uint8_t(role);out[5]=out[6]=out[7]=0;std::memcpy(out+8,transcript,32);
  if(!confirm_mac(confirmation_key,out,out+40)||!live()){clear();return false;}
  return true;
}
bool DirectAuth::receive_confirmation(const uint8_t* b,size_t n){
  if(!live()||!peer_verified||confirmed||!b||n!=ConfirmBytes||b[0]!=12||b[1]!=0xa2||b[2]!=trusted_peer.id||
     b[3]!=local.identity.id||b[4]!=uint8_t(role)||b[5]||b[6]||b[7]||std::memcmp(b+8,transcript,32)){clear();return false;}
  uint8_t mac[32];if(!confirm_mac(confirmation_key,b,mac)){clear();return false;}
  uint8_t mismatch=0;for(unsigned i=0;i<32;++i)mismatch|=mac[i]^b[i+40];
  mbedtls_platform_zeroize(mac,sizeof(mac));
  if(mismatch||!live()){clear();return false;}
  confirmed=true;return true;
}
DirectBinding DirectAuth::binding()const{
  DirectBinding b;
  if(!confirmed||!clock||clock(clock_context)<started_us||!incarnation||!check_link||
     !check_link(link_context,incarnation)||trusted_peer.id!=4||local.identity.id==4)return b;
  b.authenticated=b.encrypted=b.direct=true;b.self=local.identity.id;b.coordinator=4;b.role=role;
  b.boot=local.boot;b.coordinator_boot=u64(peer_hello+16);b.challenge=u64(peer_hello+32);b.config=config;
  std::memcpy(b.receive_key,peer_key,16);std::memcpy(b.send_key,sender_key,16);return b;
}
void DirectAuth::clear(){mbedtls_platform_zeroize(this,sizeof(*this));local.identity.id=trusted_peer.id=255;}
}
