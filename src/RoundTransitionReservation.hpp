#pragma once
#include <cstdint>
#include <atomic>
namespace sc6::research {
// Owner-thread observations only. Start increments elapsed before the battle
// counter (2D9CD0 / 2DBC60); external memory samples are not eligible here.
// Publication is irrevocable: a changed projection after reserve() fails closed.
class RoundTransitionReservation {
public:
    struct Sample {
        uint32_t event{},round{},battle{},clock{},clock_identity{},elapsed{},duration{};
        uint64_t generation{};
        bool operator==(const Sample&)const=default;
    };
private:
    Sample last_{};
    uint32_t battle_{},clock_{},samples_{};
    bool reserved_{},failed_{};
public:
    bool observe(const Sample& s){
        if(failed_)return false;
        const auto remaining=s.duration>=s.elapsed?s.duration-s.elapsed:0;
        if(!s.event || !s.round || !s.generation || !s.duration || s.duration>600 ||
           !remaining || s.battle>=UINT32_MAX-remaining || s.clock>=UINT32_MAX-remaining){
            if(reserved_)failed_=true;else samples_=0;
            return false;
        }
        const auto b=s.battle+remaining,c=s.clock+remaining;
        const bool same=samples_ && s.event==last_.event && s.round==last_.round &&
            s.generation==last_.generation && s.clock_identity==last_.clock_identity &&
            s.duration==last_.duration && b==battle_ &&
            s.elapsed>=last_.elapsed && s.battle>=last_.battle && s.clock>=last_.clock;
        if(reserved_ && !same){failed_=true;return false;}
        if(!same)samples_=0;
        if(!samples_ || s.elapsed!=last_.elapsed)++samples_;
        last_=s;battle_=b;clock_=c;
        return true;
    }
    bool can_reserve()const{return !failed_ && samples_>=2 && last_.duration-last_.elapsed>=30;}
    bool reserve(){if(reserved_ || !can_reserve())return false;reserved_=true;return true;}
    bool reserved()const{return reserved_;}
    bool failed()const{return failed_;}
    uint32_t battle()const{return battle_;}
    // Diagnostic estimate only. Native 3FE520 can consume multiple battle
    // updates per outer call; activation binds the freshly observed clock.
    uint32_t projected_clock()const{return clock_;}
    uint32_t samples()const{return samples_;}
    bool native_step_allowed(uint32_t battle)const{return !reserved_ || (!failed_ && battle<battle_);}
    // Bitmask: failed projection, event, round, generation, clock identity,
    // reached/passed target, clock regression, backwards time, reservation age.
    uint32_t wait_rejections(uint32_t event,uint32_t round,uint64_t generation,uint32_t battle,
                  uint32_t clock,uint32_t identity,uint64_t now,uint64_t reserved_at)const{
        if(!reserved_)return 0;
        return (failed_?1u:0u) | (event!=last_.event?2u:0u) | (round!=last_.round?4u:0u) |
            (generation!=last_.generation?8u:0u) | (identity!=last_.clock_identity?16u:0u) |
            (battle>=battle_?32u:0u) | (clock<last_.clock?64u:0u) |
            (now<reserved_at?128u:0u) | (now>=reserved_at && now-reserved_at>=10000?256u:0u);
    }
    bool may_wait(uint32_t event,uint32_t round,uint64_t generation,uint32_t battle,
                  uint32_t clock,uint32_t identity,uint64_t now,uint64_t reserved_at)const{
        return !wait_rejections(event,round,generation,battle,clock,identity,now,reserved_at);
    }
    bool arrives(uint32_t event,uint32_t round,uint64_t generation,uint32_t battle,
                 uint32_t clock,uint32_t identity)const{
        return reserved_ && !failed_ && event==last_.event && round==last_.round &&
            generation==last_.generation && battle==battle_ && clock>=last_.clock && identity==last_.clock_identity;
    }
};
// A native outer call can contain multiple battle steps. The clock check alone
// cannot guard the second step of such a batch. This owner callback also runs
// at the real battle entry, before native state changes.
struct RoundTransitionBoundaryGuard {void* context{};bool (*before_step)(void*)noexcept{};};
inline std::atomic<const RoundTransitionBoundaryGuard*> round_transition_boundary_guard{};
}
