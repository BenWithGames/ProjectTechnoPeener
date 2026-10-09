#include <sc6/SessionPolicy.hpp>
#include <iostream>
#include <stdexcept>

using namespace sc6;
void expect(const SessionRequest& request, SessionDecision expected) {
    if (decide_session(request) != expected)
        throw std::runtime_error("Unexpected session decision");
}

int main() {
    SessionRequest request;
    expect(request, SessionDecision::OriginalNetworking);
    request.local = {1, 1, "test-game-build", "test-simulation-build"};
    request.peer = request.local;
    request.local_opt_in = request.peer_opt_in = request.adapter_verified = true;
    request.peer_status = PeerStatus::Confirmed;

    // Even a compatible opted-in peer cannot activate rollback in matchmaking.
    for (auto kind : {MatchKind::Unknown, MatchKind::Offline, MatchKind::Casual, MatchKind::Ranked}) {
        request.kind = kind;
        for (auto status : {PeerStatus::NotStarted, PeerStatus::Pending, PeerStatus::Confirmed,
                            PeerStatus::Unavailable, PeerStatus::Incompatible}) {
            request.peer_status = status;
            expect(request, SessionDecision::OriginalNetworking);
        }
    }
    request.kind = MatchKind::Private;
    request.peer_status = PeerStatus::Confirmed;
    expect(request, SessionDecision::Rollback);
    request.local_opt_in = false;
    expect(request, SessionDecision::OriginalNetworking);
    request.local_opt_in = true;
    request.peer_opt_in = false;
    expect(request, SessionDecision::RejectPrivateSession);
    request.peer_opt_in = true;
    request.adapter_verified = false;
    expect(request, SessionDecision::RejectPrivateSession);
    request.adapter_verified = true;

    for (auto status : {PeerStatus::NotStarted, PeerStatus::Pending}) {
        request.peer_status = status;
        expect(request, SessionDecision::WaitForPeer);
    }
    for (auto status : {PeerStatus::Unavailable, PeerStatus::Incompatible}) {
        request.peer_status = status;
        expect(request, SessionDecision::RejectPrivateSession);
    }
    request.peer_status = PeerStatus::Confirmed;
    for (int field = 0; field < 4; ++field) {
        request.peer = request.local;
        if (field == 0) ++request.peer.protocol;
        if (field == 1) ++request.peer.snapshot_schema;
        if (field == 2) request.peer.game_build += "-different";
        if (field == 3) request.peer.simulation_build += "-different";
        expect(request, SessionDecision::RejectPrivateSession);
    }
    request.local = request.peer = {};
    expect(request, SessionDecision::RejectPrivateSession);
    std::cout << "PASS: session policy defaults to original networking and gates private rollback.\n";
}
