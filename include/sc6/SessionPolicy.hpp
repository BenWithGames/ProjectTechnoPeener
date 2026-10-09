#pragma once
#include <cstdint>
#include <string>

namespace sc6 {
enum class MatchKind { Unknown, Offline, Private, Casual, Ranked };
enum class PeerStatus { NotStarted, Pending, Confirmed, Unavailable, Incompatible };
enum class SessionDecision { OriginalNetworking, WaitForPeer, Rollback, RejectPrivateSession };

struct Compatibility {
    std::uint32_t protocol{};
    std::uint32_t snapshot_schema{};
    std::string game_build;
    std::string simulation_build;

    bool valid() const {
        return protocol != 0 && snapshot_schema != 0 &&
               !game_build.empty() && !simulation_build.empty();
    }
    bool operator==(const Compatibility&) const = default;
};

struct SessionRequest {
    MatchKind kind{MatchKind::Unknown};
    bool local_opt_in{};
    bool peer_opt_in{};
    bool adapter_verified{};
    PeerStatus peer_status{PeerStatus::NotStarted};
    Compatibility local;
    Compatibility peer;
};

// Pre-match policy only. Never change networking modes in a running match.
// Confirmed means a fresh, session-bound, two-sided handshake completed;
// it must not be inferred from a cached player profile or a received packet.
inline SessionDecision decide_session(const SessionRequest& request) {
    if (request.kind != MatchKind::Private || !request.local_opt_in)
        return SessionDecision::OriginalNetworking;

    if (!request.adapter_verified || !request.local.valid())
        return SessionDecision::RejectPrivateSession;

    switch (request.peer_status) {
    case PeerStatus::NotStarted:
    case PeerStatus::Pending:
        return SessionDecision::WaitForPeer;
    case PeerStatus::Confirmed:
        if (request.peer_opt_in && request.peer.valid() && request.local == request.peer)
            return SessionDecision::Rollback;
        return SessionDecision::RejectPrivateSession;
    case PeerStatus::Unavailable:
    case PeerStatus::Incompatible:
        return SessionDecision::RejectPrivateSession;
    }
    return SessionDecision::RejectPrivateSession;
}
} // namespace sc6
