#pragma once
#include <cstdint>
#include <span>
#include "RoundBoundaryIdentity.hpp"
namespace sc6 {
// Metadata eligibility only. A successful result does not authorize game writes:
// game-thread checkpoint generation and snapshot ownership must also be checked.
struct NetworkReplayEligibility {
    struct Key {
        uint64_t local_nonce{},peer_nonce{};
        uint32_t local_slot{},round{},boundary{},frame{};
    };
    enum class Result { eligible, missing, session, slot, round, sequence, depth, checkpoint };
    // Must be called on the game thread with its current observer generation.
    // The caller still owns snapshot validity and invokes no restore on rejection.
    static Result check_checkpoint(const RoundBoundaryIdentity& identity,
        const RoundBoundaryIdentity::Key& checkpoint,const Key& saved,
        std::span<const Key> path,uint32_t current_frame) noexcept {
        if(!identity.permits(checkpoint,current_frame) || identity.session!=saved.local_nonce ||
           checkpoint.frame!=saved.frame || checkpoint.round!=saved.round)return Result::checkpoint;
        const auto result=check(saved,path);
        if(result!=Result::eligible)return result;
        return path.back().frame==current_frame?Result::eligible:Result::sequence;
    }
    static Result check(const Key& saved,std::span<const Key> path) noexcept {
        if(path.empty() || path.size()>3)return Result::depth;
        if(!saved.local_nonce || !saved.peer_nonce || !saved.round)return Result::missing;
        if(saved.local_slot<1 || saved.local_slot>2)return Result::slot;
        if(saved.boundary>saved.frame)return Result::sequence;
        auto previous=saved.frame;
        for(const auto& current:path){
            if(!current.local_nonce || !current.peer_nonce || !current.round)return Result::missing;
            if(current.local_nonce!=saved.local_nonce || current.peer_nonce!=saved.peer_nonce)return Result::session;
            if(current.local_slot!=saved.local_slot)return Result::slot;
            if(current.round!=saved.round || current.boundary!=saved.boundary)return Result::round;
            if(previous==UINT32_MAX || current.frame!=previous+1u)return Result::sequence;
            previous=current.frame;
        }
        return Result::eligible;
    }
};
}
