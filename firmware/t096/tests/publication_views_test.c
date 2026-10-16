/* Production publication cache/expiry and bounded peer fairness contract. */
#include "ble_service.h"
#include "owner_budget.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool relay_selected;static int stop_requested;
static int atomic_get(int *p){return *p;}
static unsigned radio_reads,relay_reads;
static struct r2_relay_status fixture;
static struct r2_radio_precision_view fit;
void r2_radio_service_view(struct r2_radio_precision_view *p,uint64_t now){++radio_reads;*p=fit;}
void r2_relay_service_snapshot(struct r2_relay_status *r,struct r2_radio_precision_view *p,uint64_t now){++relay_reads;*r=fixture;*p=fit;}
#include "publication_views_under_test.h"
int main(void){
 struct r2_relay_status route;struct r2_radio_precision_view precision;
 fixture=(struct r2_relay_status){.map_valid=true,.sampled_us=100,.valid_until_us=500,.source_until_us=600,.root_boot=9,.source_epoch=7,.source_frame_ns=42,.utc_us=12345};
 fit=(struct r2_radio_precision_view){.map_valid=true,.locked=true,.authorized=true,.fit_valid=true};
 relay_selected=true;publication_views(150,&fit,&fixture,&precision,&route);
 assert(!radio_reads&&!relay_reads&&route.map_valid&&precision.map_valid&&precision.locked);
 assert(route.sampled_us==100&&route.utc_us==12345&&route.valid_until_us==500&&route.source_until_us==600);
 assert(route.root_boot==9&&route.source_epoch==7&&route.source_frame_ns==42);
 publication_views(500,&fit,&fixture,&precision,&route);
 assert(!route.map_valid&&!precision.locked&&!precision.map_valid&&precision.authorized&&!route.utc_us);
 assert(route.sampled_us==100&&route.valid_until_us==500&&route.source_until_us==600);
 publication_views(600,&fit,&fixture,&precision,&route);assert(!precision.authorized&&!route.map_valid);
 publication_views(99,&fit,&fixture,&precision,&route);assert(!route.map_valid&&!precision.map_valid);
 stop_requested=1;publication_views(150,&fit,&fixture,&precision,&route);assert(!route.map_valid&&!precision.authorized);stop_requested=0;
 fixture.error=-1;publication_views(150,&fit,&fixture,&precision,&route);assert(!route.map_valid&&!precision.authorized);fixture.error=0;
 fixture.map_valid=false;publication_views(150,&fit,&fixture,&precision,&route);assert(!precision.map_valid&&!route.map_valid);fixture.map_valid=true;
 fixture.sampled_us=0;publication_views(150,&fit,&fixture,&precision,&route);assert(!route.map_valid);fixture.sampled_us=100;
 publication_views(150,NULL,NULL,&precision,&route);assert(relay_reads==1&&!radio_reads&&route.map_valid);
 relay_selected=false;publication_views(150,&fit,&fixture,&precision,&route);assert(radio_reads==1&&relay_reads==1&&!route.selected);
 for(unsigned self=0;self<4;self++)for(unsigned ready=0;ready<16;ready++){
  unsigned cursor=0;uint8_t seen=0;
  for(unsigned cycle=0;cycle<4;cycle++){
   uint8_t selected=owner_tx_peers(self,4,&cursor,ready);assert(!(selected&(1u<<self))&&!(selected&~ready));
   assert((unsigned)__builtin_popcount(selected)<=R2_OWNER_CONTROL_TX);seen|=selected;
  }
  assert(seen==(ready&~(1u<<self))); /* busy/absent low slots cannot starve a peer */
 }
 assert(R2_OWNER_CONTROL_RX==2&&R2_OWNER_CAPTURE_RX==2);
 puts("same-cycle captured publication/no recomputation/no rebase/expiry/revocation + bounded fair peers PASS");
}
