// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
if(which=="corese-gm-preference"){
 using namespace v12;
 static_assert(G1ParentSelection::GmRecoveryUs==10000000);
 static_assert(G1ParentSelection::LockDwellUs==180000000);
 G1ParentSelection selection;
 constexpr uint64_t begin=1000000,gm=111,rc=222;
 selection.seen(gm,false,-40,begin);selection.bind(gm,3,4,begin,false);
 // An unqualified but authenticated GM gets three minutes to acquire lock.
 selection.locked(false,begin+60000000);assert(!selection.needs_alternative());
 selection.locked(false,begin+179999999);assert(!selection.needs_alternative());
 selection.locked(false,begin+180000000);assert(selection.needs_alternative());
 // Actual source loss revokes permission separately; only fresh discovery
 // gets the bounded ten-second GM preference, never a cached connection.
 G1ParentSelection lost;lost.seen(gm,false,-40,begin);lost.bind(gm,3,4,begin,false);
 const uint64_t loss=begin+1000000;lost.unavailable(loss);
 lost.seen(rc,true,-35,loss+100000);assert(!lost.choose(loss+100000));
 lost.seen(gm,false,-40,loss+5000000);assert(lost.choose(loss+5000000)==gm);
 lost.seen(rc,true,-35,loss+9999999);assert(!lost.choose(loss+9999999));
 lost.seen(rc,true,-35,loss+10000000);assert(lost.choose(loss+10000000)==rc);
 // Explicit selection still overrides AUTO preference immediately.
 lost.target(rc,true);lost.seen(rc,true,-35,loss+10000001);assert(lost.choose(loss+10000001)==rc);
 std::puts("PASS CoreSE GM preference:180s precision trial,10s bounded rediscovery, fresh locator and explicit pin");return 0;
}
