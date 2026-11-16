// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "wrist_motion.hpp"
#include <cassert>
int main(){
 using watch::WristReturn;
 WristReturn w;uint64_t t=1000000;
 auto feed=[&](double x,double y,double z,int n){for(int i=0;i<n;++i){w.sample(x,y,z,t);t+=80000;}};
 feed(0,1000,0,25);feed(0,0,1000,6);w.begin(t-80000);assert(w.active);
 feed(0,0,1000,8);assert(!w.take_lowered());
 // Small adjustments and one-frame shocks do not dismiss the glance.
 feed(100,100,990,5);assert(!w.take_lowered());feed(0,3000,0,1);assert(!w.take_lowered());
 feed(0,0,1000,4);feed(0,1000,0,16);assert(w.take_lowered());assert(!w.take_lowered());
 // No prior pose, no dismissal. Interaction cancels an active return detector.
 WristReturn empty;empty.begin(100);assert(!empty.active);
 w.cancel();feed(0,0,1000,20);assert(!w.take_lowered());
 // Mirrored left/right wearing orientations have the same angular behavior.
 WristReturn mirror;uint64_t now=1000000;
 for(int i=0;i<25;++i){mirror.sample(0,-1000,0,now);now+=80000;}
 for(int i=0;i<6;++i){mirror.sample(0,0,1000,now);now+=80000;}
 mirror.begin(now-80000);
 for(int i=0;i<8;++i){mirror.sample(0,0,1000,now);now+=80000;}
 for(int i=0;i<16;++i){mirror.sample(0,-1000,0,now);now+=80000;}
 assert(mirror.take_lowered());
 // A first sample during a shock must not poison gravity forever. After a
 // stale gap the next stationary sample re-seeds the filter before gating.
 WristReturn shock;shock.sample(0,0,3000,1000000);shock.sample(0,0,1000,1600000);
 assert(shock.last==1600000&&shock.gravity==1000);
 shock.begin(1600000);assert(!shock.active);

 // Measured-direction detector: start in view is insufficient, a settled
 // down pose must precede a fresh view. Lowering must never produce raise.
 watch::WristPose pose;uint64_t pt=1000000;
 auto posefeed=[&](double x,double y,double z,int n,bool allow=true,bool glance=false){int result=0;for(int i=0;i<n;++i){pt+=80000;result|=pose.sample(x,y,z,pt,allow,glance);if(result)break;}return result;};
 assert(posefeed(0,-3700,1800,30)==0);
 assert(posefeed(700,3100,2600,12)==0&&pose.prepared);
 assert(posefeed(0,-3700,1800,8)==1);
 assert(posefeed(0,-3700,1800,8,false,true)==0);
 assert(posefeed(700,3100,2600,12,false,true)==2);
 assert(posefeed(700,3100,2600,12)==0);
 // Static desk/face-down and noisy swings cannot stand in for settled view.
 assert(posefeed(0,0,4096,20)==0);
 assert(posefeed(0,-3700,-1800,12)==0);
 assert(posefeed(700,3100,2600,12)==0);
 for(int i=0;i<10;++i){assert(posefeed(0,-3700,1800,1)==0);assert(posefeed(700,3100,2600,1)==0);}
 // Stale/missing sample history cannot bridge a raise.
 pt+=600000;assert(posefeed(0,-3700,1800,12)==0);
 assert(posefeed(700,3100,2600,12,false)==0);
 assert(posefeed(0,-3700,1800,12)==0);

 // Seated initial resting pose observed near +Z must also prepare; don't
 // require a vertically hanging forearm before checking the time.
 pose.reset();assert(posefeed(-280,720,4050,12)==0&&pose.prepared);
 assert(posefeed(0,-3700,1800,8)==1);

}
