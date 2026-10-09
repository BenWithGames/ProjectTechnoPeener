#include <sc6/Game.hpp>
#include <iostream>
#include <stdexcept>

// Deliberately tiny toy simulation. Passing proves the contract/test wiring,
// not determinism or snapshot completeness in Soulcalibur VI.
class ToyGame final : public sc6::Game {
    sc6::Frame frame_{};
    std::uint8_t value_{};
public:
    sc6::Snapshot capture() const override {
        return {frame_, {static_cast<std::byte>(value_)}};
    }
    void restore(const sc6::Snapshot& s) override {
        if (s.payload.size() != 1) throw std::invalid_argument("Invalid toy snapshot");
        frame_ = s.next_frame;
        value_ = std::to_integer<std::uint8_t>(s.payload[0]);
    }
    void advance(const sc6::Inputs& inputs, sc6::StepMode) override {
        value_ = static_cast<std::uint8_t>(value_ + inputs[0].buttons + inputs[1].buttons);
        ++frame_;
    }
};

int main() {
    ToyGame game;
    const auto start = game.capture();
    std::vector<sc6::Inputs> recording;
    std::vector<sc6::Snapshot> expected;
    for (unsigned i = 0; i < 120; ++i) {
        recording.push_back({sc6::Input{static_cast<std::uint8_t>(i % 16), 0}, sc6::Input{2, 0}});
        game.advance(recording.back(), sc6::StepMode::Normal);
        expected.push_back(game.capture());
    }
    game.restore(start);
    if (!(game.capture() == start)) return 1;
    for (std::size_t i = 0; i < recording.size(); ++i) {
        game.advance(recording[i], sc6::StepMode::Resimulation);
        if (!(game.capture() == expected[i])) {
            std::cerr << "Replay mismatch at input " << i << '\n';
            return 1;
        }
    }
    const auto before_invalid = game.capture();
    try {
        game.restore({999, {}});
        return 1;
    } catch (const std::invalid_argument&) {
        if (!(game.capture() == before_invalid)) return 1;
    }
    std::cout << "PASS: 120 toy frames replay exactly; invalid snapshot rejected without mutation.\n";
}
