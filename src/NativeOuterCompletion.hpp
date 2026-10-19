#pragma once
#include <array>
#include <cstdint>
#include <atomic>
namespace sc6::research {
// A receipt is issued only after the complete wrapped native call returns.
// The mid-function post-battle hook can claim the active call, not complete it.
class NativeOuterCompletion {
public:
    class Receipt {
        uintptr_t outer_{};uint64_t token_{};unsigned owner_{};
        friend class NativeOuterCompletion;
        Receipt(uintptr_t outer,uint64_t token,unsigned owner):outer_(outer),token_(token),owner_(owner){}
    public:
        bool matches(uintptr_t outer,uint64_t token,unsigned owner)const{return outer==outer_ && token && token==token_ && owner==owner_;}
    };
    using Complete=bool(*)(void*,const Receipt&);
private:
    struct Call {uintptr_t outer{};uint64_t token{};void* context{};Complete complete{};};
    std::array<Call,16> calls_{};
    unsigned owner_{},depth_{};uint64_t sequence_{};bool failed_{},completing_{};
    bool fail(){failed_=true;return false;}
public:
    bool failed()const{return failed_;}
    bool enter(unsigned owner,uintptr_t outer){
        if(failed_)return false;
        if(!owner || !outer || (owner_ && owner_!=owner) || completing_ || depth_==calls_.size() || sequence_==UINT64_MAX)return fail();
        owner_=owner;calls_[depth_++]={outer,++sequence_,nullptr,nullptr};return true;
    }
    uint64_t claim(unsigned owner,uintptr_t outer,void* context,Complete callback){
        if(failed_ || !depth_ || completing_ || owner!=owner_ || !context || !callback){fail();return 0;}
        auto& call=calls_[depth_-1];
        if(call.outer!=outer || call.complete){fail();return 0;}
        call.context=context;call.complete=callback;return call.token;
    }
    bool returned(unsigned owner,uintptr_t outer){
        if(failed_)return false;
        if(!depth_ || completing_ || owner!=owner_ || calls_[depth_-1].outer!=outer)return fail();
        const auto call=calls_[depth_-1];
        completing_=true;
        struct Exit {bool& value;~Exit(){value=false;}} exit{completing_};
        try {
            if(call.complete && !call.complete(call.context,Receipt(call.outer,call.token,owner_)))return fail();
            if(failed_)return false;
            calls_[--depth_]={};return true;
        }catch(...){return fail();}
    }
};
inline thread_local NativeOuterCompletion native_outer_completion;
inline std::atomic<uintptr_t> native_outer_hook_base{},native_post_battle_hook_base{};
}
