#pragma once
#include "RecordedInput.hpp"
#include <limits>
namespace sc6::research {
// Pinned 3FE520 callsite 3FE81D, BEFORE the A30 delegate dispatch.
// This object owns data, not a suspended native stack or permission to resume.
// The hook adapter must supply registers captured before its C++ bridge runs.
class NativeOuterContinuationFrame {
public:
    struct Registers {
        uintptr_t owner{},input_header{}; // RBX and native RSP+38
        uint64_t remaining{};             // RSI, includes the current step
        uint32_t reverse_index{};         // EBP: remaining-1, used by 3FCD10
        uint8_t suppress_edges{},suppress_held{}; // R12B/R13B cached by prefix
    };
    enum class Failure {none,already_captured,identity,loop,read,pointers,envelope,changed};
private:
    RecordedInput input_;
    Registers saved_{};
    uintptr_t clock_{},battle_{},players_{};
    uint32_t clock_identity_{},clock_frame_{};
    Failure failure_{};
    bool captured_{};
    bool fail(Failure failure){failure_=failure;return false;}
public:
    Failure failure()const{return failure_;}
    const Registers& registers()const{return saved_;}
    const auto& bytes()const{return input_.bytes;}
    uintptr_t argument()const{return captured_ && failure_==Failure::none?reinterpret_cast<uintptr_t>(input_.pointers.data()):0;}
    uintptr_t clock()const{return clock_;}
    uintptr_t battle()const{return battle_;}
    uintptr_t players()const{return players_;}
    uint32_t clock_identity()const{return clock_identity_;}
    uint32_t clock_frame()const{return clock_frame_;}
    // Owner-thread use only. Access::read copies bytes and rejects invalid reads.
    // Double observation detects a changed layout; it does not synchronize a
    // cross-thread reader or prove ownership of native callback execution.
    template<class Access> bool capture(const Registers& r){
        if(captured_ || failure_!=Failure::none)return fail(Failure::already_captured);
        constexpr auto maximum=std::numeric_limits<uintptr_t>::max();
        if(!r.owner || r.owner>maximum-0x14E0 || !r.input_header || r.input_header>maximum-24)return fail(Failure::identity);
        if(!r.remaining || r.remaining>INT32_MAX || r.reverse_index!=r.remaining-1 ||
            r.suppress_edges>1 || r.suppress_held>1)return fail(Failure::loop);
        struct Observed {
            uintptr_t clock{},battle{},players{};
            int32_t player_count{};
            uint32_t identity{},frame{};
            std::array<uintptr_t,3> header{};
            bool operator==(const Observed&)const=default;
        } before,after;
        auto observe=[&](Observed& out){
            return Access::read(r.owner+0x478,&out.clock,sizeof(out.clock)) &&
                out.clock && out.clock<=maximum-0x3A8 &&
                Access::read(r.owner+0x1450,&out.battle,sizeof(out.battle)) && out.battle &&
                Access::read(r.owner+0x14A8,&out.players,sizeof(out.players)) &&
                out.players && out.players<=maximum-16 &&
                Access::read(r.owner+0x14B0,&out.player_count,sizeof(out.player_count)) &&
                Access::read(out.clock+0x3A0,&out.identity,sizeof(out.identity)) &&
                Access::read(out.clock+0x3A4,&out.frame,sizeof(out.frame)) &&
                Access::read(r.input_header,out.header.data(),sizeof(out.header));
        };
        if(!observe(before))return fail(Failure::read);
        if(before.player_count!=2 || before.header[0]!=before.players || before.header[1]!=before.players+8 ||
            before.header[2]!=r.owner+0x14C8)return fail(Failure::pointers);
        std::array<std::byte,40> bytes{};
        if(!Access::read(before.players,bytes.data(),16) ||
            !Access::read(r.owner+0x14C8,bytes.data()+16,24))return fail(Failure::read);
        if(!observe(after))return fail(Failure::read);
        if(!(before==after))return fail(Failure::changed);
        if(!RecordedInput::valid_envelope(bytes))return fail(Failure::envelope);
        input_.assign(bytes);saved_=r;clock_=before.clock;battle_=before.battle;players_=before.players;
        clock_identity_=before.identity;clock_frame_=before.frame;captured_=true;return true;
    }
};
}
