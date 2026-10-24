#pragma once
namespace sc6 {
// Adaptive round agreement: send immediately, consume 0..2 frames later.
// Both peers must install this build; normal bilateral admission is retained.
struct InputDelayBuildPolicy {
    static constexpr bool adaptive=true;
    static constexpr unsigned fixed_frames=1;
    static constexpr unsigned proposal(bool prediction,unsigned adaptive_frames){
        return prediction?(adaptive?(adaptive_frames<=2?adaptive_frames:2):fixed_frames):0;
    }
    static constexpr bool accepts(bool prediction,unsigned agreed){
        return prediction?(adaptive?agreed<=2:agreed==fixed_frames):agreed==0;
    }
};
}
