// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/shadow_gnss.cpp"
#include <cstdarg>
#include <cstdio>
#include <limits>
namespace v12 {
R2Status test_status;
R2Status r2_status(){return test_status;}
bool ConsoleFrame::format(const char* fmt,...){va_list a;va_start(a,fmt);const auto n=std::vsnprintf(bytes+length,sizeof bytes-length,fmt,a);va_end(a);if(n<=0||unsigned(n)>=sizeof bytes-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
namespace watchboard {bool gnss_power(bool on){++shadow_fake::power_calls;if(shadow_fake::during_power)shadow_fake::during_power();if(shadow_fake::fail_power)return false;shadow_fake::power=on;return true;}}
bool watch_pps_start(uint64_t boot,uint64_t factory,uint64_t){assert(boot==1&&factory==2);++shadow_fake::capture_starts;shadow_fake::capture=true;return !shadow_fake::fail_capture_start;}
bool watch_pps_stop(){++shadow_fake::capture_stops;if(shadow_fake::fail_capture_stop)return false;shadow_fake::capture=false;shadow_fake::edges.clear();return true;}
bool watch_pps_healthy(){return shadow_fake::healthy&&shadow_fake::capture;}
bool watch_pps_take(WatchPpsCapture& p,bool (*)(uint64_t*)){if(shadow_fake::edges.empty())return false;const auto e=shadow_fake::edges.front();shadow_fake::edges.pop_front();p={e.tick,e.at,e.number,e.incarnation,e.enclosure,e.valid};return true;}
static bool clock_read(uint64_t* n){*n=shadow_fake::now*20;return true;}
static void reset(){
 using namespace watchshadow;if(state){state->~State();std::free(state);state=nullptr;}
 shown={};wanted=rail=rail_transition=inhibited=hardware=uart_owned=capture_owned=false;generation=observed_generation=0;power_error=cleanup_error=start_error=0;selected_ble=false;selected_until=0;selected_source=watch::Source::None;
 using namespace shadow_fake;now=1000000;power=uart=capture=false;healthy=true;fail_power=fail_alloc=fail_uart_start=fail_uart_stop=fail_capture_start=fail_capture_stop=fail_uart_read=false;power_calls=uart_starts=uart_stops=capture_starts=capture_stops=0;during_power=nullptr;bytes.clear();edges.clear();v12::test_status={};init(1,2,clock_read);
}
static void nmea(const char* body){unsigned x=0;for(const char* p=body;*p;++p)x^=uint8_t(*p);char end[8];std::snprintf(end,sizeof end,"*%02X\r\n",x);shadow_fake::bytes+="$";shadow_fake::bytes+=body;shadow_fake::bytes+=end;}
static void sample(unsigned i,bool source=true,bool permission=true,uint64_t root=10){
 using namespace shadow_fake;const auto target=1000000+uint64_t(i)*1000000;
 while(now+50000<target){now+=50000;watch::Model keep;keep.time_valid=true;keep.source=source?watch::Source::BLE:watch::Source::LOR;keep.sampled_us=now;keep.until_us=now+250000;watchshadow::selected(keep,now);watchshadow::poll(now);}
 now=target;
 watch::Model model;model.time_valid=true;model.source=source?watch::Source::BLE:watch::Source::LOR;model.sampled_us=now;model.until_us=now+250000;watchshadow::selected(model,now);
 auto& s=v12::test_status;s.requested=s.following=s.clock_valid=true;s.g1_clock_origin=s.root_identity=root;s.root_boot=20;s.source_epoch=1;s.publication=1;s.g1_relative_quality.scope_generation=1;s.g1_clock_model.tick=1000000;s.g1_clock_model.epoch=50000087;s.g1_anchor_deadline_us=s.g1_corrected_until_us=s.service_until_us=now+(permission?1500000:0);
 char rmc[100];std::snprintf(rmc,sizeof rmc,"GNRMC,1200%02u.00,A,3000,N,08600,W,0,0,171126,,,A",i%60);nmea(rmc);nmea("GNGGA,120000.00,3000,N,08600,W,1,11,0.8,0,M,0,M,,");
 // Deliver the RMC before the physical edge, then queue the independently
 // captured edge. The production witness map is observed before either.
 watchshadow::poll(now);now+=200000;model.sampled_us=now;model.until_us=now+250000;watchshadow::selected(model,now);
 edges.push_back({now*20,now,i+1,1,50,true});watchshadow::poll(now);
}
int main(){
 reset();watchshadow::visible(false);watchshadow::poll(shadow_fake::now);assert(watchshadow::released()&&!shadow_fake::power_calls);
 // Main may run during the PMU transaction: power acknowledgement must not
 // consume the generation and strand the face permanently in STARTING.
 shadow_fake::during_power=[](){watchshadow::poll(shadow_fake::now);};watchshadow::visible(true);shadow_fake::during_power=nullptr;watchshadow::poll(shadow_fake::now);
 assert(shadow_fake::uart&&shadow_fake::capture&&watchshadow::snapshot().powered);
 for(unsigned i=0;i<16;++i)sample(i);auto v=watchshadow::snapshot();assert(v.valid&&v.samples&&v.settling_ms==10000);
 sample(16,false);v=watchshadow::snapshot();assert(!v.valid&&!v.samples&&v.stage==watch::ShadowStage::WaitMesh);
 for(unsigned i=17;i<31;++i)sample(i);v=watchshadow::snapshot();assert(v.valid&&v.samples);
 // A short permission lapse between valid PPS edges hides the same-context
 // cohort. It resumes only on a newly scored pulse; expired leases never score.
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);
 for(unsigned i=0;i<21;++i){sample(i);shadow_fake::now+=300000;watchshadow::poll(shadow_fake::now);assert(!watchshadow::snapshot().valid);}
 sample(21);v=watchshadow::snapshot();assert(v.valid&&v.samples>=5);const auto retained=v.samples;
 sample(22,true,false);v=watchshadow::snapshot();assert(!v.valid&&v.samples==retained);
 sample(23);v=watchshadow::snapshot();assert(v.valid&&v.samples==retained+1);
 watch::Model reroute;reroute.time_valid=true;reroute.source=watch::Source::RC1;reroute.sampled_us=shadow_fake::now;reroute.until_us=shadow_fake::now+250000;
 watchshadow::selected(reroute,shadow_fake::now);assert(!watchshadow::snapshot().valid&&!watchshadow::snapshot().samples);
 sample(24,true,true,11);v=watchshadow::snapshot();assert(!v.valid&&!v.samples);
 sample(25,false);v=watchshadow::snapshot();assert(!v.valid&&!v.samples);
 sample(26);v=watchshadow::snapshot();assert(!v.valid&&!v.samples&&!v.settling_ms);
 watchshadow::visible(false);watchshadow::poll(shadow_fake::now);assert(watchshadow::released()&&!shadow_fake::uart&&!shadow_fake::capture);
 watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(!watchshadow::snapshot().samples&&!watchshadow::snapshot().valid);
 // A failed retirement remains a fault; a later success cannot rearm a new
 // UART/capture generation while that cleanup fault is retained.
 shadow_fake::fail_capture_stop=true;watchshadow::visible(false);watchshadow::poll(shadow_fake::now);assert(!watchshadow::released());const auto starts=shadow_fake::capture_starts;
 shadow_fake::fail_capture_stop=false;watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(shadow_fake::capture_starts==starts);
 // Retained UART-delete faults cannot rearm capture, either.
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);shadow_fake::fail_uart_stop=true;watchshadow::visible(false);watchshadow::poll(shadow_fake::now);const auto uart_before=shadow_fake::uart_starts;
 shadow_fake::fail_uart_stop=false;watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(shadow_fake::uart_starts==uart_before&&!shadow_fake::capture);
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);shadow_fake::fail_uart_stop=shadow_fake::fail_capture_stop=true;watchshadow::visible(false);watchshadow::poll(shadow_fake::now);assert(watchshadow::cleanup_error==1);
 shadow_fake::fail_capture_stop=false;watchshadow::poll(shadow_fake::now);assert(watchshadow::cleanup_error==1&&!watchshadow::released());
 // Maintenance preempting a delayed power acknowledgment cannot manufacture
 // an OFF proof; the next UI owner step must verify the actual rail OFF.
 reset();shadow_fake::during_power=[](){watchshadow::stop();assert(!watchshadow::control().off);};watchshadow::visible(true);shadow_fake::during_power=nullptr;assert(!watchshadow::released());watchshadow::visible(false);watchshadow::poll(shadow_fake::now);assert(watchshadow::released()&&!shadow_fake::capture_starts);
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);shadow_fake::fail_uart_read=true;watchshadow::poll(shadow_fake::now);watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(!shadow_fake::power&&watchshadow::start_error==7&&watchshadow::snapshot().stage==watch::ShadowStage::Fault);
 reset();shadow_fake::fail_alloc=true;watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(watchshadow::snapshot().stage==watch::ShadowStage::Fault&&!shadow_fake::uart_starts);
 reset();shadow_fake::fail_capture_start=true;watchshadow::visible(true);watchshadow::poll(shadow_fake::now);assert(!shadow_fake::uart&&!shadow_fake::capture&&watchshadow::snapshot().stage==watch::ShadowStage::Fault);
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);shadow_fake::healthy=false;watchshadow::poll(shadow_fake::now);watchshadow::visible(true);assert(!shadow_fake::power&&watchshadow::snapshot().stage==watch::ShadowStage::Fault);
 reset();watchshadow::visible(true);watchshadow::poll(shadow_fake::now);watchshadow::stop();watchshadow::visible(true);assert(!shadow_fake::power&&watchshadow::released());
 // Run the actual formatter with maximum widths and retain bytes for Python
 // protocol validation. Detailed data does not grow the normal UI row.
 watchshadow::shown={};watchshadow::shown.samples=watchshadow::shown.p99_ns=watchshadow::shown.rms_ns=watchshadow::shown.skipped=watchshadow::shown.missing=UINT32_MAX;watchshadow::shown.mean_tenth_ns=INT64_MIN;
 static v12::ConsoleFrame f;f.format("TWATCH_UI version=1 GNSS=FACE_AUTO precision=UNQUALIFIED");watchshadow::status(f);const auto compact=f.length;assert(compact<240);watchshadow::detail(f);assert(!f.failed&&f.rows==2&&f.length<1400);std::fwrite(f.bytes,1,f.length,stdout);
 reset();std::puts("PASS actual GNSS controller:PMU race,source withdrawal,real witness10s,OFF/reentry,partial start,cleanup fence,maintenance,format bounds");
}
