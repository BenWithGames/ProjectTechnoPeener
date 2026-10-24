#include "../src/NativeOuterTiming.hpp"
#include <cassert>
#include <memory>
#include <sstream>
#include <thread>
using sc6::research::NativeOuterTiming;
int main(){
 alignas(8) std::array<unsigned char,0x1500> outer{};alignas(8) std::array<unsigned char,0x400> clock{};
 const auto address=reinterpret_cast<uintptr_t>(outer.data());
 *reinterpret_cast<uintptr_t*>(outer.data()+0x478)=reinterpret_cast<uintptr_t>(clock.data());
 *reinterpret_cast<uint32_t*>(clock.data()+0x3A0)=7;*reinterpret_cast<uint32_t*>(clock.data()+0x3A4)=43;
 *reinterpret_cast<uint32_t*>(outer.data()+0x1488)=7;*reinterpret_cast<uint32_t*>(outer.data()+0x148C)=42;outer[0x1461]=2;
 NativeOuterTiming::clock_counts.calls=8;NativeOuterTiming::clock_counts.advanced=6;
 const auto observation=NativeOuterTiming::observe(address);
 assert(observation.valid && observation.frame==43 && observation.consumed_frame==42 && observation.phase==2);
 assert(observation.dispatch.calls==8 && observation.dispatch.advanced==6);
 assert(!NativeOuterTiming::observe(1).valid);
 auto t=std::make_unique<NativeOuterTiming>();t->frequency=60000;
 t->enter(1,100);t->phase(6,10,20);t->returned(1,500);
 t->enter(1,1100);t->phase(0,11,100);t->phase(1,9,1100);t->phase(5,9,30);t->phase(6,11,20);t->returned(1,2400);
 t->enter(1,3100);t->phase(6,12,20);t->returned(1,3500);
 NativeOuterTiming::Worker worker;std::ostringstream csv,summary;worker.drain(*t,csv);worker.write(*t,summary);
 assert(worker.cursor==3 && worker.duration[1].calls==1 && worker.duration[1].ticks==1300 && worker.duration[1].slow==1);
 assert(worker.interval[1].calls==1 && worker.interval[1].ticks==2000);
 assert(worker.outside[1].ticks==700); // Outside native update, not restore overhead.
 assert(t->rows[1].row.capture_ticks==100 && t->rows[1].row.restore_ticks==1100 && t->rows[1].row.live==1);
 const auto length=csv.str().size();worker.drain(*t,csv);assert(csv.str().size()==length);
 t->enter(1,4100);t->returned(1,4200); // Empty outer calls must remain visible after tracking starts.
 t->enter(1,5100);t->phase(6,13,20);t->returned(1,5500);assert(t->rows[3].row.previous_begin==3100 && !t->rows[3].row.marked);
 assert(t->rows[4].row.previous_begin==4100 && t->rows[4].row.marked);
 worker.drain(*t,csv);assert(worker.cursor==5);
 assert(!NativeOuterTiming::observe(0).valid);
 t->enter(1,6000);t->enter(1,6100);t->returned(1,6200);t->returned(1,6300);assert(t->invalid==1);
 t->enter(1,7000);t->phase(6,14,20);t->returned(1,6900);assert(t->invalid==2);
 t->reserved=t->consumed.load()+NativeOuterTiming::capacity;t->enter(1,8000);t->phase(6,15,20);t->returned(1,8500);assert(t->dropped==1);
 // Multiple wraps remain publishable after draining; no permanent saturation.
 auto ring=std::make_unique<NativeOuterTiming>();NativeOuterTiming::Worker rw;std::ostringstream rc;
 NativeOuterTiming::local={};
 for(unsigned n=0;n<NativeOuterTiming::capacity*3;++n){
  const uint64_t stamp=100+uint64_t(n)*100;
  ring->enter(1,stamp);ring->checkpoint(NativeOuterTiming::entry_recovery,stamp+10);
  ring->checkpoint(NativeOuterTiming::to_battle_return,stamp+30);
  ring->checkpoint(NativeOuterTiming::post_battle_callback,stamp+40);
  // A second native combat-loop iteration has its own additive segments.
  ring->checkpoint(NativeOuterTiming::to_battle_return,stamp+50);
  ring->checkpoint(NativeOuterTiming::post_battle_callback,stamp+55);
  ring->phase(6,n,1);ring->returned(1,stamp+60);
  if(n%128==127)rw.drain(*ring,rc);
 }
 rw.drain(*ring,rc);assert(rw.cursor==NativeOuterTiming::capacity*3 && !ring->dropped);
 assert(rw.slices[NativeOuterTiming::entry_recovery][0].ticks==uint64_t(rw.cursor)*10);
 assert(rw.slices[NativeOuterTiming::to_battle_return][0].ticks==uint64_t(rw.cursor)*30);
 assert(rw.slices[NativeOuterTiming::post_battle_callback][0].ticks==uint64_t(rw.cursor)*15);
 assert(rw.slices[NativeOuterTiming::native_tail][0].ticks==uint64_t(rw.cursor)*5);
 // A genuinely full unread queue drops, then recovers when the worker drains.
 auto full=std::make_unique<NativeOuterTiming>();NativeOuterTiming::Worker fw;std::ostringstream fc;
 NativeOuterTiming::local={};
 for(unsigned n=0;n<=NativeOuterTiming::capacity;++n){full->enter(1,1+n*10);full->phase(6,n,1);full->returned(1,2+n*10);}
 assert(full->dropped==1);fw.drain(*full,fc);
 full->enter(1,1000000);full->phase(6,0,1);full->returned(1,1000001);fw.drain(*full,fc);
 assert(fw.cursor==NativeOuterTiming::capacity+1 && full->dropped==1);
 NativeOuterTiming::local={};
 // Concurrent producers and the report worker must never overwrite unread rows.
 auto concurrent=std::make_unique<NativeOuterTiming>();NativeOuterTiming::Worker cw;std::ostringstream cc;
 std::atomic<unsigned> done{};constexpr unsigned each=20000;
 auto produce=[&](unsigned identity){NativeOuterTiming::local={};
  for(unsigned i=0;i<each;++i){const uint64_t stamp=100+uint64_t(i)*10;
   concurrent->enter(identity,stamp);concurrent->phase(6,i,1);concurrent->returned(identity,stamp+5);
  }++done;
 };
 std::thread first(produce,1),second(produce,2);
 while(done.load()!=2){cw.drain(*concurrent,cc);std::this_thread::yield();}
 first.join();second.join();cw.drain(*concurrent,cc);
 assert(cw.cursor+concurrent->dropped.load()==2*each);
 assert(cw.duration[0].calls==cw.cursor && cw.duration[0].ticks==uint64_t(cw.cursor)*5 && !concurrent->invalid);
 NativeOuterTiming::local={};
 // Admission delay is attributed only to the matching clock/frame and a
 // single live update; stale prior-round stamps must not enter the metric.
 auto gaps=std::make_unique<NativeOuterTiming>();gaps->frequency=60000;
 auto state=observation;state.dispatch.advance_qpc=100;
 state.dispatch.advance_clock=state.clock;state.dispatch.advance_identity=state.identity;state.dispatch.advance_frame=state.frame;
 gaps->enter(1,1100,state);gaps->phase(6,1,1);gaps->returned(1,1200,state);
 ++state.dispatch.advance_identity;
 gaps->enter(1,2100,state);gaps->phase(6,2,1);gaps->returned(1,2200,state);
 NativeOuterTiming::Worker gw;std::ostringstream gc;gw.drain(*gaps,gc);
 assert(gw.admission_gap[0].calls==1 && gw.admission_gap[0].ticks==1000);
 assert(gc.str().find("before_admission_qpc")!=std::string::npos);
 assert(summary.str().find("outer_duration_corrected_mean_us=")!=std::string::npos);
}
