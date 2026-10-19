#pragma once
#include <cstdint>
namespace sc6 { void recorder_init(); void recorder_poll(); void recorder_stop(); }

namespace sc6 { inline bool (*prepare_owned_replay)(){}; }
namespace sc6 { inline bool (*prepare_combat_recording)(std::uint64_t,bool){}; }
namespace sc6 { inline void (*abort_combat_recording)(std::uint64_t){}; }
namespace sc6 {
struct ContinuousStartRequest {
    std::uint64_t local_nonce{},remote_nonce{},steam_peer{},build{},ticket{};
    unsigned local_slot{},clock_identity{},clock_frame{},battle_frame{};
    bool private_match_confirmed{};
};
inline bool (*queue_continuous_start)(const ContinuousStartRequest&){};
// 0 = no request, 1 = waiting, 2 = preparing, 3 = prepared, 4 = rejected.
inline unsigned (*continuous_start_state)(){};
inline unsigned (*continuous_start_stage)(){};
}
