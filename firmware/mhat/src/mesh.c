/* Copyright 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * A local, unauthenticated G1 hint selects a context to TRY. Only the current
 * RC owner can authenticate it and grant a clock offer. Trust is separate.
 */
#include "mhat_private.h"
#include "ble_service.h"
#include "rc_dynamic_service.h"
#include "rc_admission_policy.h"
#include "usb_source.h"
#include "source_monitor.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
enum { IDLE, SCAN, RUNNING, DRAIN };
static unsigned state;
static bool halted;
static atomic_t listening;
static struct k_spinlock hint_lock;
static struct r2_identity identity;
static uint64_t candidate, selected, failed;
static int8_t best_rssi;
static int64_t deadline, retry_at, last_valid, failed_until;
static uint32_t hints, selections, restarts;
static int last_error;
static struct mhat_role_monitor roles;
static struct { uint8_t wire[R2_ROLE_AD_BYTES];uint64_t at; } role_pending;
static bool role_queued;
static uint32_t role_auth_rejected,role_overwritten;
struct observation { bool connectable; int8_t rssi; };
static bool parse(struct bt_data *d,void *opaque){
    const struct observation *o=opaque;
    /* Copy only in the Bluetooth callback. Authentication runs in the main
     * loop, outside radio/controller work; this adds no scan or transmission. */
    if(d->type==BT_DATA_MANUFACTURER_DATA&&d->data_len==R2_ROLE_AD_BYTES-2&&
       d->data[0]==0xff&&d->data[1]==0xff&&d->data[2]=='G'&&d->data[3]=='S'&&
       r2_group_get(d->data+14,4)==MHAT_KEY_EPOCH){
        k_spinlock_key_t key=k_spin_lock(&hint_lock);
        if(role_queued)++role_overwritten;
        role_pending.wire[0]=R2_ROLE_AD_BYTES-1;role_pending.wire[1]=0xff;
        memcpy(role_pending.wire+2,d->data,d->data_len);role_pending.at=k_uptime_get()*1000;
        role_queued=true;k_spin_unlock(&hint_lock,key);return true;
    }
    if(!atomic_get(&listening))return true;
    if(d->type!=BT_DATA_MANUFACTURER_DATA||d->data_len!=23||
       d->data[0]!='G'||d->data[1]!='1'||
       !rc_admission_g1_direct(d->data[2],o->connectable)||
       r2_group_get(d->data+11,4)!=MHAT_KEY_EPOCH)return true;
    uint64_t id=r2_group_get(d->data+3,8);
    if(!id)return false;
    k_spinlock_key_t key=k_spin_lock(&hint_lock);
    ++hints;
    if(!(id==failed&&k_uptime_get()<failed_until)&&(!candidate||o->rssi>best_rssi)){
        candidate=id;best_rssi=o->rssi;
    }
    k_spin_unlock(&hint_lock,key);return false;
}
static void received(const struct bt_le_scan_recv_info *info,struct net_buf_simple *ad){
    struct observation o={.connectable=!!(info->adv_props&BT_GAP_ADV_PROP_CONNECTABLE),.rssi=info->rssi};
    bt_data_parse(ad,parse,&o);
}
static void role_poll(void){
    static uint8_t wire[R2_ROLE_AD_BYTES];uint64_t at=0;
    k_spinlock_key_t key=k_spin_lock(&hint_lock);
    if(role_queued){memcpy(wire,role_pending.wire,sizeof wire);at=role_pending.at;role_queued=false;}
    k_spin_unlock(&hint_lock,key);if(!at)return;
    struct r2_group_config group={.id=r2_group_get(wire+8,8),.epoch=MHAT_KEY_EPOCH,.psk=MHAT_PSK};
    struct r2_role_record r;
    if(r2_role_decode(&r,wire,sizeof wire,&group)){++role_auth_rejected;return;}
    (void)mhat_role_observe(&roles,group.id,&r,at,k_uptime_get()*1000);
}
static struct bt_le_scan_cb callbacks={.recv=received};
int mhat_mesh_start(const struct r2_identity *id){
    identity=*id;bt_le_scan_cb_register(&callbacks);return 0;
}
void mhat_mesh_stop(void){
    halted=true;r2_usb_source_withdraw();
    if(state==SCAN){atomic_clear(&listening);last_error=bt_le_scan_stop();if(!last_error)state=IDLE;}
    if(state==RUNNING){r2_dynamic_service_stop();state=DRAIN;deadline=k_uptime_get()+2500;}
}
bool mhat_mesh_quiescent(void){return state==IDLE;}
int mhat_mesh_poll(void){
    int64_t now=k_uptime_get();
    role_poll();
    static struct r2_service_status s;
    if(state==RUNNING||state==DRAIN){
        r2_dynamic_service_supervise();r2_dynamic_service_snapshot(&s);
        if(state==RUNNING){
            if(s.follower_clock_valid&&!s.error)last_valid=now;
            if((s.closed&&!s.active)||(uint64_t)(now-last_valid)>MHAT_CONTEXT_RECOVERY_MS){
                r2_usb_source_withdraw();r2_dynamic_service_stop();state=DRAIN;deadline=now+2500;
                k_spinlock_key_t key=k_spin_lock(&hint_lock);failed=selected;failed_until=now+20000;k_spin_unlock(&hint_lock,key);
                ++restarts;
            }
        }else if(s.closed&&!s.active){state=IDLE;retry_at=now+1000;}
        else if(now>deadline)return last_error=-ETIMEDOUT;
    }
    if(state==IDLE&&!halted&&now>=retry_at){
        k_spinlock_key_t key=k_spin_lock(&hint_lock);candidate=0;best_rssi=-127;k_spin_unlock(&hint_lock,key);
        struct bt_le_scan_param p={.type=BT_LE_SCAN_TYPE_PASSIVE,.interval=80,.window=40};
        atomic_set(&listening,1);last_error=bt_le_scan_start(&p,NULL);
        if(last_error){atomic_clear(&listening);return last_error;}
        state=SCAN;deadline=now+1500;
    }
    if(state==SCAN&&!halted&&now>=deadline){
        atomic_clear(&listening);last_error=bt_le_scan_stop();if(last_error)return last_error;
        k_spinlock_key_t key=k_spin_lock(&hint_lock);selected=candidate;k_spin_unlock(&hint_lock,key);
        state=IDLE;retry_at=now+1000;
        if(selected){
            struct r2_enrollment e={.group={.id=selected,.epoch=MHAT_KEY_EPOCH,.psk=MHAT_PSK},
                .rc_dynamic=true,.rc7_leaf=true,.rc7_boot_rc=true,.map_bound_us=RC5_UNKNOWN};
            /* Normal G1 admission binds the caller's actual BLE address to its
             * authenticated identity. Derive it from the discovered context;
             * public bt_disable() clears ID0, which bt_id_reset() cannot reset.
             * A previous owner is fully drained before reaching this branch. */
            uint64_t self=strtoull(identity.silicon,NULL,16);uint8_t derived[16];
            r2_group_pair_key(derived,&e.group,self,self);
            bt_addr_le_t own={.type=BT_ADDR_LE_RANDOM};memcpy(own.a.val,derived,6);own.a.val[5]|=0xc0;
            last_error=bt_disable();if(last_error)return last_error;
            last_error=bt_id_create(&own,NULL);if(last_error)return last_error;
            last_error=bt_enable(NULL);if(last_error)return last_error;
            /* configure() clears old context state. run() repeats ordinary
             * JOIN/proof admission; an advertisement cannot authorize PPS. */
            r2_usb_source_withdraw();last_error=r2_dynamic_service_configure(&identity,&e);
            if(!last_error)last_error=r2_dynamic_service_run(now);
            if(last_error)return last_error;
            state=RUNNING;last_valid=now;++selections;
        }
    }
    return 0;
}
void mhat_mesh_stats(uint32_t out[6]){
    k_spinlock_key_t key=k_spin_lock(&hint_lock);
    out[0]=1;out[1]=hints;out[2]=selections;out[3]=restarts;out[4]=state;out[5]=(uint32_t)last_error;
    k_spin_unlock(&hint_lock,key);
}
int mhat_mesh_json(char *out,size_t size){
    uint64_t now=k_uptime_get(),valid_at=(uint64_t)last_valid,age=now>=valid_at?now-valid_at:0;
    int n=snprintf(out,size,",\"discovery\":{\"context\":\"%016llx\",\"state\":%u,"
        "\"no_clock_ms\":%llu,\"restart_after_ms\":%llu,\"role_auth_rejected\":%u,"
        "\"role_replayed\":%u,\"role_overwritten\":%u,\"role_capacity_refused\":%u,\"reports\":[",
        (unsigned long long)selected,state,(unsigned long long)age,
        (unsigned long long)(age<MHAT_CONTEXT_RECOVERY_MS?MHAT_CONTEXT_RECOVERY_MS-age:0),
        role_auth_rejected,roles.replayed,role_overwritten,roles.capacity_refused);
    if(n<0||(size_t)n>=size)return -ENOSPC;
    size_t used=n;unsigned count=0;
    for(unsigned i=0;i<MHAT_ROLE_ROWS;i++){
        const struct mhat_role_row *p=&roles.row[i];const struct r2_role_record *r=&p->report;
        if(!p->group)continue;
        uint64_t age_us=now*1000>=p->received_us?now*1000-p->received_us:UINT64_MAX;
        n=snprintf(out+used,size-used,"%s{\"context\":\"%016llx\",\"root\":\"%016llx\","
            "\"root_boot\":\"%016llx\",\"hot\":\"%016llx\",\"hot_boot\":\"%016llx\","
            "\"epoch\":%u,\"sequence\":%u,\"age_ms\":%llu,\"fresh\":%s}",count?",":"",
            (unsigned long long)p->group,(unsigned long long)r->root,(unsigned long long)r->root_boot,
            (unsigned long long)r->hot,(unsigned long long)r->hot_boot,r->source_epoch,r->sequence,
            (unsigned long long)(age_us/1000),age_us<r->valid_us?"true":"false");
        if(n<0||(size_t)n>=size-used)return -ENOSPC;
        used+=n;++count;
    }
    n=snprintf(out+used,size-used,"]}");
    return n<0||(size_t)n>=size-used?-ENOSPC:(int)(used+n);
}
