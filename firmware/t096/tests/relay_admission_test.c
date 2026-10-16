#include "relay_admission.h"
#include <assert.h>
int main(void){
 struct r2_relay_request r={1,5000000,10000000,true};
 assert(r2_relay_request_poll(&r,0,true,false)==R2_RELAY_PENDING_GM&&r.pending);
 assert(r2_relay_request_poll(&r,100,false,true)==R2_RELAY_PENDING_CONTROL&&r.pending);
 assert(r2_relay_request_poll(&r,5000000,true,true)==R2_RELAY_EXPIRED&&!r.pending);
 r=(struct r2_relay_request){2,9000000,12000000,true};
 assert(r2_relay_request_poll(&r,8000000,false,false)==R2_RELAY_ADMIT&&!r.pending);
 assert(r.activation_until==12000000&&r.expires==9000000);
 assert(r2_relay_request_poll(&r,8000001,false,false)==R2_RELAY_NONE);
 r.pending=false;assert(r2_relay_request_poll(&r,8000002,false,false)==R2_RELAY_NONE);
 return 0;
}
