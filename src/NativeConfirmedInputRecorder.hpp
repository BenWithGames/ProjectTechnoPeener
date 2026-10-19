#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace sc6::research {
// 3F6600 publishes raw pairs through 3D73F0 and advances 3B0. Neither its
// tagged lookup nor 3D73F0 verifies remote confirmation. An owned clock must
// therefore replace this publisher before it can advance speculatively.
struct NativeConfirmedRecorderRoute {
    uintptr_t clock{};
    void* context{};
    bool (*service)(void*,uintptr_t){};
};
inline std::atomic<const NativeConfirmedRecorderRoute*> native_confirmed_recorder_route{};
inline std::atomic<uintptr_t> native_confirmed_recorder_hook_base{};
struct NativeConfirmedRecorderCalls {
    static void append(uintptr_t base,uintptr_t object,const uint32_t* raw){
        reinterpret_cast<void(*)(uintptr_t,uintptr_t,const uint32_t*)>(base+0x3D73F0)(object,object+0x43C0,raw);
    }
    static void flush(uintptr_t base,uintptr_t object){
        reinterpret_cast<void(*)(uintptr_t,uintptr_t,unsigned)>(base+0x3D7330)(object,object+0x43C0,0);
    }
};
template<class Calls=NativeConfirmedRecorderCalls> class NativeConfirmedInputRecorder {
    uintptr_t base_{},clock_{};unsigned owner_{},identity_{},origin_{};
    bool bound_{},failed_{};
    bool fail(){failed_=true;return false;}
    bool scope()const{
        return GetCurrentThreadId()==owner_ && *reinterpret_cast<const uintptr_t*>(clock_)==base_+0x3283578 &&
            *reinterpret_cast<const uint32_t*>(clock_+0x3A0)==identity_ &&
            *reinterpret_cast<const uint32_t*>(clock_+0x398)==2 && *reinterpret_cast<const uint8_t*>(clock_+0x4404)==1;
    }
public:
    uint64_t appended{},already_published{},flushes{};
    bool service()noexcept{
        if(!bound_ || failed_)return false;
        __try{
            if(!scope())return fail();
            const auto cursor=*reinterpret_cast<const int32_t*>(clock_+0x3B0);
            auto& stop=*reinterpret_cast<int32_t*>(clock_+0x3B4);
            if(cursor>=0 && stop>=0 && cursor>=stop){stop=-1;Calls::flush(base_,clock_);++flushes;}
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail();}
    }
    bool bind(uintptr_t base,uintptr_t clock,unsigned owner,unsigned identity,unsigned origin)noexcept{
        if(bound_ || !base || !clock || !owner || origin>INT32_MAX)return false;
        base_=base;clock_=clock;owner_=owner;identity_=identity;origin_=origin;
        __try {
            if(!scope())return fail();
            const auto cursor=*reinterpret_cast<const int32_t*>(clock_+0x3B0);
            // Disabled recording is supported; a preceding publication needs
            // exact authoritative-ring proof when that first pair is supplied.
            if(cursor>=0 && (uint32_t(cursor)<origin || uint64_t(cursor)>uint64_t(origin)+1))return fail();
            if(cursor>=0 && uint64_t(cursor)==uint64_t(origin)+1){
                for(unsigned side=0;side<2;++side){const auto entry=clock+0x3C0+side*0x2000+(origin&511u)*16;
                    if(*reinterpret_cast<const uint32_t*>(entry)!=identity ||
                       *reinterpret_cast<const uint32_t*>(entry+4)!=origin ||
                       *reinterpret_cast<const uint8_t*>(entry+12)!=1)return fail();
                }
            }
            bound_=true;return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail();}
    }
    // A true result also covers a disabled recorder. Never read predictions
    // from the native ring and never move its confirmation cursors/listeners.
    bool publish(uint32_t frame,const std::array<uint32_t,2>& raw)noexcept{
        if(!bound_ || failed_ || !service())return false;
        __try {
            if(!scope() || frame<origin_)return fail();
            auto& cursor=*reinterpret_cast<int32_t*>(clock_+0x3B0);
            const auto stop=*reinterpret_cast<const int32_t*>(clock_+0x3B4);
            if(cursor<0)return true;
            if(cursor>int64_t(frame)){
                if(frame!=origin_ || uint64_t(cursor)!=uint64_t(frame)+1)return fail();
                for(unsigned side=0;side<2;++side){
                    const auto entry=clock_+0x3C0+side*0x2000+(frame&511u)*16;
                    if(*reinterpret_cast<const uint32_t*>(entry)!=identity_ ||
                       *reinterpret_cast<const uint32_t*>(entry+4)!=frame ||
                       *reinterpret_cast<const uint8_t*>(entry+12)!=1 ||
                       *reinterpret_cast<const uint32_t*>(entry+8)!=raw[side])return fail();
                }
                ++already_published;return true;
            }
            if(cursor!=int64_t(frame) || frame==INT32_MAX)return fail();
            if(stop>=0 && cursor>=stop)return fail();
            Calls::append(base_,clock_,raw.data());
            if(!scope() || cursor!=int64_t(frame))return fail();
            ++cursor;++appended;
            return service();
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail();}
    }
    bool failed()const{return failed_;}
};
}
