#pragma once
#include "../src/NativeOuterContinuationFrame.hpp"
#include <cassert>
struct OuterFrameAccessFixture {
    static inline std::array<std::byte,0x1500> owner{};
    static inline std::array<std::byte,0x400> clock{};
    static inline std::array<std::byte,16> players{};
    static inline std::array<uintptr_t,3> header{};
    static inline unsigned reads{},fail_at{},change_at{};
    template<class T> static void put(unsigned offset,T value){std::memcpy(owner.data()+offset,&value,sizeof(value));}
    static bool read(uintptr_t source,void* target,size_t size){
        ++reads;if(reads==fail_at)return false;
        if(reads==change_at){uint32_t frame=900;std::memcpy(clock.data()+0x3A4,&frame,4);}
        auto copy=[&](const auto& data){const auto start=reinterpret_cast<uintptr_t>(data.data());
            if(source<start || source-start>sizeof(data) || size>sizeof(data)-(source-start))return false;
            std::memcpy(target,reinterpret_cast<const void*>(source),size);return true;};
        return copy(owner)||copy(clock)||copy(players)||copy(header);
    }
    static auto reset(){
        owner={};clock={};players={};header={};reads=fail_at=change_at=0;
        const auto address=reinterpret_cast<uintptr_t>(owner.data());
        put(0x478,reinterpret_cast<uintptr_t>(clock.data()));put(0x1450,uintptr_t{123});
        put(0x14A8,reinterpret_cast<uintptr_t>(players.data()));put(0x14B0,int32_t{2});
        header={reinterpret_cast<uintptr_t>(players.data()),reinterpret_cast<uintptr_t>(players.data()+8),address+0x14C8};
        return sc6::research::NativeOuterContinuationFrame::Registers{address,reinterpret_cast<uintptr_t>(header.data()),3,2,1,0};
    }
};
inline void test_native_outer_continuation_frame(){
    using F=OuterFrameAccessFixture;using Frame=sc6::research::NativeOuterContinuationFrame;
    auto registers=F::reset();F::players[0]=std::byte{0x21};F::owner[0x14C8]=std::byte{0x17};
    Frame frame;assert(frame.capture<F>(registers));
    const auto* header=reinterpret_cast<const uintptr_t*>(frame.argument());
    assert(header[0]==reinterpret_cast<uintptr_t>(frame.bytes().data()));
    assert(header[1]==header[0]+8 && header[2]==header[0]+16);
    F::players={};F::owner={};F::header={}; // No saved pointer into original stack or inputs.
    assert(frame.bytes()[0]==std::byte{0x21} && frame.bytes()[16]==std::byte{0x17});
    assert(frame.registers().remaining==3 && frame.registers().reverse_index==2 && frame.registers().suppress_edges==1);
    assert(!frame.capture<F>(registers));
    // Every read failure is rejected before publishing an input argument.
    for(unsigned at=1;at<=16;++at){auto r=F::reset();F::fail_at=at;Frame bad;
        assert(!bad.capture<F>(r) && !bad.argument());}
    {auto r=F::reset();F::change_at=10;Frame bad;assert(!bad.capture<F>(r) && bad.failure()==Frame::Failure::changed);}
    {auto r=F::reset();F::header[2]++;Frame bad;assert(!bad.capture<F>(r) && bad.failure()==Frame::Failure::pointers);}
    {auto r=F::reset();r.reverse_index=3;Frame bad;assert(!bad.capture<F>(r) && bad.failure()==Frame::Failure::loop);}
    {auto r=F::reset();r.remaining=0;Frame bad;assert(!bad.capture<F>(r));}
    {auto r=F::reset();r.suppress_held=2;Frame bad;assert(!bad.capture<F>(r));}
    {auto r=F::reset();F::put(0x14B0,int32_t{3});Frame bad;assert(!bad.capture<F>(r));}
    {auto r=F::reset();F::put(0x14D0,uint32_t{0x7fc00000});Frame bad;assert(!bad.capture<F>(r) && bad.failure()==Frame::Failure::envelope);}
}
