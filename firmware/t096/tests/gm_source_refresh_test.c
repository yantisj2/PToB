/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Exact production network/authenticated wire; synthetic accepted-map input.
 * A wide new echo must not discard source proof checked by a live old map. */
#define main original_retirement_main
#include "gm_standby_retirement_test.c"
#undef main

static void setup(struct r2_network *a,struct r2_network *b)
{
 struct late_pair f=late_pair();*a=f.a;*b=f.b;
 assert(!deliver_record(b,a,&b->peer[1].response));
 strcpy(a->name,"Active");strcpy(b->name,"Hot");
 uint64_t at=a->now;
 a->peer[1].seen=a->peer[1].map_anchor=at-1000000;
 a->peer[1].offset_low=-40000;a->peer[1].offset_high=40000;
 a->peer[1].candidate_until=at+50000;a->peer[1].sent[0]=at-120000;
}
static struct r2_auth_record fresh(struct r2_network *a,struct r2_network *b)
{
 struct r2_auth_record r=record(b,1,R2_AUTH_HELLO);
 r.root=b->self;r.boot=r.coordinator_boot=b->boot;r.root_boot=a->boot;
 r.challenge=b->peer[1].own_nonce;r.decision=b->peer[1].nonce;r.sequence=++b->hello_sequence;
 r.created_us=a->now-60000;r.evidence_us=r.created_us;r.source_second=a->now-120000;
 r.expires_us=r.created_us+R2_GM5_RECORD_US;
 r.flags=1;r.source_epoch=1;r.permit_until_us=a->now+1000000;
 r.boot_age_ms=r.source_age_ms=100000;
 return r;
}
static int receive(struct r2_network *a,struct r2_network *b,struct r2_auth_record *r)
{
 uint8_t wire[R2_GROUP_CONTROL_MAX];assert(!encode_control(b,wire,r,b->peer[1].tx_key));
 return r2_network_receive(a,1,wire,r2_control_wire_size(wire),a->now,a->now);
}
static void map_unchanged(const struct r2_network *a,const struct r2_network *before)
{
 assert(a->peer[1].seen==before->peer[1].seen);
 assert(a->peer[1].map_anchor==before->peer[1].map_anchor);
 assert(a->peer[1].offset_low==before->peer[1].offset_low);
 assert(a->peer[1].offset_high==before->peer[1].offset_high);
}
static void grant_unchanged(const struct r2_network *a,const struct r2_network *before)
{
 assert(a->gm5.issued_until==before->gm5.issued_until&&a->gm5.generation==before->gm5.generation);
 assert(a->gm5.selected==before->gm5.selected&&a->pair.until==before->pair.until);
}
int main(void)
{
 for(unsigned kind=0;kind<16;kind++){
  struct r2_network a,b;setup(&a,&b);uint64_t oldpermit=a.peer[1].candidate_until;
  struct r2_auth_record r=fresh(&a,&b);
  if(kind==1)r.challenge++;
  if(kind==2)r.decision++;
  if(kind==3){r.boot++;r.coordinator_boot=r.boot;}
  if(kind==4)r.root_boot++;
  if(kind==5)a.peer[1].seen=a.peer[1].map_anchor=a.now-10000000;
  if(kind==6)a.peer[1].map_valid=false;
  if(kind==7){
   a.peer[1].seen=a.peer[1].map_anchor=a.now-500000;
   r.created_us=r.evidence_us=a.now-900000;r.expires_us=a.now+20000;
   r.permit_until_us=a.now+200000;
  }
  if(kind==8)r.permit_until_us=a.now-30000;
  if(kind==9){r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;}
  if(kind==10)a.peer[1].seen=a.peer[1].map_anchor=a.now-9999999;
  if(kind==11)r.source_second=0; /* Original accepted noecho behavior. */
  if(kind==12)a.peer[1].connected=false;
  if(kind==13)a.peer[1].authenticated=false;
  if(kind==14)a.peer[1].seen=a.now+1;
  if(kind==15)r.permit_until_us=0;
  struct r2_network before=a;int rc=receive(&a,&b,&r);
  grant_unchanged(&a,&before);
  if(kind!=1&&kind!=3)map_unchanged(&a,&before);
  if(kind==0||kind==10){
   assert(!rc&&a.peer[1].candidate_until>oldpermit&&a.hello_map_refusals==before.hello_map_refusals+1);
   assert(a.peer[1].map_valid&&a.peer[1].authenticated);
  }else if(kind==8){
   assert(!rc&&a.peer[1].candidate_until<a.now);
  }else if(kind==9){assert(!rc&&!a.peer[1].candidate_until&&!a.peer[1].candidate_epoch&&a.peer[1].source_withdrawn);}
  else if(kind==11){assert(!rc&&a.peer[1].candidate_until>oldpermit);}
  else {assert(a.peer[1].candidate_until<=oldpermit);}
  if(kind==7){
   assert(rc==-ESTALE); /* Original TTL freshly checked through fallback map. */
  }
  if(kind==12||kind==15)assert(rc==-EACCES);
  if(kind==13||kind==14)assert(!rc);
  if(kind==0){
   a.now=oldpermit+1;a.timing_at=a.now;gm5_tick(&a,true,a.now,NULL);
   assert(a.hot_ready&&!a.hot_withdrawals);
   grant_unchanged(&a,&before);
  }
  printf("PASS case%u rc%d oldpermit%llu after%llu map_seen%llu nonce%llu boot%llu hot%u reason%u\n",kind,rc,(unsigned long long)oldpermit,(unsigned long long)a.peer[1].candidate_until,(unsigned long long)a.peer[1].seen,(unsigned long long)a.peer[1].nonce,(unsigned long long)a.peer[1].boot,a.hot_ready,a.hot_withdrawal_reason);
 }
 puts("PASS current accepted-map fallback cases + boot/nonce/rootboot/mapage/TTL/sourcepermit/withdrawal/disconnected/auth/futureseen negatives; no grant/map renewal; no hardware");
 return 0;
}
