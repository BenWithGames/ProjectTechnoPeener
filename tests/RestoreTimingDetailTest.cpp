#include "../src/ConfirmationAge.hpp"
#include "../src/PostloadReadiness.hpp"
#include "../src/NativeRestoreDiagnostic.hpp"
#include "../src/NativeRollbackTiming.hpp"
#include "../src/ReportMatchId.hpp"
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <iostream>
using namespace sc6::research;
int main(){
 assert(sc6::automatic_report::match_id(1,2)=="00000000000000010000000000000002");
 assert(sc6::automatic_report::match_id(1,2)==sc6::automatic_report::match_id(2,1));
 assert(sc6::automatic_report::match_id(1,3)!=sc6::automatic_report::match_id(1,2));
 assert(sc6::automatic_report::match_id(0,2).empty());
 assert(sc6::automatic_report::match_id(1,0).empty());
 for(unsigned fail=0;fail<=9;++fail){
  PostloadReadiness d;unsigned calls=0;bool ok=true;
  for(unsigned step=1;step<=9 && ok;++step)ok=d.enter(step) && (++calls,step!=fail) && d.pass(step);
  assert(ok==(fail==0));assert(calls==(fail?fail:9));
  assert(d.rejected()==fail);assert(d.passed==((1u<<(fail?fail-1:9))-1));
 }
 static_assert(std::size(NativeRestoreDiagnostic::names)==NativeRestoreDiagnostic::count);
 assert(std::string(NativeRestoreDiagnostic::names[NativeRestoreDiagnostic::postload_rejected])=="postload_rejected");
 ConfirmationAge a;a.frequency=1000000;
 a.observe(10,13,1000,5000,6000,7000);
 assert(a.buckets[0].samples==1 && a.buckets[0].total==1000 && a.buckets[1].total==1000);
 assert(a.buckets[2].total==4000 && a.buckets[3].total==6000 && a.behind_frames[3]==1);
 a.observe(11,11,7000,5000,6000,8000);assert(a.early==1 && a.buckets[2].samples==1);
 a.observe(12,11,0,5000,6000,8000);assert(a.unpublished==1);
 a.observe(13,13,1000,0,0,8000);assert(a.unmeasured==1);
 a.observe(14,14,1000,8000,6000,9000);assert(a.invalid==1);
 a.observe(15,15,1000,5000,8000,7000);assert(a.invalid==2);
 assert(a.buckets[0].samples==3);
 std::ostringstream out;a.write(out);assert(out.str().find("confirmation_age_network_latency=0")!=std::string::npos);
 NativeRollbackTiming t;unsigned called=0;
 assert(NativeRollbackTiming::measure(NativeRollbackTiming::restore_native_load,[&]{++called;return 42;})==42 && called==1);
 assert(t.buckets[NativeRollbackTiming::restore_native_load].calls==0);
 {NativeRollbackTiming::Binding binding(&t,123);
  assert(!NativeRollbackTiming::measure(NativeRollbackTiming::restore_native_load,[&]{++called;return false;}));
  NativeRollbackTiming::measure(NativeRollbackTiming::restore_native_resave,[&]{++called;});
  try{NativeRollbackTiming::measure(NativeRollbackTiming::restore_payload_compare,[]{throw std::runtime_error("test");});assert(false);}catch(const std::runtime_error&){}
 }
 assert(called==3 && t.buckets[NativeRollbackTiming::restore_native_load].calls==1 && t.buckets[NativeRollbackTiming::restore_native_resave].calls==1);
 assert(t.buckets[NativeRollbackTiming::restore_payload_compare].calls==1 && NativeRollbackTiming::active==nullptr);
 std::ostringstream report;t.write(report);assert(report.str().find("continuous_timing_restore_native_load_calls=1")!=std::string::npos);
 std::cout<<"PASS: local confirmation intervals, early/missing/invalid timestamps, depth buckets, timed return values, exceptions and binding restoration\n";
}
