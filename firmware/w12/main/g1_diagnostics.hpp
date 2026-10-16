#pragma once
#include <cstdint>
namespace v12 {
// Fixed worker-owned counters. Reasons describe observations, never authority.
enum class G1Reason:uint8_t {None,Malformed,Group,Epoch,Flags,Address,Identity,
  Window,Owner,Retry,Cooldown,Hint,CurrentPeer,JoinStale,JoinDecode,JoinBusy,
  JoinExchange,JoinTime,RootHint,PeriodicStale,PeriodicAuth,PeriodicScope,
  Permission,Count};
struct G1Diagnostics {
  struct Peer {uint64_t identity=0,last_us=0;uint32_t eligible=0,attempts=0,
    accepts=0,failures=0;int8_t rssi=127;};
  uint32_t reasons[unsigned(G1Reason::Count)]{};
  G1Reason first=G1Reason::None,last=G1Reason::None;
  uint64_t first_us=0,last_us=0;
  Peer peers[8]{};
  uint32_t eligible=0,descriptor_seen=0,connect_ok=0,connect_failed=0,connect_drain=0,
    traces=0,closed=0,scope_changes=0;
  uint32_t close_min_margin_us=UINT32_MAX,disconnect_max_us=0;
  uint64_t last_candidate=0,last_candidate_us=0;
  int8_t last_rssi=127;
  // Public controller getters, not measured radiated power.127=unavailable.
  int8_t default_tx_dbm=127,scan_tx_dbm=127,init_tx_dbm=127,conn_tx_dbm=127;
  static void add(uint32_t& n){if(n!=UINT32_MAX)++n;}
  void reason(G1Reason r,uint64_t now){if(r==G1Reason::None)return;
    add(reasons[unsigned(r)]);if(first==G1Reason::None){first=r;first_us=now;}
    last=r;last_us=now;}
  Peer& peer(uint64_t id){unsigned slot=0;
    for(unsigned i=0;i<8;i++){if(peers[i].identity==id)return peers[i];
      if(!peers[i].identity){slot=i;break;}
      if(peers[i].last_us<peers[slot].last_us)slot=i;}
    peers[slot]={};peers[slot].identity=id;return peers[slot];}
  void candidate(uint64_t id,int8_t rssi,uint64_t at){add(eligible);
    last_candidate=id;last_candidate_us=at;last_rssi=rssi;
    auto& p=peer(id);p.last_us=at;p.rssi=rssi;add(p.eligible);}
};
inline const char* g1_reason_name(G1Reason r){
  constexpr const char* names[]={"NONE","MALFORMED","GROUP","EPOCH","FLAGS",
    "ADDRESS","IDENTITY","WINDOW","OWNER","RETRY","COOLDOWN","HINT",
    "CURRENT_PEER","JOIN_STALE","JOIN_AUTH","JOIN_BUSY","JOIN_EXCHANGE",
    "JOIN_TIME","ROOT_HINT","PA_STALE","PA_AUTH","PA_SCOPE","PERMISSION"};
  return unsigned(r)<unsigned(G1Reason::Count)?names[unsigned(r)]:"INVALID";
}
}
