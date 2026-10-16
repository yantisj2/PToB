/* Actual service snapshot: direct coarse control time is never a delivered map. */
#include "ble_service.h"
#include "group_admission.h"
#include <assert.h>
#include <stdio.h>
#define K_FOREVER 0
static int lock,stop_requested;
static struct r2_service_status status;
static struct r2_group_client delivered,coarse;
static struct r2_radio_precision_view view;
int r2_group_slot(const uint64_t ids[4],uint64_t id){for(unsigned i=0;i<4;i++)if(ids[i]==id)return (int)i;return -1;}
static int k_mutex_lock(int *m,int wait){return 0;}
static int k_mutex_unlock(int *m){return 0;}
static int atomic_get(int *p){return *p;}
static uint64_t now_us(void){return 1000000;}
static void r2_radio_service_snapshot(struct r2_radio_precision_view *v,struct r2_group_client *t,struct r2_group_client *c,uint64_t *frame,uint64_t now){*v=view;*t=delivered;*c=coarse;*frame=123;}
static bool r2_radio_service_serving(uint64_t now){return true;}
#include "service_snapshot_under_test.h"
int main(void){
 status.follower_clock_valid=true;status.follower_utc=1791744000000000ULL;status.time_sampled_us=0;status.service_until=UINT64_MAX;status.follower=true;
 struct r2_service_status s;r2_service_snapshot(&s);
 assert(!s.follower_clock_valid&&s.service_until==0&&s.follower_utc==0);
 delivered=(struct r2_group_client){.source=true,.utc_us=1791744000000000ULL,.at=1000000,.map_until=1500000};view.map_valid=true;
 r2_service_snapshot(&s);assert(s.follower_clock_valid&&s.time_sampled_us==1000000&&s.follower_utc==delivered.utc_us);
 delivered.source=false;delivered.utc_us=0;view.map_valid=false;r2_service_snapshot(&s);assert(!s.follower_clock_valid&&s.follower_utc==0);
 delivered.passive=true;r2_service_snapshot(&s);assert(!s.follower_clock_valid);
 puts("Actual direct/public service snapshot: no stale coarse-control UTC inherited as delivered time PASS");
}
