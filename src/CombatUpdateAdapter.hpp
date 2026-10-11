#pragma once
#include "SingleUpdateCall.hpp"
#include "RecordedInput.hpp"
#include "NetworkInputComposer.hpp"
namespace sc6::research {
struct CombatFrameInput {
    NetworkInputComposer::Envelope envelope{};
    // Network input must be projected before entering prediction history.
    static CombatFrameInput remote(const NetworkInputComposer::Envelope& value,unsigned slot){
        CombatFrameInput out;
        if(slot>=1 && slot<=2){const auto off=(slot-1)*2;out.envelope[off]=value[off];out.envelope[off+1]=value[off+1];}
        return out;
    }
    bool operator==(const CombatFrameInput&)const=default;
};
// Controller adapter for an explicitly owned simulation boundary. Not armed by
// the observer hook: its caller must suppress the automatic original call after
// this adapter advances, and must own the native network wait/round clock.
// Backend supplies capture/restore of arbitrary supported combat snapshots.
template<class Backend> class CombatUpdateAdapter {
    Backend& backend_;
    SingleUpdateCall::Routine original_{};
    volatile uint32_t* counter_{};
    DWORD owner_{};unsigned remote_slot_{};
    uint32_t expected_{};
    RecordedInput delivery_;
    bool failed_{};
public:
    using Snapshot=typename Backend::Snapshot;
    enum Failure : unsigned {none,identity,capture,restore,counter,update,input};
    Failure failure{};
    unsigned exception{};uint64_t calls{};
    CombatUpdateAdapter(Backend& backend,SingleUpdateCall::Routine original,
        volatile uint32_t* counter,DWORD owner,unsigned remote_slot,uint32_t first)
        :backend_(backend),original_(original),counter_(counter),owner_(owner),remote_slot_(remote_slot),expected_(first){}
    bool failed()const{return failed_;}
    uint32_t expected_frame()const{return expected_;}
    CombatFrameInput predict(const CombatFrameInput& prior)const{
        CombatFrameInput predicted{};
        if(remote_slot_>=1 && remote_slot_<=2)predicted.envelope[(remote_slot_-1)*2]=prior.envelope[(remote_slot_-1)*2];
        // Remote edge bits never repeat. Local and auxiliary words come from
        // the separately retained local envelope when composing the update.
        return predicted;
    }
    bool save(Snapshot& out){
        if(!ready())return false;
        if(!backend_.capture(out,expected_))return fail(capture);
        return check_counter();
    }
    bool save(Snapshot& out,const CombatFrameInput& local,const CombatFrameInput& remote){
        if(!ready() || !check_counter())return false;
        const auto composed=NetworkInputComposer::compose(local.envelope,remote.envelope,remote_slot_,expected_,expected_);
        if(!composed)return fail(identity);
        std::array<std::byte,40> bytes{};std::memcpy(bytes.data(),composed->data(),40);
        if(!RecordedInput::valid_envelope(bytes))return fail(input);
        if constexpr(requires {backend_.capture(out,expected_,bytes);}){
            if(!backend_.capture(out,expected_,bytes))return fail(capture);
            return check_counter();
        }else return save(out);
    }
    bool begin_boundary(const CombatFrameInput& local){
        if(!ready() || !check_counter())return false;
        if constexpr(requires {backend_.begin_boundary(expected_,local.envelope);})
            if(!backend_.begin_boundary(expected_,local.envelope))return fail(capture);
        return true;
    }
    bool load(const Snapshot& saved){
        if(!ready())return false;
        if(!backend_.restore(saved))return fail(restore);
        expected_=backend_.frame(saved);
        return check_counter();
    }
    bool step(const CombatFrameInput& local,const CombatFrameInput& remote){
        if(!ready() || !check_counter())return false;
        const auto composed=NetworkInputComposer::compose(local.envelope,remote.envelope,remote_slot_,expected_,expected_);
        if(!composed)return fail(identity);
        std::array<std::byte,40> bytes{};std::memcpy(bytes.data(),composed->data(),bytes.size());
        if(!RecordedInput::valid_envelope(bytes))return fail(input);
        SingleUpdateCall call;
        const bool ok=call.run(original_,delivery_.assign(bytes),counter_,expected_,owner_);
        if(call.invoked)++calls;
        if(!ok){exception=call.exception;return fail(update);}
        ++expected_;return true;
    }
private:
    bool fail(Failure reason){failed_=true;failure=reason;return false;}
    bool ready(){
        if(failed_)return false;
        if(!original_ || !counter_ || owner_!=GetCurrentThreadId() || remote_slot_<1 || remote_slot_>2)return fail(identity);
        return true;
    }
    bool check_counter() noexcept {
        __try {if(*counter_!=expected_)return fail(counter);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return fail(counter);}
    }
};
}
