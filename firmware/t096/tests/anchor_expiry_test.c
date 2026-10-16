/* Repeated causal UTC reads must neither erode nor extend source permission. */
#include "group_admission.h"
#include <assert.h>
#include <stdio.h>
static struct r2_group_client client,timing_context;
static bool refresh_required;
#include "anchor_under_test.h"
int main(void){
 struct r2_network n={.map_bound_us=40000};
 client=(struct r2_group_client){.source=true,.root=11,.boot=22,.source_epoch=3};
 timing_context=(struct r2_group_client){.source=true,.root=11,.boot=22,.source_epoch=3,.utc_us=1000000,.at=100000,.map_until=400000,.until=500000,.publication=7};
 for(uint64_t now=100000;now<300000;now+=2000){
  anchor_from_live_map(&n,now);assert(client.until==500000&&client.publication==7);
  assert(client.utc_us==1000000+now-100000);
 }
 struct r2_group_client last=client;
 anchor_from_live_map(&n,400000);assert(client.at==last.at&&client.until==last.until);
 timing_context.boot++;anchor_from_live_map(&n,300000);assert(client.at==last.at);
 puts("Repeated same-scope captured UTC anchors preserve original source deadline; expired map/wrong boot cannot refresh PASS");
}
