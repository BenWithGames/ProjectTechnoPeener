#pragma once
#include "NativeRollbackWindow.hpp"
#include "RoundEventObserver.hpp"
#include "CombatUpdateAdapter.hpp"
#include "NativeFilterFramePreparation.hpp"
namespace sc6::research {
struct NativeBattleBoundaryAccess {
    static bool counter(uintptr_t base,unsigned& value)noexcept{
        __try {value=*reinterpret_cast<const unsigned*>(base+0x470D0C4);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static auto round(){return round_event::snapshot();}
    static bool input(uintptr_t argument,std::array<std::byte,40>& bytes){return FullStateProbe::input_bytes(argument,bytes);}
};
template<class Access=NativeBattleBoundaryAccess> class NativeBattleBoundaryReader {
    uintptr_t base_{},caller_{};RollbackBoundaryKey identity_{};bool bound_{},failed_{};
    bool fail(){failed_=true;return false;}
public:
    bool bind(uintptr_t base,uintptr_t caller,const RollbackBoundaryKey& identity,bool image_verified){
        if(bound_ || failed_ || !base || !image_verified || caller<base || caller-base>=0x4900000 ||
            !identity.same_round(identity) || identity.thread!=GetCurrentThreadId())return false;
        base_=base;caller_=caller;identity_=identity;bound_=true;return true;
    }
    template<class Boundary> bool read(uintptr_t argument,uintptr_t caller,Boundary& out){
        out={};
        if(!bound_ || failed_ || caller!=caller_ || GetCurrentThreadId()!=identity_.thread)return fail();
        const auto before=Access::round();unsigned first{},last{};std::array<std::byte,40> bytes{};
        if(before.round!=identity_.round || before.counter!=identity_.event || before.generation!=identity_.generation ||
            !Access::counter(base_,first) || !Access::input(argument,bytes) || !RecordedInput::valid_envelope(bytes) ||
            !Access::counter(base_,last))return fail();
        const auto after=Access::round();
        if(first!=last || first<identity_.frame || first==UINT32_MAX || before.round!=after.round || before.counter!=after.counter ||
            before.generation!=after.generation)return fail();
        out.session=identity_.session;out.round=identity_.round;out.frame=first;
        std::memcpy(out.local.envelope.data(),bytes.data(),40);
        if(auto* preparation=native_filter_preparation_route.load(std::memory_order_acquire)){
            if(!preparation->context || !preparation->complete ||
               !preparation->complete(preparation->context,first,out.local.envelope))return fail();
        }
        return true;
    }
    // Reading seals the native filter pair and prepares the coordinator token.
    // The final receipt drain must follow that callback, before any capture.
    template<class Boundary,class Coordinator> bool read_for_execution(uintptr_t argument,uintptr_t caller,
        Boundary& out,Coordinator& coordinator,uint64_t owner){
        return read(argument,caller,out) && coordinator.before_execute(owner);
    }
    template<class Boundary> static bool callback(void* context,uintptr_t argument,uintptr_t caller,Boundary& out){
        return static_cast<NativeBattleBoundaryReader*>(context)->read(argument,caller,out);
    }
    bool failed()const{return failed_;}
    bool frontier(unsigned frame){
        if(!bound_ || failed_ || GetCurrentThreadId()!=identity_.thread)return false;
        const auto before=Access::round();unsigned counter{};
        if(!Access::counter(base_,counter))return false;
        const auto after=Access::round();
        return counter==frame && before.round==identity_.round && before.counter==identity_.event && before.generation==identity_.generation &&
            after.round==before.round && after.counter==before.counter && after.generation==before.generation;
    }
};
}
