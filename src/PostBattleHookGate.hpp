#pragma once
#include <atomic>
#include <cstdint>
namespace sc6::research {
// The callback must settle synchronously on the original native stack. Only
// explicit permission allows the trampoline (and thus the outer loops) to run.
// This gate does not certify that the continuation has committed.
class PostBattleHookGate {
public:
    enum class Result {permit,failed};
    enum class Failure {none,identity,reentry,callback,exception};
    using Callback=Result(*)(void*,uintptr_t,uintptr_t);
private:
    unsigned owner_{};
    void* context_{};
    Callback callback_{};
    bool entered_{};
    void fail(Failure why)noexcept{
        unsigned empty=0;failure.compare_exchange_strong(empty,static_cast<unsigned>(why));
    }
public:
    std::atomic<unsigned> failure{};
    std::atomic<uint64_t> calls{},permitted{},contained{};
    PostBattleHookGate(unsigned owner,void* context,Callback callback):owner_(owner),context_(context),callback_(callback){}
    bool valid()const{return owner_ && context_ && callback_;}
    unsigned owner()const{return owner_;}
    template<class Stream> void write(Stream& out)const{
        out<<"continuous_post_battle_calls="<<calls.load()
           <<"\ncontinuous_post_battle_permitted="<<permitted.load()
           <<"\ncontinuous_post_battle_contained="<<contained.load()
           <<"\ncontinuous_post_battle_failure="<<failure.load()<<'\n';
    }
    template<class Pause> void enter(unsigned thread,uintptr_t outer,uintptr_t input_header,Pause pause){
        if(invoke(thread,outer,input_header))return;
        ++contained;
        // No stop-request escape: callers after a partially restored update
        // cannot safely commit. Production supplies a nonthrowing sleep.
        for(;;)pause();
    }
    bool invoke(unsigned thread,uintptr_t outer,uintptr_t input_header)noexcept{
        ++calls;
        if(failure.load())return false;
        if(!valid() || thread!=owner_ || !outer || !input_header){fail(Failure::identity);return false;}
        if(entered_){fail(Failure::reentry);return false;}
        entered_=true;
        struct Exit {bool& value;~Exit(){value=false;}} exit{entered_};
        try {
            const auto result=callback_(context_,outer,input_header);
            if(failure.load())return false;
            if(result!=Result::permit){fail(Failure::callback);return false;}
            ++permitted;return true;
        }catch(...){fail(Failure::exception);return false;}
    }
};
// Published objects have process lifetime. No automatic fallback or unpublish
// is allowed after a failure, including a requested stop from the UI worker.
inline std::atomic<PostBattleHookGate*> post_battle_hook_gate{};
}
