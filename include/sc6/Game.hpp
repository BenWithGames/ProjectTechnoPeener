#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sc6 {
using Frame = std::uint64_t;

// Project-owned transport format, NOT a verified SC6 memory layout.
// Direction encoding will be selected when the input hook is researched.
struct Input {
    std::uint8_t buttons{};
    std::uint8_t direction{};
    bool operator==(const Input&) const = default;
};
using Inputs = std::array<Input, 2>;

// State immediately BEFORE next_frame consumes its inputs.
// Payload must own its bytes: no process pointers or raw C++ object copies.
struct Snapshot {
    Frame next_frame{};
    std::vector<std::byte> payload;
    bool operator==(const Snapshot&) const = default;
};

enum class StepMode { Normal, Resimulation };

// Implement only once the battle boundary, input injection, and full
// simulation state have been identified for a specific SC6 executable.
// All calls must occur on the appropriate game thread at a safe boundary.
class Game {
public:
    virtual ~Game() = default;
    virtual Snapshot capture() const = 0;
    virtual void restore(const Snapshot&) = 0;
    virtual void advance(const Inputs&, StepMode) = 0;
};
} // namespace sc6
