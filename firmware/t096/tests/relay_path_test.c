/* Actual private path configuration and closed-owner release boundaries. */
#include "ble_service.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
static struct r2_enrollment enrollment={.group={.id=1,.epoch=1,.psk={1}}};
static unsigned own,root,relay,leaf;static bool selected,local_path,closed=true;
static uint64_t path_boot;static uint32_t path_epoch,train_floor;static int error;
static struct r2_relay_precision timing;
bool r2_relay_service_closed(void){return closed;}
void r2_ble_field_admission_window(bool allowed,bool renewal){assert(allowed&&!renewal);}
#include "relay_path_under_test.h"
int main(void){
 assert(!r2_relay_service_path(111,99999,99999,9,4));
 assert(own==1&&enrollment.identity[0]==111&&enrollment.identity[1]==99999&&path_boot==9&&path_epoch==4);
 selected=true;closed=false;timing.train=12;
 assert(r2_relay_service_release_path()==-EBUSY);
 assert(r2_relay_service_path(111,99999,99999,10,5)==-EACCES);
 closed=true;assert(!r2_relay_service_release_path()&&!selected&&train_floor==12);
 assert(!r2_relay_service_path(111,99999,99999,10,5)&&path_boot==10&&path_epoch==5);
 assert(!r2_relay_service_release_path());
 assert(!r2_relay_service_path(222,88888,77777,11,6)&&own==2&&enrollment.identity[2]==77777);
 assert(!enrollment.gm_mask);puts("PASS private stable-ID path rebind requires closed owner, accepts fresh boot/epoch/root/parent, preserves TX generation floor and zero GM slots");
}
