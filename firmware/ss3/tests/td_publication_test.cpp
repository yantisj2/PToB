#include "td_publication.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <thread>
#include <cstdio>
struct Value{unsigned serial=0;std::array<unsigned,512> words{};};
int main(){
 v12::TdPublication state;Value slots[3]{};
 auto first=state.pin();assert(first==0);int one=state.reserve();assert(one>0);slots[one].serial=1;state.commit(one);
 auto second=state.pin();assert(second==one);int two=state.reserve();assert(two>0&&two!=one);state.commit(two);
 assert(state.reserve()<0);state.release(first);int free=state.reserve();assert(free==first);state.commit(free);state.release(second);
 v12::TdPublication shared;Value values[3]{};std::atomic<bool> done{false};std::atomic<unsigned> reads{0};std::thread readers[4];
 for(auto& t:readers)t=std::thread([&]{while(!done.load(std::memory_order_acquire)){
  const int pin=shared.pin();if(pin<0)continue;const auto copy=values[pin];shared.release(pin);
  for(auto w:copy.words)assert(w==copy.serial);++reads;
 }});
 for(unsigned generation=1;generation<100000;generation++){
  const int next=shared.reserve();if(next<0)continue;auto& v=values[next];v.serial=generation;v.words.fill(generation);shared.commit(next);
 }
 done.store(true,std::memory_order_release);for(auto& t:readers)t.join();assert(reads>0);
 std::printf("PASS pinned version remains complete; writer skips occupied slots; %u concurrent reader snapshots verified\n",reads.load());
}
