#pragma once
#include "EffectLifetimeLedger.hpp"
#include <span>
#include <thread>

namespace sc6::research {
// Simulation-thread ownership layer, not a native free hook. Caller must establish
// complete allocation/unlink coverage before using retire in place of a real free.
// No payload/list restoration and no automatic cleanup from the destructor.
template<std::size_t Capacity=128,unsigned Slots=16> class EffectRetentionWindow {
public:
    using Ledger=EffectLifetimeLedger<Capacity,Slots>;
    using Token=typename Ledger::Token;
    using Allocation=typename Ledger::Allocation;
    struct Snapshot {unsigned slot{};uint64_t generation{},session{},round{},frame{};};
private:
    Ledger ledger_;
    std::array<Snapshot,Slots> snapshots_{};
    std::thread::id owner_=std::this_thread::get_id();
    uint64_t serial_{},session_{},round_{};
    bool failed_{};
    bool owner()const noexcept{return owner_==std::this_thread::get_id();}
    bool valid(Snapshot h)const noexcept {
        if(!owner() || h.slot>=Slots || !h.generation)return false;
        const auto& s=snapshots_[h.slot];
        return s.generation==h.generation && s.session==h.session && s.round==h.round && s.frame==h.frame;
    }
public:
    bool begin(uint64_t session,uint64_t round) noexcept {
        if(!owner() || session_ || !session || !round)return false;
        session_=session;round_=round;failed_=false;return true;
    }
    Token track(Allocation allocation) noexcept {return owner() && session_ && !failed_?ledger_.track(allocation):Token{};}
    // Full validation precedes all pins, so failed capture cannot leave partial pins.
    Snapshot capture(uint64_t frame,std::span<const Token> tokens) noexcept {
        if(!owner() || !session_ || failed_ || serial_==UINT64_MAX || tokens.size()>Capacity)return {};
        unsigned slot=Slots;
        for(unsigned i=0;i<Slots;++i){
            if(snapshots_[i].generation && snapshots_[i].frame==frame)return {};
            if(!snapshots_[i].generation && slot==Slots)slot=i;
        }
        if(slot==Slots)return {};
        for(auto t:tokens)if(!ledger_.live(t))return {};
        for(auto t:tokens)ledger_.pin(t,slot);
        return snapshots_[slot]={slot,++serial_,session_,round_,frame};
    }
    bool retained(Snapshot s,Token t) noexcept {return !failed_ && valid(s) && ledger_.retained_by(t,s.slot);}
    // Called on owner thread BEFORE forwarding an unexpected native free.
    // Invalidate all references, relinquish this allocation without freeing it,
    // and stop further captures/restores for the whole session. Other retired
    // objects remain owned until explicit collection or shutdown.
    bool unexpected_release(Token token) noexcept {
        if(!owner() || !session_)return false;
        failed_=true;
        for(auto& s:snapshots_)if(s.generation && ledger_.retained_by(token,s.slot)){
            ledger_.release_snapshot(s.slot);s={};
        }
        return ledger_.forget_unpinned(token);
    }
    bool snapshot_valid(Snapshot snapshot)const noexcept {return !failed_ && valid(snapshot);}
    bool pinned(Token token) noexcept {return owner() && session_ && ledger_.pinned(token);}
    bool live(Token token) noexcept {return owner() && session_ && !failed_ && ledger_.live(token);}
    bool failed()const noexcept {return failed_;}
    // Only after verified unlink. Unknown/stale/duplicate retire is rejected;
    // caller must not interpret rejection as permission to suppress a native free.
    bool retire(Token t) noexcept {return owner() && session_ && ledger_.retire(t);}
    template<class Release> std::size_t collect(Release&& release) noexcept {
        if(!owner() || !session_)return 0;
        return ledger_.collect(release);
    }
    template<class Release> bool expire(Snapshot s,Release&& release) noexcept {
        if(!valid(s))return false;
        ledger_.release_snapshot(s.slot);snapshots_[s.slot]={};ledger_.collect(release);return true;
    }
    // Memory restoration must have completed successfully before calling this.
    bool restored(Snapshot s,Token t) noexcept {return !failed_ && valid(s) && ledger_.revive(t,s.slot);}
    // Explicit quiescent boundary; never silently discard active snapshot pins.
    template<class Release> bool finish(Release&& release) noexcept {
        if(!owner() || !session_)return false;
        for(const auto& s:snapshots_)if(s.generation)return false;
        if(!ledger_.finish_session(release))return false;
        session_=round_=0;return true;
    }
};
}
