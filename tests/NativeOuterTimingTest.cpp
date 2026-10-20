#include "../src/NativeOuterTiming.hpp"
#include <cassert>
#include <memory>
#include <sstream>
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
 t->reserved=NativeOuterTiming::capacity;t->enter(1,8000);t->phase(6,15,20);t->returned(1,8500);assert(t->dropped==1);
 assert(summary.str().find("outer_duration_corrected_mean_us=")!=std::string::npos);
}
