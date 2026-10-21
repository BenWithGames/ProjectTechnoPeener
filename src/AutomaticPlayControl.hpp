#pragma once
#include <atomic>
#include <cstdint>
namespace sc6::automatic_play {
inline bool may_release(uint64_t session,uint64_t saved,bool completed,bool active,bool restart,bool history,bool outer,bool retired){
    return session && session==saved && completed && !active && !restart && history && outer && retired;
}
inline bool enabled{};
inline std::atomic<uint64_t> report_saved{},released{};
inline uint64_t requested_peer{};
inline bool failed{};
inline std::atomic<bool> cleanup_failed{};
}
