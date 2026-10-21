/* Scalar-copy contract tests; fake types, not controller lifecycle simulation. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "controller_snapshot.h"
#define CONFIG_BT_MAX_CONN 6
struct proc_ctx { uint8_t proc, state, done; };
struct list { struct proc_ctx *head; };
struct ll_conn {
    struct { uint16_t handle; } lll;
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
    pool[5].lll.handle=5; pool[5].llcp.local.pend_proc_list.head=&ctx;
    pool[5].llcp.local.state=3; pool[5].llcp.remote.state=0;
    pool[5].llcp.prt_reload=134; pool[5].supervision_expire=9;
    pool[5].connect_expire=2;
    struct ll_conn before[6]; memcpy(before,pool,sizeof pool);
    ptob_controller_snapshot_locked(5,&s);
    assert(s.handle_matches && s.head_present && s.head_proc==10 && s.head_state==1);
    assert(s.prt_reload==134 && s.supervision_expire==9 && s.connect_expire==2);
    assert(!memcmp(before,pool,sizeof pool)); assert(ctx.state==1);
    ctx.state=2; pool[5].llcp.local.prt_expire=133;
    ptob_controller_snapshot_locked(5,&s); assert(s.head_state==2 && s.local_prt==133);
    pool[5].lll.handle=UINT16_MAX;
    pool[5].llcp.local.pend_proc_list.head=(void *)(uintptr_t)1;
    ptob_controller_snapshot_locked(5,&s); assert(!s.handle_matches && !s.head_present);
    return 0;
}
