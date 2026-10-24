#include "../src/EffectBirthJournal.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>
#include <thread>
#include <latch>
using sc6::research::EffectBirthJournal;
using Buffer=std::array<unsigned char,0x1E0>;
static uintptr_t address(Buffer& b){return reinterpret_cast<uintptr_t>(b.data());}
static void put(Buffer& b,unsigned at,uint32_t word){std::memcpy(b.data()+at,&word,4);}
static uint32_t get(const Buffer& b,unsigned at){uint32_t word;std::memcpy(&word,b.data()+at,4);return word;}
static void advance(Buffer& b){
    float seed,step;std::memcpy(&seed,b.data()+0x138,4);std::memcpy(&step,b.data()+0x134,4);
    seed+=step;std::memcpy(b.data()+0x138,&seed,4);
}
int main(){
    { // Reproduce old one-shot branch failure; retry only contention, once entered.
        sc6::research::EffectMemoryGate gate;std::atomic<bool> stopped{};
        EffectBirthJournal branch;assert(branch.start(1,2,3));branch.seal();
        std::latch held(1),release(1),done(1);
        std::thread worker([&]{gate.release_decision([&]{held.count_down();release.wait();return true;});done.count_down();});
        held.wait();bool entered=false;
        assert(!gate.try_access(stopped,[&]{entered=true;assert(branch.resume_recording(0));}));
        assert(!entered && branch.branches==0 && branch.mode==EffectBirthJournal::Mode::sealed);
        unsigned attempts{},retries{};sc6::research::EffectMemoryGate::AccessOutcome outcome{};
        assert(branch.resume_bounded(gate,stopped,0,[&]{++retries;release.count_down();done.wait();return true;},4,attempts,outcome));
        worker.join();assert(attempts==2 && retries==1 && branch.branches==1 && outcome==sc6::research::EffectMemoryGate::AccessOutcome::entered);
        // An entered validation failure is never retried or silently accepted.
        assert(!branch.resume_bounded(gate,stopped,0,[&]{++retries;return true;},4,attempts,outcome));
        assert(attempts==1 && retries==1 && branch.branches==1);
        branch.seal();stopped=true;
        assert(!branch.resume_bounded(gate,stopped,0,[&]{++retries;return true;},4,attempts,outcome));
        assert(outcome==sc6::research::EffectMemoryGate::AccessOutcome::stopped && retries==1);
    }
    {
        sc6::research::EffectMemoryGate gate;std::atomic<bool> stopped{};
        EffectBirthJournal branch;assert(branch.start(1,2,3));branch.seal();
        std::latch held(1),release(1);std::thread worker([&]{gate.release_decision([&]{held.count_down();release.wait();return true;});});
        held.wait();unsigned attempts{},retries{};sc6::research::EffectMemoryGate::AccessOutcome outcome{};
        assert(!branch.resume_bounded(gate,stopped,0,[&]{++retries;return true;},3,attempts,outcome));
        assert(attempts==3 && retries==2 && outcome==sc6::research::EffectMemoryGate::AccessOutcome::contended);
        assert(branch.mode==EffectBirthJournal::Mode::sealed && branch.branches==0);
        release.count_down();worker.join();
        assert(branch.resume_bounded(gate,stopped,0,[]{return false;},3,attempts,outcome) && branch.branches==1);
    }

    // All supported constructor paths participate in sequence validation. Only
    // Stage has an audited allocation-entry seed; other payloads stay untouched.
    for(const auto& site:sc6::research::effect_allocation_sites){
        EffectBirthJournal all;Buffer original,replay;
        original.fill(0x37);replay.fill(0xa5);const auto before=replay;
        assert(all.start(1,2,3));
        assert(all.allocation(address(original),site.bytes,site.caller));
        all.seal();assert(all.begin(0,1,1,2,3));
        assert(all.allocation(address(replay),site.bytes,site.caller));assert(all.finish());
        if(site.type!=1)assert(replay==before && all.applied_count==0);
        else assert(all.applied_count==1);
        assert(all.events[0].type==site.type && all.cursor==1);
    }
    // Preserve exact bits, including signed zero and unusual values; no range guess.
    for(uint32_t seed: {0U,0x80000000U,0xc412fdbaU,0x41996f18U,0x7fc00001U,0xffffffffU}){
        EffectBirthJournal j;Buffer original{},replay;replay.fill(0xa5);put(original,0x138,seed);
        const auto untouched=original;assert(j.start(7,0,1));
        assert(j.allocation(address(original),0x1E0,0x334A1E));assert(original==untouched);
        j.seal();assert(j.begin(0,1,7,0,1));const auto before=replay;
        assert(j.allocation(address(replay),0x1E0,0x334A1E));assert(j.finish());
        for(unsigned i=0;i<replay.size();++i)assert(replay[i]==(i>=0x138 && i<0x13c?original[i]:before[i]));
        assert(j.applied[0].saved==seed && j.applied[0].after==seed && j.intervals==1);
        put(original,0x134,0x3d240108);put(replay,0x134,0x3d240108);
        advance(original);advance(replay);assert(get(original,0x138)==get(replay,0x138));
    }
    Buffer a{},b{};put(a,0x138,0x12345678);EffectBirthJournal j;assert(j.start(1,2,3));
    assert(j.allocation(address(a),0x180,0x2FC720));
    assert(j.allocation(address(a),0x1E0,0x334A1E));
    j.seal();assert(j.begin(0,1,1,2,3));auto before=b;
    assert(j.allocation(address(b),0x180,0x2FC720) && b==before);assert(j.finish());
    assert(j.begin(1,2,1,2,3));assert(j.allocation(address(b),0x1E0,0x334A1E));assert(j.finish());
    assert(get(b,0x138)==0x12345678);assert(j.begin(0,0,1,2,3) && j.finish());
    // Wrong epoch/interval, missing births, wrong order and extra births fail.
    for(unsigned test=0;test<5;++test){
        EffectBirthJournal bad;assert(bad.start(1,2,3));assert(bad.allocation(address(a),0x1E0,0x334A1E));bad.seal();b={};
        if(test==0)assert(!bad.begin(0,1,1,9,3));
        if(test==1)assert(!bad.begin(0,2,1,2,3));
        if(test==2){assert(bad.begin(0,1,1,2,3));assert(!bad.finish());}
        if(test==3){assert(bad.begin(0,1,1,2,3));assert(!bad.allocation(address(b),0x130,0x334516));}
        if(test==4){assert(bad.begin(0,0,1,2,3));assert(!bad.allocation(address(b),0x1E0,0x334A1E));}
        assert(bad.failure && get(b,0x138)==0);
    }
    EffectBirthJournal full;assert(full.start(1,2,3));
    for(unsigned i=0;i<full.capacity;++i)assert(full.allocation(address(a),0x1E0,0x334A1E));
    assert(!full.allocation(address(a),0x1E0,0x334A1E) && full.failure==6);
    full.stop();assert(full.start(1,2,4) && full.count==0 && !full.failure);
    assert(!full.allocation(1,0x1E0,0x334A1E) && full.failure==7);
    std::ostringstream report;j.report(report);assert(report.str().find("suite_wind_birth_applied=1")!=std::string::npos);
    // Rolling retention must not retain an unbounded match allocation log.
    EffectBirthJournal rolling;assert(rolling.start(1,2,3));
    for(unsigned frame=0;frame<7200;++frame){
        assert(rolling.allocation(address(a),0x180,0x2FC720));
        if(rolling.count>4)assert(rolling.discard_prefix(rolling.count-4));
    }
    assert(rolling.count==4 && !rolling.failure);
    rolling.seal();assert(!rolling.discard_prefix(1));
    assert(!rolling.resume_recording(5));assert(rolling.resume_recording(2));
    assert(rolling.count==2 && rolling.mode==EffectBirthJournal::Mode::record);
    // Different corrected allocation sequence is recorded, not compared to or
    // seeded from the discarded prediction. No writes to the fresh allocation.
    const auto unchanged=b;assert(rolling.allocation(address(b),0x130,0x334516));
    assert(b==unchanged && rolling.count==3 && rolling.events[2].caller==0x334516);
    assert(!rolling.discard_prefix(4));assert(rolling.discard_prefix(2));
    assert(rolling.count==1 && rolling.events[0].caller==0x334516);
    // A worker holding the release bookkeeping gate is not journal corruption.
    // Defer without touching entries/cursors, then discard exactly once later.
    {
        sc6::research::EffectMemoryGate gate;std::atomic<bool> stopped{};
        EffectBirthJournal maintenance;assert(maintenance.start(1,2,3));
        for(unsigned i=0;i<3;++i)assert(maintenance.allocation(address(a),0x180,0x2FC720));
        const auto original=maintenance.events;
        std::array<unsigned,3> retained{1,2,3};
        std::latch locked(1),release(1);
        std::thread worker([&]{gate.release_decision([&]{locked.count_down();release.wait();return false;});});
        locked.wait();
        auto result=maintenance.try_discard_prefix(gate,stopped,1);
        assert(result==EffectBirthJournal::Compaction::deferred);
        assert(maintenance.count==3 && maintenance.discarded==0 && !maintenance.failure);
        assert(std::memcmp(original.data(),maintenance.events.data(),sizeof(original))==0);
        assert(retained[0]==1 && retained[2]==3);
        release.count_down();worker.join();
        result=maintenance.try_discard_prefix(gate,stopped,1);
        assert(result==EffectBirthJournal::Compaction::applied);
        for(auto& cursor:retained)--cursor;
        assert(maintenance.count==2 && maintenance.discarded==1 && retained[0]==0 && retained[2]==2);
        assert(maintenance.try_discard_prefix(gate,stopped,0)==EffectBirthJournal::Compaction::applied);
        assert(maintenance.discarded==1);
        assert(maintenance.try_discard_prefix(gate,stopped,3)==EffectBirthJournal::Compaction::rejected);
        maintenance.seal();assert(maintenance.try_discard_prefix(gate,stopped,0)==EffectBirthJournal::Compaction::rejected);
        stopped=true;assert(maintenance.try_discard_prefix(gate,stopped,0)==EffectBirthJournal::Compaction::stopped);
        assert(maintenance.count==2 && maintenance.discarded==1);
    }
    std::puts("Effect birth journal tests passed");
}
