#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace sc6 {
// Pure composition only. Caller must establish session/frame identity and
// remote ownership separately. No game memory writes or restoration authority.
struct NetworkInputComposer {
    using Envelope=std::array<uint32_t,10>;
    static std::optional<Envelope> compose(const Envelope& local,
        const Envelope& peer,unsigned remote_slot,uint32_t local_frame,
        uint32_t peer_frame) noexcept {
        if(remote_slot<1 || remote_slot>2 || local_frame!=peer_frame)return std::nullopt;
        auto result=local;
        const auto offset=(remote_slot-1)*2;
        result[offset]=peer[offset];result[offset+1]=peer[offset+1];
        // Keep the other player's pair and this PC's recorded auxiliary block.
        // Auxiliary flags/axes are populated outside the battle-update loop.
        return result;
    }
};
}
