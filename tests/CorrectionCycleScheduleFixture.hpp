#pragma once
#include "../src/CorrectionCycleSchedule.hpp"
inline void test_correction_cycle_schedule(){
    using S=sc6::research::CorrectionCycleSchedule;
    // Both ordering possibilities: peer retirement before/after local work.
    for(bool peer_first:{false,true}){
        S s;assert(s.start(17,2,100,3,120));auto old=s.ticket();
        for(unsigned cycle=1;cycle<=3;++cycle){
            const auto t=s.ticket();assert(t.cycle==cycle);
            assert(!s.admit(t,2,t.first,false));
            if(peer_first)assert(s.peer_result(t,true,true));
            assert(s.admit(t,2,t.first,true));
            assert(!s.advance(2,t.first+3));
            assert(s.local_result(t,true,true));
            if(!peer_first){assert(!s.advance(2,t.first+3));assert(s.peer_result(t,true,true));}
            assert(s.advance(2,t.first+3));
            assert(!s.peer_result(old,true,true));old=t;
        }
        assert(s.state()==S::State::complete);
    }
    for(unsigned fault=0;fault<5;++fault){
        S s;assert(s.start(17,2,100,2,120));auto t=s.ticket();
        assert(s.admit(t,2,100,true));
        if(fault==0)assert(!s.local_result(t,true,false));
        if(fault==1)assert(!s.peer_result(t,false,true));
        if(fault==2)assert(!s.observe(3,103));
        if(fault==3){assert(s.local_result(t,true,true));assert(s.peer_result(t,true,true));assert(!s.advance(2,220));}
        if(fault==4)s.cancel();
        assert(s.state()==S::State::failed && !s.advance(2,103));
    }
    S expired;assert(expired.start(1,1,10,2,4));assert(!expired.observe(1,11));
    S overflow;assert(!overflow.start(1,1,UINT32_MAX-2,1,4));
    S invalid;assert(!invalid.start(1,1,10,17,4));assert(!invalid.start(1,1,10,2,3));
    std::cout<<"PASS: repeated correction scheduling, fresh tickets, bilateral retirement, failure and round cancellation\n";
}
