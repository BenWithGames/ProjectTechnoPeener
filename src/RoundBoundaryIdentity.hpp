#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace sc6 {
// Pure guard for the discovered OnRoundStarted(CurrentRound) event.
// A new boundary always invalidates an old checkpoint,
// even if a rematch reuses both its round number and frame counter.
struct RoundBoundaryIdentity {
    struct Key {
        uint64_t session{},boundary{};
        uint32_t round{},frame{};
        bool operator==(const Key&)const=default;
    };
    uint64_t session{},boundary{};
    uint32_t round{};
    bool known=false,failed=false;
    void begin(uint64_t nonce){session=nonce;boundary=0;round=0;known=false;failed=!nonce;upstream_seen=false;upstream_frame=0;}
    bool on_round_started(int current_round){
        known=false;
        if(failed || !session || current_round<=0 || boundary==(std::numeric_limits<uint64_t>::max)()){
            failed=true;return false;
        }
        ++boundary;round=static_cast<uint32_t>(current_round);known=true;return true;
    }
    uint32_t upstream_frame{};bool upstream_seen=false;
    bool on_upstream_start(uint32_t value,uint32_t frame,bool trusted){
        if(!trusted || !value || value>=65535){known=false;failed=true;return false;}
        if(upstream_seen && known && round==value && upstream_frame==frame)return true;
        if(!on_round_started(static_cast<int>(value)))return false;
        upstream_frame=frame;upstream_seen=true;return true;
    }
    std::optional<Key> key(uint32_t frame)const{
        if(!known || failed)return std::nullopt;
        return Key{session,boundary,round,frame};
    }
    bool permits(const Key& saved,uint32_t current_frame)const{
        return known && !failed && saved.session==session && saved.boundary==boundary &&
            saved.round==round && saved.frame<=current_frame;
    }
    // boundary is a local generation, NOT a cross-PC round identifier. Peers
    // still need an agreed boundary token before any network restore is allowed.
};
}

