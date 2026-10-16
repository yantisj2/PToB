/* Config5 actual codec/network: independent sources, restricted client, loss/rejoin. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_network n[4];
static bool online[4];static unsigned delay=6000;static bool oscillator_drift;
struct flight{uint8_t bytes[R2_AUTH_BYTES],from,to;uint64_t due;};
static struct flight q[1024];static unsigned head,tail;
static uint64_t local(unsigned p,uint64_t t){static const int ppm[4]={200,-400,400,-200};return t+1000000+p*173000+(oscillator_drift?(int64_t)t*ppm[p]/1000000:0);}
static void init(unsigned p,uint64_t t,unsigned incarnation)
{
 assert(!r2_network_init(&n[p],p,4,p+1,100+p+incarnation*100,10001+p+incarnation*100,40000,local(p,t)));
 r2_network_capabilities(&n[p],7,7);n[p].relative_source=true;
 for(unsigned j=0;j<4;j++){n[p].peer[j].identity=j+1;for(unsigned b=0;b<16;b++){n[p].peer[j].tx_key[b]=1+b*13+p*31+j*7;n[p].peer[j].rx_key[b]=1+b*13+j*31+p*7;}}
 online[p]=true;
}
static void step(uint64_t t,bool source0)
{
 while(head!=tail&&q[head%1024].due<=t){struct flight *f=&q[head++%1024];struct r2_auth_record rec;assert(!r2_auth_decode(&rec,f->bytes,r2_auth_size(f->bytes[1]),n[f->to].peer[f->from].rx_key));
  if(f->from==0&&f->to==3&&t>=150200000&&t<150800000)continue; /* Existing original source permit remains fresh. */
  if(f->to==3&&rec.kind>=R2_AUTH_PREPARE)continue; /* Client lacks management support. */
  if(online[f->to]&&online[f->from])r2_network_receive(&n[f->to],f->from,f->bytes,r2_auth_size(f->bytes[1]),local(f->to,t),local(f->to,t));}
 for(unsigned p=0;p<4;p++)if(online[p]){
  uint64_t now=local(p,t);for(unsigned j=0;j<4;j++)if(j!=p)r2_network_link(&n[p],j,online[j],now);
  struct r2_clock c={0};c.view.source_available=p!=0||source0;c.view.source_epoch=1;
  c.last_pair_edge=local(p,t/1000000*1000000);c.last_rmc=c.last_pair_edge;c.last_pair_utc=1791417600000000LL+t/1000000*1000000;
  r2_network_timing(&n[p],n[p].root_boot,n[p].view.generation,n[p].publication,n[p].local_gm,n[p].ready,now);
  r2_network_tick(&n[p],&c,now);
  for(unsigned k=0;k<8;k++){uint8_t peer,wire[R2_AUTH_BYTES];bool hello;if(!r2_network_next(&n[p],&peer,wire,&hello))break;
   assert(tail-head<1024);struct flight *f=&q[tail++%1024];memcpy(f->bytes,wire,sizeof wire);f->from=p;f->to=peer;f->due=t+delay;r2_network_sent(&n[p],hello,peer,wire);
  }
 }
}
static void codec(void)
{
 uint8_t key[16]={1},wire[R2_AUTH_BYTES];struct r2_auth_record a={.kind=10,.sender=0,.target=1,.root=255,.boot=3,.coordinator_boot=3,.challenge=4,.sequence=5,.decision=6,.config_epoch=R2_AUTH_CONFIG,.created_us=1000,.evidence_us=900,.expires_us=251000,.priority1=128,.clock_class=248,.accuracy=254,.priority2=128,.variance=65535,.boot_age_ms=60000,.source_age_ms=60000},b;
 assert(!r2_auth_encode(wire,&a,key));assert(!r2_auth_decode(&b,wire,r2_auth_size(wire[1]),key));assert(b.source_age_ms==60000&&b.variance==65535);
 for(unsigned i=0;i<sizeof wire;i++){wire[i]^=1;assert(r2_auth_decode(&b,wire,r2_auth_size(wire[1]),key));wire[i]^=1;}
 assert(r2_auth_decode(&b,wire,sizeof wire-1,key));
}
int main(int argc,char **argv)
{
 if(argc>1){unsigned value=0;assert(sscanf(argv[1],"%u",&value)==1&&value>=2000&&value<=30000);delay=value;}
 oscillator_drift=argc>2;
 codec();for(unsigned p=0;p<4;p++)init(p,0,0);
 uint32_t pub=0;unsigned hot=0;uint64_t gap_permit=0,gap_candidate=0;
 for(uint64_t t=0;t<245000000;t+=2000){
  if(t==90000000){pub=n[0].publication;online[1]=false;}
  if(t==100000000)init(1,t,1);
  step(t,t<180000000||t>=195000000);
  if(t<60000000)for(unsigned p=0;p<4;p++)assert(!n[p].ready);
  if(t>70000000&&t<90000000){for(unsigned p=0;p<4;p++){assert(n[p].ready&&n[p].choice==0&&n[p].coordinator==1);assert(!(n[p].members_mask&8));assert(n[p].publication==n[0].publication);}hot+=n[1].hot_ready;}
  if(t==150200000){gap_permit=n[3].service_until;gap_candidate=n[3].peer[0].candidate_until;}
  if(t>=150200000&&t<150800000){assert(n[3].peer[0].authenticated&&n[3].peer[0].connected);assert(n[3].now-n[3].peer[0].seen<10000000);assert(n[3].service_until==gap_permit&&n[3].peer[0].candidate_until==gap_candidate&&gap_permit>n[3].now);}
  if(t>75000000&&t<90000000)assert(n[1].hot_ready);
  if(t>=90000000&&t<180000000){assert(n[0].local_gm&&n[0].publication==pub);assert(n[2].follower_valid&&n[2].publication==pub);assert(n[3].follower_valid&&n[3].publication==pub);}
  if(t>185000000){assert(!n[3].local_gm&&n[3].coordinator!=3);}
  if(t>205000000){for(unsigned p=0;p<4;p++)assert(n[p].ready&&n[p].choice==2);}
 }
 assert(hot>1000);
 /* With all classes due, source wins; after admission HELLO can renew its
  * bounded map rather than being starved by an unbounded priority stream. */
 struct r2_network priority=n[2];struct r2_net_peer *dest=&priority.peer[3];
 dest->next_service=0;dest->next_hello=priority.now;
 uint8_t selected,packet[R2_AUTH_BYTES];bool selected_hello;struct r2_auth_record selection;
 assert(r2_network_next_ready(&priority,8,&selected,packet,&selected_hello));
 assert(!r2_auth_decode(&selection,packet,r2_auth_size(packet[1]),dest->tx_key)&&selection.kind==R2_AUTH_SERVICE&&!selected_hello);
 r2_network_sent(&priority,false,selected,packet);
 struct r2_network overdue=priority;overdue.peer[3].next_service=0;overdue.peer[3].next_hello=overdue.now-50000;
 assert(r2_network_next_ready(&overdue,8,&selected,packet,&selected_hello)&&selected_hello);
 assert(r2_network_next_ready(&priority,8,&selected,packet,&selected_hello)&&selected_hello);
 priority=n[3];dest=&priority.peer[2];dest->next_hello=0;
 dest->response=(struct r2_auth_record){.kind=R2_AUTH_ACK,.sender=3,.target=1,.boot=priority.boot,.coordinator_boot=priority.root_boot,.challenge=dest->nonce,.sequence=1,.created_us=priority.now,.evidence_us=priority.now,.expires_us=priority.now+250000,.config_epoch=R2_AUTH_CONFIG};dest->response_pending=true;
 assert(r2_network_next_ready(&priority,4,&selected,packet,&selected_hello));
 assert(!r2_auth_decode(&selection,packet,r2_auth_size(packet[1]),dest->tx_key)&&selection.kind==R2_AUTH_ACK&&!selected_hello);
 /* Five50ms retries must not erase an otherwise live250ms ACK. */
 struct r2_network history=n[2];uint64_t base=history.now,first_sequence=history.tx_sequence+1;
 struct r2_auth_record issued={.kind=R2_AUTH_SERVICE,.sender=2,.target=3,.root=2,.flags=5,.boot=history.boot,.coordinator_boot=history.boot,.challenge=history.peer[3].nonce,.decision=history.publication,.root_boot=history.boot,.source_epoch=history.candidate.generation,.config_epoch=R2_AUTH_CONFIG,.source_second=1791417844,.permit_until_us=base+1400000};
 for(unsigned i=0;i<5;i++){history.now=base+i*50000;issued.sequence=first_sequence+i;issued.created_us=issued.evidence_us=history.now;issued.expires_us=history.now+250000;assert(!r2_auth_encode(packet,&issued,history.peer[3].tx_key));r2_network_sent(&history,false,3,packet);}
 struct r2_auth_record delayed_ack={.kind=R2_AUTH_ACK,.sender=3,.target=2,.root=2,.boot=n[3].boot,.coordinator_boot=history.boot,.challenge=history.peer[3].own_nonce,.sequence=history.peer[3].sequence+1,.decision=history.publication,.root_boot=history.boot,.source_epoch=history.candidate.generation,.config_epoch=R2_AUTH_CONFIG,.source_second=first_sequence,.created_us=n[3].now+100000,.evidence_us=n[3].now+100000,.expires_us=n[3].now+350000};
 assert(!r2_auth_encode(packet,&delayed_ack,history.peer[3].rx_key));
 assert(!r2_network_receive(&history,3,packet,r2_auth_size(packet[1]),base+220000,base+220000));
 assert(history.peer[3].acked_permit==base+1400000);
 struct r2_network storage=n[2];storage.service_until=storage.now+1400000;
 assert(!r2_network_storage_ready(&storage));
 for(unsigned p=0;p<4;p++)if(p!=storage.self){storage.peer[p].acked_publication=storage.publication;storage.peer[p].acked_permit=storage.service_until;}
 assert(r2_network_storage_ready(&storage));
 storage.peer[3].acked_permit--;assert(!r2_network_storage_ready(&storage));
 storage.peer[3].connected=false;assert(r2_network_storage_ready(&storage));
 storage.service_until=storage.now+999999;assert(!r2_network_storage_ready(&storage));
 storage=n[3];storage.service_until=storage.now+1400000;
 storage.peer[storage.choice].response_pending=true;storage.peer[storage.choice].response.kind=R2_AUTH_ACK;
 assert(!r2_network_storage_ready(&storage));
 storage.peer[storage.choice].response_pending=false;assert(r2_network_storage_ready(&storage));
 storage.service_until=storage.now+999999;assert(!r2_network_storage_ready(&storage));
 struct r2_network standby=n[1];assert(standby.coordinator==1&&standby.hot_ready);
 struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
 source.last_pair_edge=source.last_rmc=standby.now;source.last_pair_utc=1791417845000000LL;
 standby.timing_valid=false;r2_network_tick(&standby,&source,standby.now+1);
 assert(!standby.hot_ready&&!standby.management_committed&&standby.follower_valid);
 struct r2_network copy=n[3];uint64_t now=copy.now,permit=copy.service_until;
 r2_network_tick(&copy,&(struct r2_clock){0},permit);assert(!copy.follower_valid&&!copy.ready);
 copy=n[3];struct r2_auth_record expired={.kind=R2_AUTH_SERVICE,.sender=2,.target=3,.root=2,.flags=5,.boot=n[2].boot,.coordinator_boot=n[2].boot,.challenge=copy.peer[2].own_nonce,.sequence=copy.peer[2].sequence+1,.decision=copy.publication,.root_boot=n[2].boot,.source_epoch=1,.config_epoch=R2_AUTH_CONFIG,.source_second=1791417844,.created_us=n[2].now-500000,.evidence_us=n[2].now-500000,.expires_us=n[2].now-250000,.permit_until_us=n[2].now+500000};
 uint8_t stale[R2_AUTH_BYTES];assert(!r2_auth_encode(stale,&expired,copy.peer[2].rx_key));assert(r2_network_receive(&copy,2,stale,r2_auth_size(stale[1]),now,now));assert(copy.service_until==permit);
 /* Echo age<500ms cannot renew a HELLO beyond its original250ms TTL. */
 for(unsigned negative=0;negative<2;negative++){
  struct r2_network receiver=n[3],tx=n[2];tx.peer[3].next_hello=0;tx.peer[3].next_service=UINT64_MAX;
  if(negative)tx.candidate.available=false;
  uint8_t target,bytes[R2_AUTH_BYTES];bool is_hello;
  assert(r2_network_next_ready(&tx,8,&target,bytes,&is_hello)&&is_hello&&target==3);
  struct r2_net_peer before=receiver.peer[2];uint64_t later=receiver.now+300000;
  struct r2_auth_record sent;assert(!r2_auth_decode(&sent,bytes,r2_auth_size(bytes[1]),before.rx_key));
  assert(later>=sent.source_second&&later-sent.source_second<500000);
  assert(r2_network_receive(&receiver,2,bytes,r2_auth_size(bytes[1]),later,later));
  assert(!memcmp(&before,&receiver.peer[2],sizeof before));
 }
 struct r2_network sender=n[2];sender.candidate.available=false;sender.peer[3].next_hello=0;sender.peer[3].next_service=UINT64_MAX;
 uint8_t recipient;bool hello;assert(r2_network_next_ready(&sender,8,&recipient,stale,&hello)&&hello&&recipient==3);
 assert(!r2_network_receive(&copy,2,stale,r2_auth_size(stale[1]),now,now));assert(!copy.peer[2].candidate_until);
 copy=n[3];
 r2_network_tick(&copy,&(struct r2_clock){0},now-1);assert(copy.stopped&&!copy.ready);
 copy=n[3];copy.next_nonce=UINT64_MAX;r2_network_link(&copy,0,false,now);r2_network_link(&copy,0,true,now);assert(copy.stopped);
 n[0].peer[2].next_hello=n[0].peer[3].next_hello=0;uint8_t peer,wire[R2_AUTH_BYTES];bool h;
 assert(r2_network_next_ready(&n[0],1u<<3,&peer,wire,&h)&&peer==3&&h);
 assert(r2_network_receive(&n[3],0,wire,r2_auth_size(wire[1]),n[3].now,n[3].now+50000));
 puts("PASS Config5 HELLO original TTL/unchanged peer on stale positive and negative; codec tamper; 60s admission; client exclusion; GM/follower continuity through coordinator loss/reboot; real GM loss and recovery admission");
}
