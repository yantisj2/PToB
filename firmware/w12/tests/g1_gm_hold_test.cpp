#include "g1_parent_selection.hpp"
#include "g1_gm_discovery.hpp"
#include "g1_parent_journal.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
int main(){
 G1ParentSelection p;p.seen(11,false,-65,1000000);p.bind(11,111,1,1000000);
 p.seen(11,false,-65,12000000);p.seen(22,true,-30,12000000,true);
 assert(p.choose(12040000)==11&&p.selection_view(12040000).reason==G1ParentSelection::Decision::HoldGM);
 p.seen(22,true,-20,12300000,true);assert(!p.choose(12340000)); // No stale-GM connection and no RSSI-only hop.
 p.unavailable(12400000);assert(!p.parent());p.seen(22,true,-20,12400000,true);assert(!p.choose(12440000));
 p.seen(11,false,-90,12500000);assert(p.choose(12540000)==11);
 assert(p.selection_view(12540000).reason==G1ParentSelection::Decision::RecoverGM);
 p.seen(22,true,-20,15400000,true);assert(p.choose(15440000)==22); // Genuine absence has a bounded fallback.
 p.bind(22,222,1,15500000);p.seen(22,true,-20,26000000,true);p.seen(11,false,-90,26000000);
 assert(p.choose(26040000)==11&&p.selection_view(26040000).reason==G1ParentSelection::Decision::ReturnGM);
 p.diagnostic(true);assert(p.choose(26040000)==22);p.diagnostic(false);p.target(22,true);assert(p.choose(26040000)==22);
 p.reset();p.seen(11,false,-90,30000000);p.bind(11,111,1,30000000);
 for(uint64_t t=30000000;t<=36000000;t+=62500){p.usable(false,t);p.tick(t,true,false);}
 assert(p.quality().poor);p.seen(11,false,-90,36100000);p.seen(22,true,-20,36100000,true);assert(p.choose(36140000)==22); // Measured bad delivery still permits failover.
 G1GmDiscovery d;assert(!d.observe(1000000,true));assert(!d.observe(30999999,true));assert(d.observe(31000000,true));
 assert(d.active(32499999)&&!d.observe(32500000,true));assert(!d.observe(60999999,true)&&d.observe(61000000,true));
 assert(!d.observe(61100000,false)&&!d.next&&!d.until);assert(!d.observe(62000000,true));assert(!d.observe(1,true));
 G1ParentJournal j;G1ParentScope gm{11,111,11,111,1,1,0,false};
 assert(j.observe(gm,100,G1ParentReason::Startup,0,0,0,false)==1);
 assert(!j.observe(gm,200,G1ParentReason::HoldGM,0,0,0,false)); // Renewal is not infrastructure churn.
 auto restarted=gm;restarted.boot=restarted.root_boot=112;
 assert(j.observe(restarted,300,G1ParentReason::SourceChange,0,0,0,false)==2);
 G1ParentEvent e;assert(j.get(2,e)&&e.from.boot==111&&e.to.boot==112);
 for(unsigned i=0;i<10;i++){auto next=restarted;next.source=2+i;assert(j.observe(next,400+i,G1ParentReason::SourceChange,0,0,0,false));}
 assert(!j.get(1,e)&&j.next(0,e)&&e.sequence==5);assert(!j.next(UINT32_MAX,e));
 assert(sizeof(G1ParentJournal)<=1024&&sizeof(G1ParentSelection)<=1280);
 std::printf("PASS direct GM hold/recovery/return, real-loss fallback, pins, bounded passive re-observation and RAM parent history; selector=%zu journal=%zu\n",sizeof(p),sizeof(j));
}
