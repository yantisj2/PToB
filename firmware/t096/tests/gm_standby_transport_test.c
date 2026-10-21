/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual native transport + JOIN owner, public controller callbacks faked.
 * Measures quiet COLD work, one selected control slot and ordinary RC fairness.
 */
#define R2_TIMING_EVENTS_TEST 1
#include "ble_transport_fake.h"
static unsigned inventory_events,inventory_consumed;static int inventory_flags;static uint32_t inventory_counts,inventory_age;static uint64_t inventory_selected;
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b){
 (void)kind;if(detail==24){++inventory_events;inventory_consumed+=(result&1)!=0;inventory_flags=result;inventory_selected=peer;inventory_counts=a;inventory_age=b;}
}
static unsigned initiations;
static bool create_pending,create_callback;static uint64_t create_delay,create_return_delay;
static struct bt_conn connection;
static int sys_csrand_get(void *out,size_t size){static uint64_t nonce=1000;assert(size==8);uint64_t value=++nonce;memcpy(out,&value,8);return 0;}
#include "../protocol/ble_transport.c"
#include "../src/group_admission.c"
#include <stdio.h>
#include "gm_standby_handoff.h"
static int bt_conn_le_create(const bt_addr_le_t *a,const void *b,const void *c,struct bt_conn **out){
 ++initiations;if(!create_pending)return -EBUSY;
 connection=(struct bt_conn){.address=*a,.refs=1};*out=&connection;fake_time+=create_delay;if(create_callback)connected(&connection,0);fake_time+=create_return_delay;return 0;
}
static struct r2_network n;
static struct r2_group_client accepted_anchor;
static void reset(void)
{
 memset(peers,0,sizeof peers);memset(candidates,0,sizeof candidates);candidate_since=0;memset(&gatt_request,0,sizeof gatt_request);
 self=0;nodes=5;local_identity=333;group_id=7;group_epoch=1;group_client=true;client_window=false;bootstrap_needed=false;fake_early_tx=true;
 stop_error=0;owner_deadline=0;transport_owner=NULL;fake_current=NULL;fake_defer=false;recent_admission=recent_admission_until=0;stopping=scanning=advertising=precision_discovery=sync_acquiring=false;field_profile=false;admission_peer=4;
 r2_group_admission_reset();r2_ble_gm5_policy(true,0,false);
 assert(!r2_network_init(&n,0,2,333,33,44,40000,70000000));n.group=(struct r2_group_config){.id=7,.epoch=1,.psk={1}};n.gm5.enabled=true;n.capabilities=3;strcpy(n.name,"Synthetic");
 n.admission.eligible=true;n.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=90000000};n.gm_mask=n.gnss_mask=1;n.gm5.active=111;n.gm5.active_boot=11;n.gm5.active_until=190000000;n.gm5.next_poll=100000000;
 fake_time=n.now;create_pending=create_callback=false;create_delay=create_return_delay=0;
 for(unsigned p=0;p<5;p++)for(unsigned s=0;s<2;s++)peers[p].completion[s]=(struct tx_context){&peers[p],s};
}
static void ready_guest(unsigned capabilities)
{
 (void)capabilities;uint8_t derived[16];r2_group_pair_key(derived,&n.group,111,111);derived[5]|=0xc0;
 connection=(struct bt_conn){.address={.type=BT_ADDR_LE_RANDOM},.refs=1,.role=BT_CONN_ROLE_PERIPHERAL};memcpy(connection.address.a.val,derived,6);
 peers[4].identity=111;peers[4].address=connection.address;peers[4].conn=&connection;peers[4].admission_server=true;peers[4].disconnect_requested=peers[4].release=false;peers[4].provisional_until=n.now+5000000;
 atomic_set(&peers[4].connected,1);atomic_set(&peers[4].mtu_done,1);atomic_set(&peers[4].discovered,1);atomic_set(&peers[4].att_mtu,226);peers[4].handle=1;
}
static struct r2_join_record authenticate(unsigned caps)
{
 ready_guest(caps);
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.sender=111,.receiver=333,.sender_boot=11,.client_nonce=77,.sequence=1,.created_us=n.now,.capabilities=caps,.name="Remote"};
 struct r2_ble_event e={.peer=4,.at=n.now,.length=R2_GROUP_JOIN_BYTES};assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);struct r2_join_record response;
 assert(!r2_group_join_decode(&response,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(response.kind==R2_JOIN_CHALLENGE&&!response.control);
 r.kind=R2_JOIN_PROOF;r.receiver_boot=33;r.server_nonce=response.server_nonce;r.echo_us=response.created_us;r.sequence=2;r.created_us=++fake_time;e.at=r.created_us;
 assert(!r2_group_join_encode(e.data,&r,&n.group));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);n.now=++fake_time;r2_group_admission_tick(&n,n.now);
 assert(!r2_group_join_decode(&response,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(response.kind==R2_JOIN_ACCEPT&&!response.control&&exchange.accepted);
 return response;
}
static struct r2_join_exchange authenticate_client(bool foreign_time)
{
 ready_guest(3);peers[4].admission_server=false;connection.role=0;
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);struct r2_join_record request;
 assert(!r2_group_join_decode(&request,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(request.kind==R2_JOIN_REQUEST&&!exchange.server);
 struct r2_join_record r={.kind=R2_JOIN_CHALLENGE,.sender=111,.receiver=333,.sender_boot=11,.receiver_boot=33,.client_nonce=request.client_nonce,.server_nonce=77,.sequence=1,.created_us=++fake_time,.echo_us=request.created_us,.capabilities=3,.name="Remote"};
 struct r2_ble_event e={.peer=4,.at=fake_time,.length=R2_GROUP_JOIN_BYTES};assert(!r2_group_join_encode(e.data,&r,&n.group));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);struct r2_join_record proof;
 assert(!r2_group_join_decode(&proof,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(proof.kind==R2_JOIN_PROOF);
 r.kind=R2_JOIN_ACCEPT;r.sequence=2;r.created_us=e.at=++fake_time;r.echo_us=proof.created_us;
 if(foreign_time){r.source_flags=1;r.root=111;r.root_boot=11;r.source_epoch=1;r.publication=1;r.utc_us=INT64_C(1791417600000000);r.remaining_us=1500000;r.source_bound_us=40000;}
 assert(!r2_group_join_encode(e.data,&r,&n.group));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);
 accepted_anchor=client;
 if(foreign_time){struct r2_group_client public;r2_group_admission_client(&public);assert(public.source&&public.root==111);if(n.gm5.role==R2_GM5_ACTIVE)assert(!public.passive&&n.local_gm&&n.ready&&n.gm5.active==n.identity&&!n.gm5.selected);}
 assert(exchange.accepted&&!exchange.control&&gm5_send);n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(!gm5_send&&gm5_phase==0);
 return (struct r2_join_exchange){.self=111,.peer=333,.boot=11,.peer_boot=33,.nonce=77,.remote_nonce=request.client_nonce,.until=exchange.until,.accepted=true,.server=true};
}
static void authenticated_withdrawal(uint64_t at)
{
 n.peer[1]=(struct r2_net_peer){.identity=111,.boot=11,.connected=true,.authenticated=true,.map_valid=true,.member=true,.seen=at,.alive=at,.map_anchor=at,.nonce=88,.own_nonce=99,.candidate_epoch=1,.candidate_until=at+1500000};
 n.gm_mask=n.gnss_mask=3;
 struct r2_auth_record r={.kind=R2_AUTH_HELLO,.sender=0,.target=1,.root=255,.config_epoch=5,.boot=11,.coordinator_boot=11,.root_boot=33,.challenge=88,.decision=99,.sequence=1,.created_us=at,.evidence_us=at,.expires_us=at+250000};
 uint64_t ids[4]={111,333};uint8_t wire[R2_GROUP_CONTROL_MAX];assert(!r2_group_control_encode5(wire,&r,&n.group,ids,"Remote",3));
 assert(!r2_network_receive(&n,1,wire,r2_control_wire_size(wire),at,at));assert(n.peer[1].source_withdrawn);
}
static void inventory_snapshot_tests(void)
{
 reset();gm5_inventory_due=false;unsigned before=inventory_events;
 fake_time=71000000;gm5_selected=700;admission_parent=700;candidate_since=fake_time-40000;
 candidates[0]=(struct discovery_candidate){.identity=700,.seen=fake_time-10000};
 candidates[1]=(struct discovery_candidate){.identity=800,.seen=fake_time-20000};
 candidates[2]=(struct discovery_candidate){.identity=900,.seen=fake_time-200000};
 r2_ble_gm5_policy(true,700,true);r2_ble_gm5_policy(true,700,false);
 r2_ble_gm5_policy(true,700,false); /* no repeated expiry record */
#if defined(R2_CONTROLLER_DIAGNOSTICS)
 assert(inventory_events==before+1&&!(inventory_flags&1)&&inventory_selected==700);
 assert(inventory_counts==(3u|(2u<<8)|(2u<<16)|(1u<<24))&&inventory_age==10000);
 assert((inventory_flags&(1u<<12))&&(inventory_flags&(1u<<14)));
 assert((inventory_flags&0x1f8000)==((1u<<15)|(1u<<16)|(1u<<17)|(1u<<20)));
#else
 assert(inventory_events==before);
#endif
 candidates[0].seen=fake_time-250000;r2_ble_gm5_policy(true,700,true);r2_ble_gm5_policy(true,700,false);
#if defined(R2_CONTROLLER_DIAGNOSTICS)
 assert(inventory_age==250000&&(inventory_flags&(1u<<15))&&!(inventory_flags&(1u<<16)));
#endif
 candidates[0].identity=0;r2_ble_gm5_policy(true,700,true);r2_ble_gm5_policy(true,700,false);
#if defined(R2_CONTROLLER_DIAGNOSTICS)
 assert(inventory_age==UINT32_MAX&&!(inventory_flags&(1u<<15)));
#endif
 before=inventory_events;r2_ble_gm5_policy(false,0,true);r2_ble_gm5_policy(false,0,false);
 assert(inventory_events==before);admission_parent=0;
 puts("PASS inventory opportunity diagnostic: original due edge once, packed current candidate gates, ordinary/RC image emits none");
}
static void missed_active_polls(bool pending)
{
 reset();create_pending=pending;n.admission.boot_at=n.admission.stable_at=0;
 n.admission.observing=true;n.admission.source_epoch=1;n.admission.last_expiry=n.now+1500000;
 n.gm5.role=R2_GM5_ACTIVE;n.gm5.active=n.identity;n.gm5.active_boot=n.boot;
 n.gm5.next_poll=n.now;n.local_gm=n.ready=true;n.choice=0;n.root_boot=n.boot;
 uint64_t due=n.now;unsigned before=initiations,closed=disconnects,events_before=inventory_events,consumed_before=inventory_consumed;
 bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM,.a={{7}}};
 for(unsigned round=0;round<4;round++){
  unsigned calls=initiations;bool callback=false;uint64_t started=0,original_until=0;
  for(uint64_t at=due;at<=due+1000000;at+=5000){
   fake_time=at;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
   source.last_pair_edge=source.last_rmc=at;source.last_pair_utc=INT64_C(1791417600000000)+at;
   r2_network_tick(&n,&source,at);r2_group_admission_tick(&n,at);
   r2_group_admission_gm5_policy(&n,at);bool open=r2_gm5_poll_due(&n,at);r2_ble_client_window(true);
   if(open)candidate_seen(&address,700,false,-30,at);
   r2_ble_poll(at);
   if(initiations!=calls&&!started){
    started=at;assert(initiations==calls+1&&at<=due+40000&&(round||at>=due+40000));
    assert(candidates[0].cooldown==at&&candidates[0].consumed==at&&r2_ble_gm5_attempt_at()==at);original_until=peers[4].provisional_until;
    if(pending)assert(peers[4].connect_until==at+100000&&connection.refs==1);
   }
   if(pending&&started){
    if(at<started+100000)assert(disconnects==closed+round);
    if(at==started+100000)assert(disconnects==closed+round+1&&peers[4].disconnect_requested&&peers[4].connect_until==fake_time+R2_CALLBACK_RETIRE_US);
    if(at>=started+100000&&at<started+150000)assert(peers[4].conn==&connection&&connection.refs==1&&peers[4].provisional_until==original_until);
    if(at==started+150000){callbacks.connected(&connection,2);callback=true;}
    if(at>started+150000)assert(callback&&!peers[4].conn&&connection.refs==0);
   }
   assert(initiations<=calls+1); /* One central opportunity, including refused create and late callbacks. */
   assert(n.local_gm&&n.ready&&n.admission.eligible&&n.gm5.role==R2_GM5_ACTIVE&&!n.gm5.selected&&!n.hot_ready&&!n.standby_authorized);
   if(started&&at>started)assert(n.gm5.next_poll==started+5000000);
  }
  assert(started&&initiations==calls+1&&!r2_gm5_poll_due(&n,n.now));
  /* The attempt consumed its original opportunity. ACTIVE's self identity
   * is not a remote incumbent; completion cannot move the fixed5s retry. */
  assert(n.gm5.next_poll==started+5000000);
  uint64_t next=n.gm5.next_poll;
  for(uint64_t at=n.now+5000;at<next;at+=5000){
   fake_time=at;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
   source.last_pair_edge=source.last_rmc=at;source.last_pair_utc=INT64_C(1791417600000000)+at;
   r2_network_tick(&n,&source,at);r2_group_admission_tick(&n,at);
   r2_group_admission_gm5_policy(&n,at);r2_ble_poll(at);
   assert(n.gm5.next_poll==next&&initiations==calls+1&&n.local_gm&&n.ready);
  }
  due=next;
 }
 assert(initiations==before+4);create_pending=false;
#if defined(R2_CONTROLLER_DIAGNOSTICS)
 assert(inventory_events==events_before+4&&inventory_consumed==consumed_before+4);
#else
 assert(inventory_events==events_before&&inventory_consumed==consumed_before);
#endif
 puts(pending?"PASS four actual ACTIVE missed poll windows:100ms pending create cancellation, delayed callback drain,one consumed attempt, fixed5s next opportunities and continuous source service":
              "PASS four actual ACTIVE missed poll windows: native create EBUSY,one consumed attempt, fixed5s next opportunities and continuous source service");
}
static void admission_boundary_tests(void)
{
 for(unsigned race=0;race<3;race++){
  reset();client_phase=900000;client_resume=0;bootstrap_needed=true;bootstrap_urgent=false;
  field_admission_allowed=true;n.gm5.role=R2_GM5_COLD;n.gm5.next_poll=69995000;
  bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM,.a={{7}}};
  fake_time=70940000;candidate_seen(&address,700,false,-30,fake_time);
  fake_time=n.now=70980000;create_pending=true;r2_group_admission_gm5_policy(&n,n.now);r2_ble_poll(n.now);
  assert(peers[4].conn==&connection&&peers[4].connect_until==71080000);
  unsigned before=disconnects;uint64_t original=peers[4].provisional_until;
  fake_time=n.now=70995000;
  if(race==2)connected(&connection,0);
  r2_group_admission_gm5_policy(&n,n.now);assert(!client_window);
  if(race==1)connected(&connection,0);
  r2_ble_poll(n.now);assert(disconnects==before&&peers[4].provisional_until==original);
  if(!race){
   fake_time=n.now=71079999;r2_group_admission_gm5_policy(&n,n.now);r2_ble_poll(n.now);assert(disconnects==before);
   fake_time=n.now=71080000;r2_ble_poll(n.now);assert(disconnects>before&&peers[4].conn==&connection&&connection.refs==1);
   fake_time=n.now=71080001;connected(&connection,0);assert(peers[4].release);r2_ble_poll(n.now);
   assert(r2_ble_admission_promote()==-ETIMEDOUT&&peers[4].conn==&connection);
  }else{
   fake_time=n.now=original-1;r2_ble_poll(n.now);assert(disconnects==before);
   fake_time=n.now=original;r2_ble_poll(n.now);assert(disconnects>before&&peers[4].conn==&connection);
  }
  callbacks.disconnected(&connection,19);fake_time++;r2_ble_poll(fake_time);
  assert(!peers[4].conn&&connection.refs==0);create_pending=false;
 }
 for(int boundary=-1;boundary<=1;boundary++){
  reset();client_phase=900000;client_resume=0;bootstrap_needed=true;bootstrap_urgent=false;
  n.gm5.next_poll=n.now;n.gm5.role=R2_GM5_COLD;client_window=true;
  bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM,.a={{7}}};
  fake_time=n.now=70940000;candidate_seen(&address,700,false,-30,fake_time);
  fake_time=n.now=70980000;create_pending=create_callback=true;create_delay=100000+boundary;create_return_delay=boundary<0?2:0;
  r2_ble_gm5_policy(true,0,true);unsigned before=disconnects;r2_ble_poll(n.now);
  assert(peers[4].conn==&connection&&connection.refs==1);
  assert(peers[4].release==(boundary>=0));assert((disconnects>before)==(boundary>=0));
  if(boundary>=0)assert(r2_ble_admission_promote()==-ETIMEDOUT);
 }
 reset();ready_guest(3);peers[4].mtu.func=NULL;atomic_clear(&peers[4].att_mtu);
 transport_owner=(struct k_thread *)&n;fake_current=&n;fake_defer=true;uint64_t original=n.now+100000;peers[4].provisional_until=original;
 r2_ble_client_window(true);unsigned before=disconnects;r2_ble_poll(n.now);
 assert(fake_time==original&&gatt_request.pending&&disconnects>before&&peers[4].conn==&connection);
 fake_dispatch();fake_current=NULL;fake_defer=false;
 assert(r2_ble_admission_promote()==-ETIMEDOUT);
 for(int boundary=-1;boundary<=1;boundary++){
  reset();ready_guest(3);peers[4].admission_server=false;connection.role=0;
  uint64_t original=n.now+5000000;peers[4].provisional_until=original;
  fake_time=n.now=original-40000;r2_group_admission_tick(&n,n.now);
  assert(active&&exchange.until==original); /* Delayed GATT cannot mint5s. */
  fake_time=n.now=original+boundary;r2_group_admission_tick(&n,n.now);
  assert(active==(boundary<0)&&!n.peer[1].identity);
  if(boundary>=0)assert(peers[4].release&&peers[4].conn==&connection);
 }
 reset();ready_guest(3);n.gm5.attempt_until=n.now+30000;
 r2_group_admission_tick(&n,n.now);assert(exchange.until==n.gm5.attempt_until);
 for(int edge=0;edge<=1;edge++){
  reset();ready_guest(3);n.gm5.role=R2_GM5_ACTIVE;
  uint64_t phase=n.now+30000,resource=peers[4].provisional_until;
  n.gm5.attempt_until=phase;n.gm5.issued_until=phase+R2_GM5_GRANT_US;
  n.now=phase-1;fake_time=phase+edge; /* Network snapshot precedes actual caller time. */
  r2_group_admission_tick(&n,fake_time);
  assert(active&&exchange.until==resource&&exchange.until>fake_time);
  assert(n.gm5.attempt_until==phase); /* Original phase is never rolled. */
 }
 puts("PASS actual service policy: pending/callback before/after window sampling,100ms -1/at/+1, late callback refusal, original5s -1/at/+1, delayed JOIN and phase clip, callback/ref drain");
}
static void abandoned_phase_tests(void)
{
 for(unsigned cause=0;cause<2;cause++){
  reset();n.gm5.role=cause?R2_GM5_HOT:R2_GM5_ACQUIRING;
  n.gm5.selected=n.identity;n.gm5.attempt_started=n.now-10000000;n.gm5.attempt_until=n.now-1;
  n.gm5.active_until=cause?n.now+120000000:n.now-1;
  struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
  source.last_pair_edge=source.last_rmc=n.now;source.last_pair_utc=INT64_C(1791417600000000)+n.now;
  struct r2_gm_coordination constraint={.root=999,.boot=99,.epoch=1,.until=n.now+120000000};
  r2_network_tick_coordinated(&n,&source,n.now,cause?&constraint:NULL);
  assert(n.gm5.role==R2_GM5_COLD&&!n.gm5.selected&&!n.gm5.attempt_until&&!n.gm5.attempt_started);
  (void)authenticate(3);
  assert(active&&exchange.accepted&&exchange.until==peers[4].provisional_until&&exchange.until>n.now);
 }
 puts("PASS expired prior phase abandoned by incumbent expiry/foreign constraint cannot poison fresh unrelated COLD inventory JOIN/STATUS resource lifetime");
}
static void write_handoff(const char *path)
{
 reset();struct r2_join_exchange server=authenticate_client(true);
 assert(client.source&&client.passive&&client.at==accepted_anchor.at&&client.until==accepted_anchor.until);
 struct r2_network a;assert(!r2_network_init(&a,0,2,111,11,99,40000,n.now));a.group=n.group;a.gm5.enabled=true;a.gm5.role=R2_GM5_ACTIVE;a.gm5.active=111;a.gm5.active_boot=11;a.gm5.selected=333;a.admission.eligible=true;a.candidate.generation=1;
 struct r2_ble_event e={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};a.now=n.now=fake_time;
 assert(!r2_gm5_status_encode(&a,&server,1,e.data));r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(promote&&n.gm5.role==R2_GM5_ACQUIRING);
 fake_time+=50001;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(n.peer[1].identity==111&&peers[1].conn==&connection&&!peers[4].conn);
 n.admission.boot_at=n.admission.stable_at=0;n.admission.observing=true;n.admission.source_epoch=1;n.admission.last_expiry=n.now+1500000;
 struct r2_clock own={0};own.view.source_available=true;own.view.source_epoch=1;own.last_pair_edge=own.last_rmc=n.now;own.last_pair_utc=INT64_C(1791417600000000)+n.now;
 r2_network_tick(&n,&own,n.now);struct v12_dc_snapshot authority;r2_network_snapshot(&n,&authority);
 assert(n.choice==1&&!authority.relative_precision_permitted&&!n.ready&&!n.hot_ready&&!n.standby_authorized);
 struct gm5_handoff_fixture f={.accepted=accepted_anchor,.promoted=client,.at=n.now,
  .selected={.root=authority.elected_root,.boot=authority.root_boot,.source_epoch=authority.coarse.generation,.publication_epoch=n.publication}};
 assert(!f.promoted.passive&&f.promoted.source&&f.promoted.at==f.accepted.at&&f.promoted.until==f.accepted.until);
 FILE *file=fopen(path,"wb");assert(file&&fwrite(&f,sizeof f,1,file)==1&&!fclose(file));
 puts("PASS actual JOIN ACCEPT -> COLD tick -> STATUS1/2 ->50ms promotion -> network selected scope, original anchor retained for independent PT owner");
}
static void late_appointment(void)
{
 reset();struct r2_join_exchange server=authenticate_client(false);
 struct r2_network a;assert(!r2_network_init(&a,0,2,111,11,99,40000,n.now));a.group=n.group;a.gm5.enabled=true;a.gm5.role=R2_GM5_ACTIVE;a.gm5.active=111;a.gm5.active_boot=11;a.local_gm=a.ready=true;a.choice=0;a.root_boot=11;a.publication=1;
 a.admission=(struct r2_source_admission){.boot_at=0,.stable_at=0,.source_epoch=1,.last_expiry=n.now+1500000,.observing=true,.eligible=true};a.gm_mask=a.gnss_mask=1;
 a.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=n.now+1500000};a.gm5.next_attempt=n.now+1000000;
 uint64_t gate=a.gm5.next_attempt;assert(!r2_gm5_status_receive(&a,&server,0,peers[4].tx[0],R2_GM5_STATUS_BYTES,n.now)&&!a.gm5.selected);
 struct r2_ble_event e={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};a.now=n.now=fake_time;
 assert(!r2_gm5_status_encode(&a,&server,1,e.data)&&!r2_group_get(e.data+92,8));r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);uint64_t due=n.gm5.next_poll;assert(gm5_done&&!promote&&n.gm5.role==R2_GM5_COLD);
 fake_time+=50001;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(!active);callbacks.disconnected(&connection,19);
 fake_time=gate+1;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=fake_time;source.last_pair_utc=INT64_C(1791417600000000)+fake_time;
 r2_network_tick(&a,&source,fake_time);assert(a.gm5.selected==333&&a.gm5.contacting);uint64_t contact_until=a.gm5.attempt_until;
 for(uint64_t at=fake_time;at<due;at+=5000){fake_time=at;r2_ble_gm5_policy(true,0,false);r2_ble_client_window(false);r2_ble_poll(at);assert(!scanning&&!advertising&&!sync_acquiring&&!precision_discovery&&a.gm5.attempt_until==contact_until);}
 fake_time=n.now=due;server=authenticate_client(false);a.now=n.now;a.candidate.valid_until_us=a.now+1500000;
 assert(!r2_gm5_status_receive(&a,&server,0,peers[4].tx[0],R2_GM5_STATUS_BYTES,a.now)&&a.gm5.contacting);
 e.at=a.now=n.now=++fake_time;assert(!r2_gm5_status_encode(&a,&server,1,e.data)&&!a.gm5.contacting&&a.gm5.attempt_until==a.now+R2_GM5_ACQUIRE_US);
 assert(a.gm5.attempt_until>contact_until+R2_GM5_STABLE_US);r2_group_admission_receive(&n,&e,n.now);n.now=++fake_time;r2_group_admission_tick(&n,n.now);
 assert(promote&&n.gm5.role==R2_GM5_ACQUIRING&&!n.hot_ready&&!n.standby_authorized);
 puts("PASS actual failed-attempt gate just after STATUS closes: COLD stays quiet until original30s poll; fixed CONTACT survives, matching reply starts full original45s PA acquisition once");
}
static int stranded_control(void){
 reset();n.gm5.role=R2_GM5_COLD;n.gm5.active=0;n.gm5.active_boot=0;n.gm5.next_poll=n.now;
 struct peer *old=&peers[1];old->identity=111;old->conn=&connection;old->incarnation=3;
 connection.refs=1;old->authenticated=true;atomic_set(&old->connected,1);atomic_set(&old->tx_busy,3);
 bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM,.a={{7}}};candidate_seen(&address,111,false,-30,n.now);
 r2_group_admission_gm5_policy(&n,n.now);r2_ble_client_window(true);r2_ble_poll(fake_time);
 assert(old->disconnect_requested&&old->conn==&connection&&!peers[4].identity);
 uint64_t until=old->connect_until;unsigned requested=disconnects;
 for(unsigned tick=1;tick<20;tick++){
  fake_time+=100000;n.now=fake_time;candidate_seen(&address,111,false,-30,n.now);
  r2_group_admission_gm5_policy(&n,n.now);r2_ble_poll(fake_time);
  assert(old->conn==&connection&&old->identity==111&&atomic_get(&old->tx_busy)==3);
  assert(!peers[4].identity&&!r2_ble_stop_error()&&old->connect_until==until&&disconnects==requested);
 }
 fake_time=n.now=until;r2_ble_poll(fake_time);
 assert(r2_ble_stop_error()==-ETIMEDOUT&&old->conn==&connection&&connection.refs==1&&atomic_get(&old->tx_busy)==3);
 puts("PASS actual native+G5 COLD inventory blocked by oldincarnation/TX3; original retirement deadline produces terminal error while retaining all unsafe ownership");
 return r2_ble_stop_error();
}
static void guest_lifecycle_tests(void) {
 for(unsigned mode=0;mode<5;mode++) {
  reset();uint64_t start=fake_time;
  n.peer[1]=(struct r2_net_peer){.identity=111,.boot=11,.authenticated=true,.map_valid=true,.candidate_epoch=7,.candidate_until=start+90000000};
  n.gm5.issued_until=start+90000000;n.gm5.generation=9;
  struct r2_network remote_cache=n;
  connection=(struct bt_conn){.address={.type=BT_ADDR_LE_RANDOM,.a={{1,2,3,4,5,6}}},.role=BT_CONN_ROLE_PERIPHERAL,.state=BT_CONN_STATE_CONNECTED,.interval=6,.timeout=50};
  callbacks.connected(&connection,0);assert(connection.refs==1&&peers[4].identity==0);
  att_mtu_updated(&connection,226,226);r2_ble_poll(fake_time);
  assert(!peers[4].disconnect_requested&&peers[4].provisional_until==start+5000000);
  fake_time=start+5000000;r2_ble_poll(fake_time);
  assert(peers[4].disconnect_requested&&connection.refs==1);
  unsigned requested=disconnects;uint64_t until=peers[4].connect_until;
  assert(until==fake_time+5500000);
  if(mode==4)atomic_set(&peers[4].tx_busy,1);
  if(mode==1||mode==3||mode==4){fake_time=until-1;callbacks.disconnected(&connection,19);if(mode==3)callbacks.disconnected(&connection,19);r2_ble_poll(fake_time);}
  fake_time=until;r2_ble_poll(fake_time);
  assert(disconnects==requested);assert(!memcmp(&n,&remote_cache,sizeof n));
  if(mode==1||mode==3){assert(!stop_error&&!peers[4].conn&&!connection.refs);}
  else {assert(stop_error==-ETIMEDOUT&&peers[4].conn==&connection&&connection.refs==1);
   /* A late real callback may drain storage, but cannot erase terminal fault. */
   if(mode==2){callbacks.disconnected(&connection,19);r2_ble_poll(++fake_time);assert(!peers[4].conn&&!connection.refs&&stop_error==-ETIMEDOUT);}
   if(mode==4){tx_done(&connection,&peers[4].completion[0]);r2_ble_poll(++fake_time);assert(!peers[4].conn&&!connection.refs&&stop_error==-ETIMEDOUT);}
  }
  printf("PASS unknown incoming negotiated500ms/full5sJOIN/oneclose mode%u; callback/TX-bound ownership; unrelated peer/grant cache retained\n",mode);
 }
}

