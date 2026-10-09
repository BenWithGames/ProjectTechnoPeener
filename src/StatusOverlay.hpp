#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
namespace sc6 {
// Network code publishes measured values here; missing data is never displayed as zero.
struct OverlayStats {
    bool connected{};
    std::optional<unsigned> ping_ms,delay_frames,rollback_frames;
};
class StatusOverlay {
public:
    StatusOverlay()=default;
    ~StatusOverlay(){stop();}
    void start();
    void stop();
    void publish(const OverlayStats& stats);
private:
    void run();
    std::thread worker;
    std::atomic<bool> stopping{false};
};
}
