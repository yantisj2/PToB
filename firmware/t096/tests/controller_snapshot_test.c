/* Scalar-copy contract tests; fake types, not controller lifecycle simulation. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "controller_snapshot.h"
#define CONFIG_BT_MAX_CONN 6
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_ID_CONN_BASE 30
#define R2_HEALTH_PA 1
static unsigned irq_depth=1;
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned k){irq_depth=k;}
static void r2_health_operation(unsigned l,unsigned o,int r){(void)l;(void)o;(void)r;assert(irq_depth);}
static void *update_mark,*disable_mark;
static void *ull_update_mark_get(void){assert(irq_depth);return update_mark;}
static void *ull_disable_mark_get(void){assert(irq_depth);return disable_mark;}
static unsigned ticker_calls;
void ptob_ticker_snapshot_locked(uint8_t i,uint8_t t,const void *c,struct ptob_ticker_snapshot *out)
{assert(irq_depth&&i==0&&t>=30&&t<36&&c);++ticker_calls;*out=(struct ptob_ticker_snapshot){.valid=1,.req=255,.ack=254,.context_matches=1,.lazy_current=65535,.ticks_periodic=UINT32_MAX,.job_guard=255,.worker_trigger=254};}
struct proc_ctx { uint8_t proc, state, done; };
struct list { struct proc_ctx *head; };
struct ll_conn {
    struct { uint16_t handle,event_counter,latency_event; uint8_t access_addr[4],role; } lll;
    struct {uint8_t ref;} ull;
    struct {uint8_t latency_cancel;} periph;
    struct { struct { struct list pend_proc_list; uint8_t state; uint16_t prt_expire; } local, remote;
             uint16_t prt_reload; } llcp;
    struct { uint8_t reason_final; } llcp_terminate;
    uint16_t supervision_expire, connect_expire;
};
static struct ll_conn pool[6];
static unsigned lookups;
static struct ll_conn *ll_conn_get(uint16_t h) { assert(h<6); lookups++; return &pool[h]; }
static struct proc_ctx *sys_slist_peek_head(struct list *l) { return l->head; }
#include "../src/controller_snapshot_body.inc"
int main(void) {
    struct ptob_controller_snapshot s;
    ptob_controller_snapshot_locked(6,&s); assert(!s.handle_in_range && !lookups);
    ptob_controller_snapshot_locked(UINT16_MAX,&s); assert(!s.handle_in_range && !lookups);
    pool[0].lll.handle=0;
    ptob_controller_snapshot_locked(0,&s); assert(s.handle_matches && !s.head_present);
    struct proc_ctx ctx={10,1,0};
    pool[5].lll.handle=5; pool[5].lll.event_counter=65535;pool[5].lll.latency_event=65534;
    memcpy(pool[5].lll.access_addr,"\x01\x23\x45\x89",4);pool[5].lll.role=1;
    pool[5].ull.ref=255;pool[5].periph.latency_cancel=1;update_mark=disable_mark=&pool[5]; pool[5].llcp.local.pend_proc_list.head=&ctx;
    pool[5].llcp.local.state=3; pool[5].llcp.remote.state=0;
    pool[5].llcp.prt_reload=134; pool[5].supervision_expire=9;
    pool[5].connect_expire=2;
    struct ll_conn before[6]; memcpy(before,pool,sizeof pool);
    ptob_controller_snapshot_locked(5,&s);
    assert(s.handle_matches && s.head_present && s.head_proc==10 && s.head_state==1);
    assert(s.prt_reload==134 && s.supervision_expire==9 && s.connect_expire==2);
    assert(s.access_address==0x89452301&&s.event_counter==65535&&s.latency_event==65534);
    assert(s.role==1&&s.ull_ref==255&&s.latency_cancel&&s.update_mark&&s.disable_mark);
    assert(s.ticker.valid&&s.ticker.context_matches&&s.ticker.req==255&&s.ticker.ack==254&&s.ticker.lazy_current==65535&&s.ticker.ticks_periodic==UINT32_MAX&&s.ticker.job_guard==255&&s.ticker.worker_trigger==254);
    assert(!memcmp(before,pool,sizeof pool)); assert(ctx.state==1);
    ctx.state=2; pool[5].llcp.local.prt_expire=133;
    ptob_controller_snapshot_locked(5,&s); assert(s.head_state==2 && s.local_prt==133);
    pool[5].lll.handle=UINT16_MAX;
    pool[5].llcp.local.pend_proc_list.head=(void *)(uintptr_t)1;
    ptob_controller_snapshot_locked(5,&s); assert(!s.handle_matches && !s.head_present);
    /* Reused valid slot exposes current state and context; no generation claim. */
    pool[5].lll.handle=5;pool[5].llcp.local.pend_proc_list.head=&ctx;ctx.state=0;
    ptob_controller_snapshot_locked(5,&s);assert(s.handle_matches&&s.head_state==0);
    assert(ticker_calls==5);
    return 0;
}