static struct r2_join_record rejoin_old_proof;
static void rejoin_inject(struct r2_join_record *r) {
 struct r2_ble_event e={.peer=4,.at=++fake_time,.length=R2_GROUP_JOIN_BYTES};
 n.now=fake_time;r->created_us=fake_time;
 assert(!r2_group_join_encode(e.data,r,&n.group));r2_group_admission_receive(&n,&e,n.now);
}
static void rejoin_tick(void){n.now=++fake_time;r2_group_admission_tick(&n,n.now);}
static void rejoin_arrive(unsigned boot) {
 uint8_t a[16];r2_group_pair_key(a,&n.group,111,111);a[5]|=0xc0;
 connection=(struct bt_conn){.address={.type=BT_ADDR_LE_RANDOM},.role=BT_CONN_ROLE_PERIPHERAL,.state=BT_CONN_STATE_CONNECTED,.interval=6,.timeout=50};memcpy(connection.address.a.val,a,6);
 callbacks.connected(&connection,0);r2_ble_poll(++fake_time);assert(peers[4].mtu.func);
 att_mtu_updated(&connection,226,226);mtu_done(&connection,0,&peers[4].mtu);
 r2_ble_poll(++fake_time);assert(peers[4].lookup.func);
 assert(located(&connection,0,&peers[4].lookup,discovery_marker,sizeof discovery_marker)==BT_GATT_ITER_STOP);
 assert(r2_ble_ready(4));rejoin_tick();
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.sender=111,.receiver=333,.sender_boot=boot,.client_nonce=700+boot,.sequence=1,.capabilities=3,.name="Rebooted"};
 rejoin_inject(&r);rejoin_tick();assert(active&&exchange.peer_boot==boot&&!exchange.accepted);
 struct r2_join_record challenge;assert(!r2_group_join_decode(&challenge,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(challenge.kind==R2_JOIN_CHALLENGE);
 r.kind=R2_JOIN_PROOF;r.receiver_boot=33;r.server_nonce=challenge.server_nonce;r.echo_us=challenge.created_us;r.sequence=2;
 if(rejoin_old_proof.sender_boot&&rejoin_old_proof.sender_boot!=boot){
  rejoin_inject(&rejoin_old_proof);rejoin_inject(&rejoin_old_proof);
  assert(!exchange.proved&&!exchange.accepted&&exchange.peer_boot==boot);
 }
 rejoin_old_proof=r;
 rejoin_inject(&r);rejoin_tick();assert(exchange.accepted&&exchange.peer_boot==boot);
 struct r2_join_record accepted;assert(!r2_group_join_decode(&accepted,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(accepted.kind==R2_JOIN_ACCEPT);
}
static void retained_rejoin_tests(void) {
 reset();n.gm5.role=R2_GM5_ACTIVE;n.gm5.active=333;n.gm5.active_boot=33;n.gm5.selected=222;n.local_gm=n.ready=true;n.gm5.issued_until=n.now+90000000;n.gm5.generation=9;
 n.peer[1]=(struct r2_net_peer){.identity=222,.boot=22,.connected=true,.authenticated=true,.map_valid=true,.candidate_epoch=7,.candidate_until=n.now+90000000};
 n.gm5.candidate[0]=(struct r2_gm5_candidate){.identity=111,.boot=11,.epoch=9,.until=n.now+120000000,.withdraw_created=n.now-10,.withdraw_epoch=9};
 struct r2_net_peer healthy=n.peer[1];uint64_t obligation=n.gm5.issued_until;
 /* Pre-reset half-JOIN and old proof retained by this owner. */
 rejoin_arrive(11);struct r2_join_exchange old=exchange;
 r2_ble_admission_release();assert(peers[4].conn&&connection.refs==1);rejoin_tick();assert(peers[4].conn&&connection.refs==1); /* no fabricated completion */
 callbacks.disconnected(&connection,19);r2_ble_poll(++fake_time);rejoin_tick();assert(!active&&!peers[4].conn&&!connection.refs);
 /* Remote alone restarts. Local admission/client/candidate/healthy pair retained. */
 rejoin_arrive(12);assert(!memcmp(&healthy,&n.peer[1],sizeof healthy));
 assert(n.gm5.selected==222&&n.gm5.issued_until==obligation&&n.local_gm);
 struct r2_network cold;assert(!r2_network_init(&cold,0,2,111,12,999,40000,fake_time));cold.group=n.group;cold.gm5.enabled=true;cold.capabilities=3;cold.admission.eligible=true;cold.candidate.available=true;cold.candidate.generation=1;cold.candidate.valid_until_us=fake_time+1500000;
 struct r2_join_exchange other={.self=111,.peer=333,.boot=12,.peer_boot=33,.nonce=712,.remote_nonce=exchange.nonce,.until=exchange.until,.accepted=true};
 struct r2_ble_event e={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};cold.now=n.now=fake_time;assert(!r2_gm5_status_encode(&cold,&other,0,e.data));r2_group_admission_receive(&n,&e,n.now);rejoin_tick();
 assert(n.gm5.candidate[0].boot==12&&n.gm5.candidate[0].epoch==1&&!n.gm5.candidate[0].withdraw_created);
 assert(!r2_gm5_status_receive(&cold,&other,1,peers[4].tx[0],R2_GM5_STATUS_BYTES,++fake_time));
 assert(cold.gm5.active==333&&cold.gm5.active_boot==33&&cold.gm5.role==R2_GM5_COLD&&!cold.standby_authorized);
 cold.now=e.at=n.now=++fake_time;assert(!r2_gm5_status_encode(&cold,&other,2,e.data));r2_group_admission_receive(&n,&e,n.now);assert(!promote);
 assert(n.gm5.selected==222&&n.gm5.issued_until==obligation&&n.local_gm&&!memcmp(&healthy,&n.peer[1],sizeof healthy));
 assert(r2_gm5_status_receive(&n,&old,2,e.data,e.length,n.now));
 puts("PASS actual incoming callback/MTU/lookup/JOIN encode+owner+challenge/proof/accept, real retirement and newboot rejoin with old caches retained; STATUS clears only stale candidate scope; healthy ACTIVE/selected grant unchanged; returned COLD follows incumbent; old session rejected");
}

/* A healthy incumbent answers actual JOIN wire. Other advertised identities
 * refuse create in this fault schedule; this is not an RF/controller model. */
static void answer_incumbent(void) {
 att_mtu_updated(&connection,226,226);mtu_done(&connection,0,&peers[4].mtu);
 r2_ble_poll(++fake_time);located(&connection,0,&peers[4].lookup,discovery_marker,sizeof discovery_marker);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);
 struct r2_join_record request;assert(!r2_group_join_decode(&request,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(request.kind==R2_JOIN_REQUEST);
 struct r2_join_exchange server;++fake_time;assert(!r2_group_join_begin(&server,true,111,333,11,fake_time,fake_time));assert(!r2_group_join_receive(&server,&request,fake_time));
 server.challenge_at=++fake_time;
 struct r2_join_record reply={.kind=R2_JOIN_CHALLENGE,.sender=111,.receiver=333,.sender_boot=11,.receiver_boot=33,.client_nonce=request.client_nonce,.server_nonce=server.nonce,.sequence=1,.created_us=server.challenge_at,.echo_us=request.created_us,.capabilities=3,.name="Incumbent"};
 struct r2_ble_event e={.peer=4,.length=R2_GROUP_JOIN_BYTES,.at=fake_time};assert(!r2_group_join_encode(e.data,&reply,&n.group));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);struct r2_join_record proof;assert(!r2_group_join_decode(&proof,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));assert(proof.kind==R2_JOIN_PROOF);assert(!r2_group_join_receive(&server,&proof,fake_time));
 reply.kind=R2_JOIN_ACCEPT;reply.sequence=2;reply.created_us=e.at=++fake_time;reply.echo_us=proof.created_us;assert(!r2_group_join_encode(e.data,&reply,&n.group));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);server.accepted=true;
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(exchange.accepted&&peers[4].tx[0][0]==19);
 struct r2_network incumbent;assert(!r2_network_init(&incumbent,0,2,111,11,999,40000,fake_time));incumbent.group=n.group;incumbent.gm5.enabled=true;incumbent.capabilities=3;incumbent.admission.eligible=true;incumbent.candidate.generation=1;incumbent.gm5.role=R2_GM5_ACTIVE;incumbent.gm5.active=111;incumbent.gm5.active_boot=11;incumbent.gm5.selected=222;incumbent.gm5.issued_until=fake_time+90000000;
 assert(!r2_gm5_status_receive(&incumbent,&server,0,peers[4].tx[0],R2_GM5_STATUS_BYTES,fake_time));e.length=R2_GM5_STATUS_BYTES;e.at=incumbent.now=++fake_time;assert(!r2_gm5_status_encode(&incumbent,&server,1,e.data));n.now=fake_time;r2_group_admission_receive(&n,&e,n.now);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(!r2_gm5_status_receive(&incumbent,&server,2,peers[4].tx[0],R2_GM5_STATUS_BYTES,fake_time));
 assert(incumbent.gm5.selected==222&&n.gm5.active==111&&n.gm5.role==R2_GM5_COLD&&!promote&&!n.peer[1].identity&&!n.standby_authorized);
}
static void cold_parent_precedence_tests(void) {
 reset();n.gm5.bridge=true;
 demand=(struct r2_local_demand){.peer=999,.peer_boot=99,.at=n.now,.until=n.now+1000000};
 r2_group_admission_tick(&n,n.now);assert(admission_parent==999);
 n.gm5.role=R2_GM5_ACQUIRING;n.gm5.active=777;
 r2_group_admission_tick(&n,n.now);assert(admission_parent==777);
 n.gm5.role=R2_GM5_HOT;r2_group_admission_tick(&n,n.now);assert(admission_parent==777);
 n.gm5.role=R2_GM5_ACTIVE;n.gm5.selected=888;
 r2_group_admission_tick(&n,n.now);assert(admission_parent==888);
 n.gm5.role=R2_GM5_COLD;n.gm5.active=111;demand=(struct r2_local_demand){0};
 assert(!r2_gm5_control_peer(&n));r2_group_admission_tick(&n,n.now);assert(admission_parent==111);
 n.gm5.active_until=n.now;r2_group_admission_tick(&n,n.now);assert(!admission_parent);
 puts("PASS existing required parent and appointed control repair retain precedence; COLD bridge has no appointed control peer; expired incumbent supplies no preference");
}
static void cold_refresh_tests(bool reachable) {
 reset();uint64_t start=n.now;n.gm5.next_poll=start;n.gm5.active_until=start+120000000;n.admission.boot_at=n.admission.stable_at=0;n.admission.observing=true;n.admission.source_epoch=1;n.admission.last_expiry=start+1500000;
 unsigned completed=0,attempts=0;uint64_t expiry=0;bt_addr_le_t addr={.type=BT_ADDR_LE_RANDOM};
 for(uint64_t at=start;at<start+160000000;at+=5000){
  fake_time=n.now=at;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=at;source.last_pair_utc=INT64_C(1791417600000000)+at;
  r2_group_admission_gm5_policy(&n,at);
  if(scanning&&r2_gm5_poll_due(&n,at))for(unsigned i=0;i<6;i++){
   addr.a.val[0]=(uint8_t)i;candidate_seen(&addr,i?700+i:111,false,i?-20:-80,at);
   if(!i&&!candidates[0].consumed)candidates[0].consumed=start-1;
  }
  candidate_select(at);create_pending=reachable&&peers[4].identity==111;create_callback=true;unsigned before=initiations;
  r2_ble_poll(at);r2_group_admission_tick(&n,at);
  if(initiations!=before){attempts++;printf("ATTEMPT elapsed%llu peer%llu parent%llu\n",(unsigned long long)(at-start),(unsigned long long)peers[4].identity,(unsigned long long)admission_parent);}
  if(peers[4].conn&&!active&&!peers[4].disconnect_requested){answer_incumbent();completed++;}
  if(peers[4].disconnect_requested&&!atomic_get(&peers[4].dead))callbacks.disconnected(&connection,19);
  n.now=fake_time;r2_network_tick(&n,&source,fake_time);
  if(!expiry&&n.gm5.active!=111)expiry=at-start;
  assert(!n.peer[1].identity&&!sync_acquiring&&!precision_discovery);
 }
 printf("RESULT attempts%u completed%u expiry%llu role%u active%llu\n",attempts,completed,(unsigned long long)expiry,n.gm5.role,(unsigned long long)n.gm5.active);
 if(reachable)assert(!expiry&&completed>=4&&n.gm5.role==R2_GM5_COLD);
 else assert(expiry==120000000&&!completed&&n.gm5.role==R2_GM5_ACTIVE);
 puts(reachable?"PASS native COLD sparse scheduler renews live incumbent through actual JOIN/STATUS despite competing inventory; no persistent control/PT":
                "PASS unavailable incumbent retains original120s contact expiry and finite COLD fallback; attempts cannot renew authority");
}

int main(int argc,char **argv)
{
 if(argc==3&&!strcmp(argv[1],"--terminal")){int error=stranded_control();FILE *f=fopen(argv[2],"wb");assert(f&&fwrite(&error,sizeof error,1,f)==1&&!fclose(f));return 0;}
 stranded_control();
 if(argc==2){write_handoff(argv[1]);return 0;}assert(argc==1);
 guest_lifecycle_tests();retained_rejoin_tests();
 reset();assert(!r2_group_admission_mesh_contact(n.now));ready_guest(3);
 assert(!r2_group_admission_mesh_contact(n.now)); /* Mere native connection is not authentication. */
 (void)authenticate(3);assert(r2_group_admission_mesh_contact(n.now));
 uint64_t contact_end=exchange.until;memset(&client,0,sizeof client);memset(&demand,0,sizeof demand);
 assert(r2_group_admission_mesh_contact(contact_end-1));
 assert(!r2_group_admission_mesh_contact(contact_end));assert(!r2_group_admission_mesh_contact(contact_end+1));
 active=false;assert(!r2_group_admission_mesh_contact(contact_end-1));
 reset();(void)authenticate_client(true);active=false;memset(&demand,0,sizeof demand);
 assert(client.peer&&client.peer_boot&&client.until>client.at);client.source=false;client.map_until=0;
 assert(!r2_group_admission_mesh_contact(client.at-1));assert(r2_group_admission_mesh_contact(client.until-1));
 assert(!r2_group_admission_mesh_contact(client.until));assert(!r2_group_admission_mesh_contact(client.until+1));
 memset(&client,0,sizeof client);demand=(struct r2_local_demand){.peer=111,.peer_boot=11,.at=n.now,.until=n.now+1000000};
 assert(!r2_group_admission_mesh_contact(demand.at-1));assert(r2_group_admission_mesh_contact(demand.until-1));
 assert(!r2_group_admission_mesh_contact(demand.until));assert(!r2_group_admission_mesh_contact(demand.until+1));
 puts("PASS RX contact actual authenticated JOIN/client, inactive/unauthenticated exclusion, original client/demand boundaries independent of PT");
 inventory_snapshot_tests();late_appointment();
 abandoned_phase_tests();admission_boundary_tests();missed_active_polls(false);missed_active_polls(true);
 reset();n.gm5.role=R2_GM5_ACTIVE;n.gm5.active=n.identity;n.gm5.active_boot=n.boot;n.local_gm=n.ready=true;n.choice=0;n.root_boot=n.boot;
 (void)authenticate_client(true);
 puts("PASS real authenticated foreign timing JOIN ACCEPT cannot publish passive timing mode on a serving G5 ACTIVE before STATUS selection");
 reset();for(unsigned role=0;role<4;role++)for(unsigned tx=0;tx<2;tx++)for(unsigned requested=0;requested<2;requested++){
  n.gm5.role=role;assert(r2_gm5_bridge_needed(&n.gm5,tx,requested)==(role==R2_GM5_COLD&&tx&&requested));
 }
 n.gm5.role=R2_GM5_COLD;
 puts("PASS bridge exception only while COLD and configured transmitter explicitly requests forwarding; OFF/ACTIVE/HOT do not add leaf work");
 client=(struct r2_group_client){.passive=true,.source=true,.root=111,.boot=11,.source_epoch=1,.publication=1,.at=n.now,.until=n.now+1500000,.uncertainty_us=40000};
 uint64_t original_anchor=client.at,original_permit=client.until;
 unsigned starts=scan_starts,calls=initiations;
 for(unsigned i=0;i<10000;i++){fake_time=70000000+i*1000;n.now=fake_time;r2_group_admission_tick(&n,fake_time);r2_ble_client_window(false);r2_ble_poll(fake_time);r2_ble_client_seek(fake_time,true);}
 assert(scan_starts==starts&&initiations==calls&&!scanning&&!advertising&&!precision_discovery&&!sync_acquiring);
 assert(client.source&&client.at==original_anchor&&client.until==original_permit);
 puts("PASS10s healthy COLD: zero scan starts, initiations, precision acquisitions or standing control links between status opportunities");
 reset();unsigned visits[3]={0};bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM};r2_ble_admission_policy(0,false);
 for(unsigned opportunity=0;opportunity<9;opportunity++){
  uint64_t at=70000000+opportunity*30000000;fake_time=at;r2_ble_gm5_policy(true,0,true);
  for(unsigned c=0;c<3;c++){address.a.val[0]=(uint8_t)c;candidate_seen(&address,700+c,c==1,-20-(int)c*30,at);}
  candidate_select(at+40000);uint64_t chosen=peers[4].identity;assert(chosen>=700&&chosen<=702);visits[chosen-700]++;
  r2_ble_admission_completed(chosen,at+40000);memset(&peers[4],0,sizeof peers[4]);
 }
 for(unsigned c=0;c<3;c++)assert(visits[c]==3);
 uint64_t at=400000000;for(unsigned c=0;c<3;c++)candidate_seen(&address,700+c,false,-20-(int)c*30,at);
 r2_ble_gm5_policy(true,702,true);candidate_select(at+40000);assert(peers[4].identity==702);memset(&peers[4],0,sizeof peers[4]);
 r2_ble_admission_policy(701,false);candidate_select(at+40000);assert(peers[4].identity==701);
 puts("PASS actual transport nine30s opportunities: each unequal-RSSI/passive identity visited3times; selected repair and explicit parent precedence");

 reset();memset(visits,0,sizeof visits);group_client=false;client_window=true;
 for(unsigned opportunity=0;opportunity<9;opportunity++){
  uint64_t at=70000000+opportunity*30000000;fake_time=n.now=at;n.gm5.next_poll=at;
  r2_group_admission_gm5_policy(&n,at);
  for(unsigned c=0;c<3;c++){address.a.val[0]=(uint8_t)c;candidate_seen(&address,700+c,c==1,-20-(int)c*30,at);}
  unsigned before=initiations;fake_time=n.now=at+40000;r2_ble_poll(n.now);
  uint64_t chosen=peers[4].identity;assert(initiations==before+1&&chosen>=700&&chosen<=702);visits[chosen-700]++;
  r2_group_admission_gm5_policy(&n,n.now);uint64_t next=n.gm5.next_poll;
  r2_group_admission_gm5_policy(&n,n.now+1);assert(n.gm5.next_poll==next&&next==at+40000+30000000);
  memset(&peers[4],0,sizeof peers[4]);
 }
 for(unsigned c=0;c<3;c++)assert(visits[c]==3);
 puts("PASS real refused creates consume nine opportunities fairly across unequal RSSI/passive identities; old submission timestamp cannot move future deadline");
 reset();n.gm5.role=R2_GM5_ACTIVE;n.gm5.active=333;n.gm5.active_boot=33;n.local_gm=n.ready=true;n.choice=0;n.root_boot=33;n.publication=1;
 n.view=(struct v12_dc_coarse){.available=true,.relative_ready=true,.generation=1,.utc_second=1791417600,.evidence_us=n.now,.valid_until_us=n.now+1500000};n.service_until=n.view.valid_until_us;
 for(unsigned i=0;i<R2_GM5_CANDIDATES;i++)n.gm5.candidate[i]=(struct r2_gm5_candidate){.identity=900+i,.at=n.now,.until=n.now+120000000};
 struct r2_join_record accepted=authenticate(6);assert(accepted.source_flags&&accepted.root==333&&!promote);
 fake_time+=100001;n.now=fake_time;r2_group_admission_tick(&n,fake_time);assert(!active&&!n.peer[1].identity); /* A full inventory cannot consume RC admission capacity. */
 puts("PASS ordinary RC causal bootstrap and release with full candidate inventory; no GM control promotion");
 for(unsigned foreign=0;foreign<2;foreign++){
  reset();n.gm5.inhibited=true;n.gm5.negative_root=foreign?999:111;n.gm5.negative_boot=foreign?99:11;n.gm5.negative_epoch=1;
  (void)authenticate(3);assert(active&&exchange.accepted&&!exchange.control);
  struct r2_network remote;assert(!r2_network_init(&remote,0,2,111,11,99,40000,n.now));remote.group=n.group;remote.gm5.enabled=true;remote.gm5.role=R2_GM5_ACTIVE;remote.gm5.active=111;remote.gm5.active_boot=11;remote.gm5.selected=333;remote.admission.eligible=true;remote.candidate.generation=1;
  struct r2_join_exchange other={.self=111,.peer=333,.boot=11,.peer_boot=33,.nonce=77,.remote_nonce=exchange.nonce,.until=exchange.until,.accepted=true};
  struct r2_ble_event e={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};remote.now=fake_time;n.now=fake_time;
  assert(!r2_gm5_status_encode(&remote,&other,0,e.data));r2_group_admission_receive(&n,&e,n.now);
  n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(gm5_phase==1&&!gm5_send);
  assert(!r2_gm5_status_receive(&remote,&other,1,peers[4].tx[0],R2_GM5_STATUS_BYTES,remote.now));
  remote.now=e.at=++fake_time;n.now=fake_time;assert(!r2_gm5_status_encode(&remote,&other,2,e.data));r2_group_admission_receive(&n,&e,n.now);
  assert(promote==!foreign&&exchange.control==!foreign);
  fake_time+=50001;n.now=fake_time;r2_group_admission_tick(&n,n.now);
  assert(!n.local_gm&&!n.hot_ready&&!n.standby_authorized);
  if(!foreign){assert(n.peer[1].identity==111&&n.peer[1].authenticated&&!n.peer[1].map_valid);assert(peers[1].identity==111&&!peers[4].conn);assert(n.gm5.role==R2_GM5_ACQUIRING);assert(!r2_ble_control_available());}
  else assert(!n.peer[1].identity&&!promote&&n.gm5.role==R2_GM5_COLD);
 }
 /* Actual outgoing JOIN plus STATUS response/final send: the transport must
  * retain its already-owned ACL after poll completion until promotion/drain. */
 reset();struct r2_join_exchange server=authenticate_client(false);
 struct r2_network appointed;assert(!r2_network_init(&appointed,0,2,111,11,99,40000,n.now));appointed.group=n.group;appointed.gm5.enabled=true;appointed.gm5.role=R2_GM5_ACTIVE;appointed.gm5.active=111;appointed.gm5.active_boot=11;appointed.gm5.selected=333;appointed.admission.eligible=true;appointed.candidate.generation=1;
 struct r2_ble_event response={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};appointed.now=n.now=fake_time;
 assert(!r2_gm5_status_encode(&appointed,&server,1,response.data));r2_group_admission_receive(&n,&response,n.now);assert(gm5_phase==2&&gm5_send&&n.gm5.role==R2_GM5_ACQUIRING);
 n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(promote&&gm5_done&&!gm5_send&&!r2_gm5_poll_due(&n,n.now));
 r2_ble_gm5_policy(true,111,false);r2_ble_client_window(true);unsigned disconnect_before=disconnects;
 fake_time+=5000;n.now=fake_time;r2_ble_poll(fake_time);assert(disconnects==disconnect_before&&peers[4].conn==&connection);
 fake_time+=50001;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(!active&&n.peer[1].identity==111&&peers[1].conn==&connection&&!peers[4].conn&&!n.hot_ready&&!n.standby_authorized);
 puts("PASS actual client JOIN/STATUS1/STATUS2, completed sparse poll retains outgoing ACL through50ms promotion; no fabricated timing/HOT");
 reset();server=authenticate_client(false);uint64_t client_until=exchange.until;disconnect_before=disconnects;
 r2_ble_gm5_policy(true,0,false);r2_ble_client_window(true);fake_time+=1100000;n.now=fake_time;r2_ble_poll(fake_time);assert(disconnects==disconnect_before&&active&&exchange.until==client_until);
 fake_time=client_until;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(!active&&!n.peer[1].identity&&disconnects>disconnect_before&&!n.hot_ready);
 puts("PASS lost client STATUS1 retains only the original5s outgoing owner then releases; no poll lifetime renewal");
 /* Every incomplete/lost STATUS phase keeps the one original JOIN deadline.
  * Replayed/wrong-session phases cannot promote or reset its clock. */
 for(unsigned lost=0;lost<3;lost++){
  reset();(void)authenticate(3);uint64_t until=exchange.until;
  struct r2_network remote;assert(!r2_network_init(&remote,0,2,111,11,99,40000,n.now));remote.group=n.group;remote.gm5.enabled=true;remote.gm5.role=R2_GM5_ACTIVE;remote.gm5.active=111;remote.gm5.active_boot=11;remote.gm5.selected=333;remote.admission.eligible=true;remote.candidate.generation=1;
  struct r2_join_exchange other={.self=111,.peer=333,.boot=11,.peer_boot=33,.nonce=77,.remote_nonce=exchange.nonce,.until=until,.accepted=true};
  struct r2_ble_event e={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};remote.now=n.now=fake_time;
  assert(!r2_gm5_status_encode(&remote,&other,0,e.data));
  if(lost){r2_group_admission_receive(&n,&e,n.now);uint32_t received=n.gm5.status_rx;
   r2_group_admission_receive(&n,&e,n.now);assert(n.gm5.status_rx==received); /* Repeated phase0. */
   n.now=++fake_time;r2_group_admission_tick(&n,n.now);
   e.at=remote.now=n.now=++fake_time;assert(!r2_gm5_status_encode(&remote,&other,2,e.data));
   if(lost==2){r2_group_admission_receive(&n,&e,n.now);assert(promote);atomic_set(&peers[4].tx_busy,1); /* Final confirmation delivered; TX owner cannot drain. */}
  }
  fake_time=until-1;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(active&&exchange.until==until&&!n.peer[1].identity);
  atomic_clear(&peers[4].tx_busy);fake_time=until;n.now=fake_time;r2_group_admission_tick(&n,n.now);assert(!active&&!n.peer[1].identity&&!n.hot_ready&&!n.standby_authorized);
  assert(peers[4].release&&peers[4].conn==&connection); /* Callback still owns the old object. */
  callbacks.disconnected(&connection,19);r2_ble_client_window(false);r2_ble_gm5_policy(true,0,false);r2_ble_poll(++fake_time);assert(!peers[4].conn&&!peers[4].identity);
 }
 puts("PASS lost/replayed STATUS and busy final TX: original5s expiry, no late promotion, callback-bound drain and slot reuse");
 /* A genuine withdrawal arrives after a STATUS was minted on this JOIN.
  * Preserve its same-boot/epoch negative floor through actual ACL cleanup and
  * finite-grant takeover, then deliver the delayed final owner phase<5s. */
 reset();(void)authenticate(3);
 struct r2_network old;assert(!r2_network_init(&old,0,2,111,11,99,40000,n.now));old.group=n.group;old.gm5.enabled=true;old.gm5.role=R2_GM5_ACTIVE;old.gm5.active=111;old.gm5.active_boot=11;old.gm5.selected=333;old.admission.eligible=true;old.candidate.generation=1;
 struct r2_join_exchange other={.self=111,.peer=333,.boot=11,.peer_boot=33,.nonce=77,.remote_nonce=exchange.nonce,.until=exchange.until,.accepted=true};
 struct r2_ble_event delayed={.peer=4,.length=R2_GM5_STATUS_BYTES,.at=++fake_time};old.now=n.now=fake_time;
 assert(!r2_gm5_status_encode(&old,&other,0,delayed.data));r2_group_admission_receive(&n,&delayed,n.now);n.now=++fake_time;r2_group_admission_tick(&n,n.now);assert(gm5_phase==1);
 old.now=++fake_time;assert(!r2_gm5_status_encode(&old,&other,2,delayed.data));
 n.gm5.role=R2_GM5_HOT;n.gm5.active=111;n.gm5.active_boot=11;n.gm5.selected=333;n.gm5.grant_until=n.now+9000000;n.gm5.grant_generation=7;
 n.standby_authorized=n.hot_ready=true;n.standby_root_boot=11;n.standby_source_epoch=1;n.choice=1;n.root_boot=11;n.root_health_at=n.now;
 n.pair=(struct r2_pair){.root=1,.standby=0,.root_boot=11,.standby_boot=33,.epoch=1,.until=n.gm5.grant_until};
 n.admission.boot_at=0;n.admission.observing=true;n.admission.source_epoch=1;n.admission.last_expiry=n.now+1500000;
 uint64_t negative=++fake_time;authenticated_withdrawal(negative);uint64_t floor=n.gm5.candidate[0].withdraw_created;assert(floor==negative);
 for(unsigned ms=0;ms<=3000;ms+=10){uint64_t at=negative+ms*1000;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=at;source.last_pair_utc=INT64_C(1791417600000000)+ms*1000;
  if(ms==10)r2_network_link(&n,1,false,at);r2_network_tick(&n,&source,at);
  if(ms<3000)assert(!n.local_gm);else assert(n.local_gm&&n.gm5.role==R2_GM5_ACTIVE);
 }
 delayed.at=n.now;uint32_t rx=n.gm5.status_rx;uint64_t hold=n.gm5.active_until;r2_group_admission_receive(&n,&delayed,n.now);
 assert(n.local_gm&&n.gm5.active==333&&n.gm5.status_rx==rx&&n.gm5.active_until==hold&&n.gm5.candidate[0].withdraw_created==floor&&!promote);
 puts("PASS actual HELLO withdrawal, ACL drain, original3s finite takeover and delayed authenticated STATUS phase2: no old-root resurrection");
 /* COLD has no successor authority. Newer exact-root negative status shortens
  * the contact hold to<=10.05s, then the original30..32s discovery+3s fence. */
 reset();(void)authenticate(3);old.now=n.now;old.gm5.role=R2_GM5_COLD;old.admission.eligible=false;
 other=(struct r2_join_exchange){.self=111,.peer=333,.boot=11,.peer_boot=33,.nonce=77,.remote_nonce=exchange.nonce,.until=exchange.until,.accepted=true};
 delayed.at=n.now;assert(!r2_gm5_status_encode(&old,&other,0,delayed.data));uint64_t accepted_at=n.now;r2_group_admission_receive(&n,&delayed,n.now);
 assert(n.gm5.active_until<=accepted_at+R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US&&!n.standby_authorized&&!n.local_gm);
 uint64_t original_hold=n.gm5.active_until;old.now+=1000;n.now+=1000;assert(!r2_gm5_status_encode(&old,&other,0,delayed.data));assert(!r2_gm5_status_receive(&n,&exchange,0,delayed.data,R2_GM5_STATUS_BYTES,n.now));assert(n.gm5.active_until==original_hold);
 puts("PASS newer exact-root COLD/ineligible status bounds ordinary fallback contact hold without rolling deadlines or positive authority");

 cold_parent_precedence_tests();cold_refresh_tests(true);cold_refresh_tests(false);
 puts("PASS real G5 STATUS confirmation and one-slot promotion: same-root inhibited acquisition, foreign-root exclusion, no timing/grant invented");
 return 0;
}
