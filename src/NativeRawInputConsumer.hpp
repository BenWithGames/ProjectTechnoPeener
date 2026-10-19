#pragma once
#include "RawInputHistoryView.hpp"
#include <atomic>
namespace sc6::research {
// Scoped consumer at the 3F0720 entry. Only the 3FCD10 input-formation caller
// (return address 3FCDCE) can receive predictions; recorder/other callers retain
// the original lookup. Clock-to-battle origins must come from verified admission.
class NativeRawInputConsumer {
public:
    enum class Result {pass,delivered,failed};
private:
    const RawInputHistoryView* history_{};
    uintptr_t clock_{},caller_{};
    uint64_t session_{},round_{},event_{};
    uint32_t identity_{},clock_origin_{},battle_origin_{},owner_{};
    std::atomic<bool> failed_{};
public:
    std::atomic<uint64_t> delivered{},rejected{};
    bool bind(const RawInputHistoryView& history,uintptr_t clock,uintptr_t caller,uint32_t identity,
              uint32_t clock_origin,uint32_t battle_origin,uint64_t session,uint64_t round,uint64_t event){
        if(history_ || failed_ || !clock || !caller || !history.identity(session,round,event))return false;
        history_=&history;clock_=clock;caller_=caller;identity_=identity;clock_origin_=clock_origin;
        battle_origin_=battle_origin;session_=session;round_=round;event_=event;owner_=GetCurrentThreadId();return true;
    }
    Result read(uintptr_t clock,uintptr_t caller,unsigned slot,uint32_t identity,uint32_t frame,uint32_t& raw){
        raw=0;if(!history_ || clock!=clock_ || caller!=caller_)return Result::pass;
        if(failed_ || GetCurrentThreadId()!=owner_ || identity!=identity_ || frame<clock_origin_ ||
           frame-clock_origin_>UINT32_MAX-battle_origin_ ||
           !history_->raw(session_,round_,event_,battle_origin_+(frame-clock_origin_),slot,raw)){
            failed_=true;++rejected;return Result::failed;
        }
        ++delivered;return Result::delivered;
    }
    bool failed()const{return failed_;}
};
inline std::atomic<NativeRawInputConsumer*> native_raw_input_consumer{};
inline std::atomic<uintptr_t> native_raw_input_hook_base{};
}
