/* Exercise the exact admission body after authenticated proof verification. */
#define main old_route_main
#include "auto_route_test.c"
#undef main
int main(void){
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=3,.map_bound_us=40000};
 client.passive=true;exchange=(struct r2_join_exchange){.server=true,.peer=111,.peer_boot=1000};
 struct r2_join_record r=gm_time(1001000);r.kind=R2_JOIN_PROOF;r.sender=111;r.sender_boot=1000;
 struct r2_join_time t;assert(r2_group_join_time(&t,&r,1000000,1002000,40000));
 accepted_peer(&n,&r,1000000,1002000);
 printf("GM_SERVER_BOOTSTRAP source=%u root=%llu passive=%u\n",client.source,(unsigned long long)client.root,client.passive);fflush(stdout);
 assert(client.source&&client.root==111&&client.boot==1000&&client.until==t.until);
 assert(!n.admission.eligible&&!n.local_gm); /* A coarse anchor grants no candidacy. */
 guest_bootstrap();publisher_bootstrap();
 puts("PASS GM-profile incoming root proof bootstraps reception while RC downstream guest remains unable to replace upstream");
}
