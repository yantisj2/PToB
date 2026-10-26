/* Ordinary clock receive regression: selected Apache-2.0 SDK bodies.
 * MIT harness. Copyright (c) 2026 Jonathan Yantis.
 * SPDX-License-Identifier: MIT
 * Provenance: PToB reviewed safe02 production-body experiment, 2026-10-26. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#define CONFIG_LITTLE_ENDIAN 1
#define CONFIG_BT_CTLR_SYNC_PERIODIC 1
#define CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX 191
#define CONFIG_BT_CTLR_ULL_HIGH_PRIO 1
#define CONFIG_BT_CTLR_ULL_LOW_PRIO 2
#define IS_ENABLED(x) ENABLED_##x
#define ENABLED_CONFIG_BT_CTLR_SYNC_PERIODIC 1
#define ENABLED_CONFIG_BT_CTLR_PHY_CODED 0
#define ENABLED_CONFIG_BT_CTLR_LOW_LAT 0
#define ENABLED_CONFIG_BT_TICKER_LOW_LAT 0
#define __packed __attribute__((packed))
#include "actual_pdu.inc"
#include "expected-sites.h"
typedef int memq_link_t;
struct lll_hdr{void *parent;};
struct ull_hdr{unsigned ref,ticks_active_to_start,ticks_prepare_to_start,ticks_preempt_to_start,ticks_slot;void(*disabled_cb)(void*);void *disabled_param;};
struct lll_scan_aux{struct lll_hdr hdr;unsigned phy,state,chan,window_size_us,is_chain_sched;};
struct lll_scan{struct lll_hdr hdr;struct lll_scan_aux *lll_aux;};
struct lll_sync{struct lll_hdr hdr;struct lll_scan_aux *lll_aux;unsigned phy;};
struct node_rx_pdu;
struct ll_sync_set{struct ull_hdr ull;struct lll_sync lll;unsigned data_len,is_stop;};
struct ll_scan_set{struct ull_hdr ull;struct lll_scan lll;unsigned is_stop;struct{bool cancelled;struct ll_sync_set *sync;unsigned state,filter_policy;void *param;}periodic;};
struct ll_scan_aux_set{struct ull_hdr ull;struct lll_scan_aux lll;void *parent;struct node_rx_pdu *rx_head,*rx_last;unsigned data_len;struct node_rx_pdu *rx_incomplete;};
struct ll_sync_iso_set{};
struct node_rx_ftr{void *param;struct lll_scan_aux *lll_aux;struct node_rx_pdu *extra;unsigned aux_data_len,aux_sched,aux_lll_sched,aux_failed,scan_req,phy_flags,radio_end_us,ticks_anchor;};
struct pdu_adv{unsigned len,tx_addr;struct pdu_adv_com_ext_adv adv_ext_ind;uint8_t payload[128];};
struct node_rx_pdu{struct{unsigned type,handle;memq_link_t *link;}hdr;struct node_rx_ftr rx_ftr;union{struct pdu_adv packet;uint8_t pdu[sizeof(struct pdu_adv)];};unsigned seen;};
struct mayfly{int a,b;memq_link_t *link;void(*fp)(void*);void *param;};
static struct ll_scan_set scan_set;static struct ll_sync_set sync_set;static struct ll_scan_aux_set aux_set;
static bool allocated;static unsigned releases,queued_count,schedules,starts,stop_result;static int assertion_line;static const char *assertion;static jmp_buf failed;
#define LL_ASSERT(q) do{if(!(q)){assertion=#q;assertion_line=__LINE__;longjmp(failed,1);}}while(0)
#define HDR_LLL2ULL(p) (((struct lll_hdr*)(p))->parent)
#define BT_HCI_LE_EXT_SCAN_PHY_1M 1
#define PHY_1M 1
#define PHY_2M 2
#define PHY_FLAGS_S8 0
#define NODE_RX_TYPE_EXT_1M_REPORT 1
#define NODE_RX_TYPE_EXT_AUX_REPORT 2
#define NODE_RX_TYPE_SYNC_REPORT 3
#define NODE_RX_TYPE_EXT_2M_REPORT 4
#define NODE_RX_TYPE_RELEASE 5
#define LL_SYNC_STATE_CREATED 2
#define LL_SYNC_STATE_ADDR_MATCH 1
#define LL_SYNC_STATE_IDLE 0
#define TICKER_NULL 255
#define TICKER_NULL_PERIOD 0
#define TICKER_NULL_REMAINDER 0
#define TICKER_NULL_LAZY 0
#define TICKER_ID_SCAN_BASE 10
#define TICKER_ID_SCAN_AUX_BASE 20
#define TICKER_ID_SCAN_SYNC_BASE 30
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_USER_ID_ULL_HIGH 1
#define TICKER_USER_ID_ULL_LOW 2
#define TICKER_USER_ID_THREAD 3
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_BUSY 1
#define TICKER_STATUS_FAILURE 2
#define BDADDR_SIZE 6
#define PDU_AC_EXT_HEADER_SIZE_MIN 1
#define PDU_AC_EXT_PAYLOAD_RX_SIZE 255
#define OFFS_UNIT_300_US 300
#define OFFS_UNIT_30_US 30
#define EXT_ADV_AUX_PHY_LE_CODED 2
#define HAL_TICKER_CNTR_MSBIT 31
#define HAL_TICKER_CNTR_CMP_OFFSET_MIN 1
#define EVENT_OVERHEAD_END_US 1
#define EVENT_OVERHEAD_START_US 1
#define EVENT_OVERHEAD_XTAL_US 1
#define EVENT_OVERHEAD_PREEMPT_MIN_US 1
#define EVENT_TICKER_RES_MARGIN_US 1
#define EVENT_JITTER_US 1
#define BIT(n) (1u<<(n))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define unlikely(x) (x)
#define PDU_ADV_AUX_PTR_OFFSET_GET(p) ((p)->offs_phy_packed[0]|(((p)->offs_phy_packed[1]&31)<<8))
#define PDU_ADV_AUX_PTR_PHY_GET(p) ((p)->offs_phy_packed[1]>>5)
#define PDU_ADV_ADI_SID_GET(p) (0)
#define AUX_OFFSET_IS_VALID(a,b,c) ((a)>(c))
#define SCA_DRIFT_50_PPM_US(x) (1)
#define SCA_DRIFT_500_PPM_US(x) (1)
#define HAL_TICKER_US_TO_TICKS(x) (x)
#define HAL_TICKER_US_TO_TICKS_CEIL(x) (x)
#define HAL_TICKER_TICKS_TO_US(x) (x)
#define PDU_AC_US(a,b,c) (10)
#define PDU_AC_MAX_US(a,b) (10)
struct pdu_cte_info{unsigned char v;};struct pdu_adv_sync_info{unsigned char v;};
static void flush(void*);static void flush_safe(void*);static void done_disabled_cb(void*);static void aux_sync_partial(void*);static void aux_sync_incomplete(void*);static void ticker_op_cb(uint32_t,void*);
static struct ll_scan_set *ull_scan_is_valid_get(void *p){return p==&scan_set?&scan_set:NULL;}
static struct ll_sync_set *ull_sync_is_valid_get(void *p){return p==&sync_set?&sync_set:NULL;}
static struct ll_scan_aux_set *ull_scan_aux_is_valid_get(void *p){return allocated&&p==&aux_set?&aux_set:NULL;}
static struct ll_scan_aux_set *aux_acquire(void){assert(!allocated);allocated=true;return &aux_set;}
static void aux_release(struct ll_scan_aux_set *a){assert(a==&aux_set&&allocated&&a->parent);a->parent=NULL;allocated=false;releases++;}
static unsigned aux_handle_get(void *a){assert(a==&aux_set);return 0;}
static unsigned ull_scan_handle_get(void *s){assert(s==&scan_set);return 0;}
static unsigned ull_sync_handle_get(void *s){assert(s==&sync_set);return 0;}
static void ll_rx_put(memq_link_t *link,struct node_rx_pdu *r){assert(link);for(;r;r=r->rx_ftr.extra){assert(!r->seen);r->seen++;queued_count++;}}
static void ll_rx_sched(void){schedules++;}
static void ll_rx_put_sched(memq_link_t *l,struct node_rx_pdu *r){ll_rx_put(l,r);ll_rx_sched();}
static void ull_hdr_init(struct ull_hdr *h){memset(h,0,sizeof *h);}
static void lll_hdr_init(struct lll_scan_aux *a,void *p){a->hdr.parent=p;}
static unsigned ull_ref_get(struct ull_hdr *h){return h->ref;}
static unsigned ticker_ticks_now_get(void){return 0;}
static uint32_t ticker_ticks_diff_get(uint32_t a,uint32_t b){return a-b;}
static unsigned lll_radio_rx_ready_delay_get(unsigned a,unsigned b){return 1;}
static bool ull_sync_setup_addr_check(void *a,unsigned b,unsigned c,void*d,unsigned e){return false;}
static bool ull_sync_setup_sid_match(void *a,void*b,unsigned c){return false;}
static void ull_sync_setup(void*a,unsigned b,void*c,void*d){}
static void ull_sync_chm_update(unsigned a,void*b,unsigned c){}
static int ticker_yield_abs(unsigned a,unsigned b,unsigned c,unsigned d,void *e,void*f){return 0;}
static void ticker_cb(uint32_t a,uint32_t b,uint32_t c,uint16_t d,uint8_t e,void*f){}
static int ticker_start(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,unsigned h,unsigned i,void(*cb)(uint32_t,uint32_t,uint32_t,uint16_t,uint8_t,void*),void*j,void(*op)(uint32_t,void*),void*k){starts++;op(0,k);return 0;}
static int ull_ticker_stop_with_mark(unsigned a,void*b,void*c){return (int)stop_result;}
static int ull_disable(void *p){return 0;}
static int mayfly_enqueue(unsigned a,unsigned b,unsigned c,struct mayfly*m){m->fp(m->param);return 0;}
#ifdef FIXED
#include "actual-fixed.inc"
#else
#include "actual-original.inc"
#endif
static memq_link_t links[16];static struct node_rx_pdu nodes[8];
static void reset(void){memset(&scan_set,0,sizeof scan_set);memset(&sync_set,0,sizeof sync_set);memset(&aux_set,0,sizeof aux_set);memset(nodes,0,sizeof nodes);allocated=false;releases=queued_count=schedules=starts=stop_result=0;assertion=NULL;assertion_line=0;scan_set.lll.hdr.parent=&scan_set;sync_set.lll.hdr.parent=&sync_set;sync_set.lll.phy=1;for(unsigned i=0;i<8;i++)nodes[i].hdr.link=&links[i];}
static void packet(unsigned i,unsigned type,void *parent,bool next,bool lll){
 struct node_rx_pdu *r=&nodes[i];r->hdr.type=type;r->rx_ftr.param=parent;r->rx_ftr.lll_aux=allocated?&aux_set.lll:NULL;r->rx_ftr.aux_lll_sched=lll;r->rx_ftr.ticks_anchor=100000;r->rx_ftr.radio_end_us=10;
 struct pdu_adv_com_ext_adv *p=&r->packet.adv_ext_ind;struct pdu_adv_ext_hdr *h=&p->ext_hdr;p->ext_hdr_len=next?4:1;r->packet.len=next?15:12;h->aux_ptr=next;
 if(next){struct pdu_adv_aux_ptr *a=(void*)h->data;a->chan_idx=10;a->offs_phy_packed[0]=100;}
}
static void begin_sync(void){packet(0,NODE_RX_TYPE_SYNC_REPORT,&sync_set.lll,true,false);nodes[0].rx_ftr.extra=&nodes[7];ull_scan_aux_setup(&links[0],&nodes[0]);assert(allocated&&starts==1&&nodes[0].seen==1&&aux_set.rx_last==NULL);}
static void terminal_ull(void){begin_sync();packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,false,false);ull_scan_aux_setup(&links[1],&nodes[1]);assert(!allocated&&releases==1&&nodes[1].seen==1&&nodes[7].seen==1);}
static void continued(bool lll){begin_sync();packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,true,lll);ull_scan_aux_setup(&links[1],&nodes[1]);
 packet(2,NODE_RX_TYPE_EXT_AUX_REPORT,lll?(void*)&sync_set.lll:(void*)&aux_set.lll,false,false);ull_scan_aux_setup(&links[2],&nodes[2]);
 assert(!allocated&&releases==1&&nodes[0].seen==1&&nodes[1].seen==1&&nodes[2].seen==1&&nodes[7].seen==1);}
static void sync_stop(void){begin_sync();sync_set.is_stop=1;packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,true,false);ull_scan_aux_setup(&links[1],&nodes[1]);assert(allocated&&!releases&&nodes[1].seen==1);stop_result=EALREADY;stop_result=(unsigned)-EALREADY;assert(!ull_scan_aux_stop(&aux_set));assert(!allocated&&releases==1&&nodes[7].seen==1);}
static void scanner(bool stop){packet(0,NODE_RX_TYPE_EXT_1M_REPORT,&scan_set.lll,true,false);ull_scan_aux_setup(&links[0],&nodes[0]);assert(allocated&&!nodes[0].seen);scan_set.is_stop=stop;packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,false,false);ull_scan_aux_setup(&links[1],&nodes[1]);if(stop){assert(allocated&&!releases);assert(!ull_scan_aux_stop(&aux_set));}assert(!allocated&&releases==1&&nodes[0].seen==1&&nodes[1].seen==1);}
static void call(void(*test)(void),const char *name,bool failure){reset();int rc=setjmp(failed);if(!rc)test();assert(!!rc==failure);if(rc){const char *expr="scan";unsigned site=EXPECT_SCAN_SCHEDULE_LINE;if(!strcmp(name,"periodic-ULL-terminal")){expr="sync_lll";site=EXPECT_SYNC_FLUSH_LINE;}else if(!strcmp(name,"periodic-ULL-to-LLL-terminal"))site=EXPECT_SCAN_FLUSH_LINE;assert(assertion&&!strcmp(assertion,expr)&&assertion_line==(int)site);}printf("%s %s",rc?"EXPECTED_ASSERT":"PASS",name);if(rc)printf(" at %s line%d",assertion,assertion_line);putchar('\n');}
static void continued_lll(void){continued(true);}static void continued_ull(void){continued(false);}static void scan_running(void){scanner(false);}static void scan_stopping(void){scanner(true);}
static void deferred_sync_flush(void){begin_sync();packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,true,false);ull_scan_aux_setup(&links[1],&nodes[1]);assert(allocated&&aux_set.parent==&sync_set.lll&&!aux_set.rx_last);aux_set.ull.ref=1;sync_set.is_stop=1;flush_safe(&aux_set);assert(allocated&&!releases&&aux_set.ull.disabled_cb);void(*cb)(void*)=aux_set.ull.disabled_cb;void *param=aux_set.ull.disabled_param;aux_set.ull.ref=0;aux_set.ull.disabled_cb=NULL;cb(param);assert(!allocated&&releases==1&&nodes[0].seen==1&&nodes[1].seen==1&&nodes[7].seen==1);}
static void deferred_scan_flush(void){packet(0,NODE_RX_TYPE_EXT_1M_REPORT,&scan_set.lll,true,false);ull_scan_aux_setup(&links[0],&nodes[0]);packet(1,NODE_RX_TYPE_EXT_AUX_REPORT,&aux_set.lll,true,false);ull_scan_aux_setup(&links[1],&nodes[1]);aux_set.ull.ref=1;scan_set.is_stop=1;flush_safe(&aux_set);assert(allocated&&!releases&&aux_set.ull.disabled_cb);void(*cb)(void*)=aux_set.ull.disabled_cb;void *param=aux_set.ull.disabled_param;aux_set.ull.ref=0;aux_set.ull.disabled_cb=NULL;cb(param);assert(!allocated&&releases==1&&nodes[0].seen==1&&nodes[1].seen==1);}
int main(void){
#ifdef FIXED
 const bool failure=false;
#else
 const bool failure=true;
#endif
 call(terminal_ull,"periodic-ULL-terminal",failure);call(continued_lll,"periodic-ULL-to-LLL-terminal",failure);call(continued_ull,"periodic-ULL-to-ULL-terminal",failure);call(sync_stop,"periodic-sync-stop-drain",failure);
 call(scan_running,"extended-scan-terminal",false);call(scan_stopping,"extended-scan-stop-drain",false);
#ifdef FIXED
 call(deferred_sync_flush,"deferred-periodic-flush-ref1",false);call(deferred_scan_flush,"deferred-scanner-flush-ref1",false);
#endif
 return 0;
}
