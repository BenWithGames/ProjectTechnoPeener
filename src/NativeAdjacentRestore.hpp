#pragma once
#include "NativeBoundarySnapshot.hpp"
#include "TwoFrameIdleReplay.hpp"
#include "ImmediateIdleCorrection.hpp"
namespace sc6::research {
// Experimental one-frame idle restoration. No update invocation or input injection.
// Failure after restore_invoked requires ending the live experimental session.
struct NativeAdjacentRestore {
    NativeBoundarySnapshot current{NativeBoundarySnapshot::Mode::idle};
    std::unique_ptr<FullStateProbe> probe=std::make_unique<FullStateProbe>();
    bool attempted{},prepared{},verified{},fault{},restore_input_used{};
    RecordedInput restore_input;
    ImmediateIdleCorrection completion;
    bool native_payload_verified{};
    NativeBoundarySnapshot::Rejection rejection=NativeBoundarySnapshot::Rejection::none;
    bool run(const NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
             FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
             FullStateProbe::Preflight check=nullptr) noexcept {
        if(attempted || fault)return false;attempted=true;
        if(source.mode!=NativeBoundarySnapshot::Mode::idle || source.module!=base ||
           !source.valid || source.fault || source.thread!=GetCurrentThreadId())return fail();
        if(!current.capture(base,argument,writer,check))return fail();
        return finish(source,base,argument,writer,reader,check);
    }
    // Both boundaries use this one non-inlined call site. Native serializers
    // write stack temporaries whose addresses depend on the caller frame.
    __declspec(noinline) bool step(NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
             FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
             FullStateProbe::Preflight check=nullptr) noexcept {
        if(attempted || fault)return false;
        const bool first=!source.attempted;
        if(!first && (source.mode!=NativeBoundarySnapshot::Mode::idle || source.module!=base ||
           !source.valid || source.fault || source.thread!=GetCurrentThreadId()))return fail();
        auto& boundary=first?source:current;
        if(!boundary.capture(base,argument,writer,check))return fail();
        if(first)return true;
        attempted=true;
        return finish(source,base,argument,writer,reader,check);
    }
    RecordedInput replay_delivery;
    uintptr_t next_argument{};
    bool replay_delivered{},replay_checked{},replay_verified{},replay_scalars_verified{};
    // One natural update through the hook, never a recursive/synchronous step.
    bool replay_step(NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
                     bool replacement_available,FullStateProbe::Routine writer=nullptr,
                     FullStateProbe::Routine reader=nullptr,FullStateProbe::Preflight check=nullptr) noexcept {
        next_argument=0;
        if(fault)return false;
        if(replay_checked)return replay_verified;
        if(!replacement_available)return fail();
        if(!attempted){
            if(!step(source,base,argument,writer,reader,check))return false;
            if(verified){next_argument=replay_delivery.assign(source.data->delayed_inputs);replay_delivered=true;}
            return true;
        }
        if(!verified || !replay_delivered || source.module!=base || source.thread!=GetCurrentThreadId())return fail();
        replay_checked=true;
        std::array<std::byte,40> live{};
        if(!FullStateProbe::input_bytes(argument,live) || !RecordedInput::compatible(source.data->delayed_inputs,live))return fail();
        replay_scalars_verified=replay_scalars(base);
        if(!replay_scalars_verified)return fail();
        probe->delayed_step(base,replay_delivery.assign(source.data->delayed_inputs),writer,reader,check);
        replay_verified=TwoFrameIdleReplay::comparison_ok(*probe,base);
        if(!replay_verified)return fail();
        return true;
    }
    bool prepare_captured(const NativeBoundarySnapshot& source,const NativeBoundarySnapshot& next,bool direction_restore=false){
        if(attempted || fault)return false;attempted=true;
        if((source.mode!=NativeBoundarySnapshot::Mode::idle && !(direction_restore && source.mode==NativeBoundarySnapshot::Mode::direction_observation)) ||
           next.mode!=(direction_restore?NativeBoundarySnapshot::Mode::direction_observation:source.mode) ||
           source.thread!=GetCurrentThreadId() || source.module!=next.module)return fail();
        current.direction_bits=next.direction_bits;current.mode=next.mode;current.module=next.module;current.thread=next.thread;current.attempted=next.attempted;
        current.valid=next.valid;current.fault=next.fault;*current.data=*next.data;
        if(!source.prepare_adjacent(current,*probe,&rejection,direction_restore))return fail();
        prepared=true;return true;
    }
    bool restore_captured(const NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
                          FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
                          FullStateProbe::Preflight check=nullptr) noexcept {
        if(!prepared || verified || fault || base!=source.module || source.thread!=GetCurrentThreadId())return fail();
        __try {
            std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> keys{};uintptr_t players[2]{};
            FullStateProbe::keys(base,keys.data(),players);
            if(keys!=current.data->before_keys || std::memcmp(players,current.data->before_players,sizeof(players)) ||
               !probe->delayed_scratch.capture(base) || !current.data->rings[0].same(probe->delayed_scratch))return fail();
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail();}
        return finish(source,base,argument,writer,reader,check);
    }
private:
    bool finish(const NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
                FullStateProbe::Routine writer,FullStateProbe::Routine reader,FullStateProbe::Preflight check) noexcept {
        if(!prepared && !source.prepare_adjacent(current,*probe,&rejection))return fail();
        prepared=true;
        std::array<std::byte,40> live_inputs{};
        if(!FullStateProbe::input_bytes(argument,live_inputs) || live_inputs!=current.data->delayed_inputs)return fail();
        // Private envelope for the restoration guard only. The normal update
        // keeps its original argument; game input memory is never overwritten.
        auto saved_argument=restore_input.assign(source.data->delayed_inputs);
        restore_input_used=true;
        probe->delayed_step(base,saved_argument,writer,reader,check);
        if(probe->fault || !probe->restore_completed || !probe->delayed_loaded || !probe->camera_restore_verified)return fail();
        const auto& saved=*source.data;auto& p=*probe;
        // Accept only the already-proven native self-pointer rebuild, with exact
        // source identity; only the separately proven reader-unused padding
        // below may differ in its pinned 16-byte record.
        native_payload_verified=native_equal(saved,p,base);
        if(!native_payload_verified || !completion.apply(saved,*current.data,p,base))return fail();
        if(!p.capture_blend(base,3) || !p.cameras[4].capture(p.restored,base) ||
           !p.restored_after_rings.capture(base) || !saved.rings[0].same(p.restored_after_rings))return fail();
        verified=p.restored_valid && p.restored_save_unchanged && p.preserve_verified && p.outer_correction_verified &&
            native_payload_verified && p.restored_ring_equal &&
            p.blends[3].valid && p.blends[3].bytes==saved.blends[0].bytes &&
            !std::memcmp(p.blends[3].owners,saved.blends[0].owners,sizeof(p.blends[3].owners)) &&
            TwoFrameIdleReplay::transforms_equal(saved.transforms[0],p.transforms[3]) &&
            TwoFrameIdleReplay::cameras_equal(saved.cameras[0],p.cameras[4]);
        if(!verified)return fail();
        return true;
    }
private:
    bool replay_scalars(uintptr_t base) noexcept {
        __try {
            FullStateProbe::Measurement live{};IdleTrajectoryAudit::measure(base,live);
            const auto& expected=probe->original_next;
            if(live.rng!=expected.rng || std::memcmp(live.players,expected.players,sizeof(live.players)))return false;
            for(unsigned side=0;side<2;++side)if(std::memcmp(live.fields[side].data()+8,expected.fields[side].data()+8,216))return false;
            return true;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
public:
    static bool native_equal(const FullStateProbe& a,const FullStateProbe& b,uintptr_t base){
        const auto& x=a.saved;const auto& y=b.restored;
        if(!IdleTrajectoryAudit::same_shape(x,y) || x.stream.write_position!=y.stream.write_position)return false;
        std::uint64_t offset=0;
        for(unsigned i=0;i<x.writes;++i){auto size=x.lengths[i];unsigned skip=0;
            if(offset>x.stream.write_position || size>x.stream.write_position-offset)return false;
            for(auto player:a.before_players)if(size==48 && x.traces[i].writer==base+0x30AA54 &&
                x.traces[i].source==player+0x43DF0 && y.traces[i].source==player+0x43DF0){
                if(TransformSnapshot::pointer(y.stream.payload.data()+offset)!=player)return false;skip=8;
            }
            // Same pinned writer/reader exception as the verified replay
            // comparator: bytes4..7 are uninitialized and never consumed.
            const bool padding=size==16 && x.traces[i].writer==base+0x32AC76 &&
                b.restore_completed && b.saved.sealed && i<b.saved.writes &&
                b.saved.lengths[i]==16 && b.saved.traces[i].writer==base+0x32AC76 &&
                b.saved.traces[i].reader==base+0x32AD1B;
            if(padding){
                if(std::memcmp(x.stream.payload.data()+offset,y.stream.payload.data()+offset,4) ||
                   std::memcmp(x.stream.payload.data()+offset+8,y.stream.payload.data()+offset+8,8))return false;
            }else if(std::memcmp(x.stream.payload.data()+offset+skip,y.stream.payload.data()+offset+skip,static_cast<size_t>(size-skip)))return false;
            offset+=size;
        }
        return offset==x.stream.write_position;
    }
private:
    bool fail(){fault=true;return false;}
};
}

