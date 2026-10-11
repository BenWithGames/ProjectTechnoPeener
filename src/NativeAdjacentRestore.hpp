#pragma once
#include "NativeBoundarySnapshot.hpp"
#include "TwoFrameIdleReplay.hpp"
#include "ImmediateIdleCorrection.hpp"
namespace sc6::research {
// Experimental one-frame idle restoration. No update invocation or input injection.
// Failure after restore_invoked requires ending the live experimental session.
struct NativeAdjacentRestore {
    NativeAdjacentRestore()=default;
private:
    NativeAdjacentRestore(std::unique_ptr<FullStateProbe> snapshot,std::unique_ptr<FullStateProbe> scratch) noexcept
        :current(NativeBoundarySnapshot::Mode::idle,std::move(snapshot)),probe(std::move(scratch)){}
public:
    NativeBoundarySnapshot current{NativeBoundarySnapshot::Mode::idle};
    std::unique_ptr<FullStateProbe> probe=std::make_unique<FullStateProbe>();
    bool attempted{},prepared{},verified{},fault{},restore_input_used{};
    RecordedInput restore_input;
    ImmediateIdleCorrection completion;
    bool native_payload_verified{};
    unsigned diagnostic_stage{}; // Last restoration check entered; no guard behavior changes.
    NativeBoundarySnapshot::Rejection rejection=NativeBoundarySnapshot::Rejection::none;
    // Call only after replay comparison completes and no caller retains views
    // into this workspace. A successful load alone does NOT authorize reuse.
    bool recycle_after_replay() noexcept {
        if(!attempted || !prepared || !verified || fault || !replay_delivered ||
           !replay_checked || !replay_verified || !replay_scalars_verified ||
           !native_payload_verified || !completion.verified || !probe || !current.data ||
           current.fault || current.exception || probe->fault || probe->exception ||
           !probe->restore_invoked || !probe->restore_completed || !probe->delayed_loaded ||
           !probe->camera_restore_verified || probe->saved.stream.failures ||
           probe->after_save.stream.failures || probe->restored.stream.failures)return false;
        auto snapshot=std::move(current.data),scratch=std::move(probe);
        std::destroy_at(snapshot.get());std::construct_at(snapshot.get());
        std::destroy_at(scratch.get());std::construct_at(scratch.get());
        // Rebuild flags, patch-list pointers and owned input headers in place.
        // Constructor overrides both allocating member initializers.
        std::destroy_at(this);
        ::new (this) NativeAdjacentRestore(std::move(snapshot),std::move(scratch));
        return true;
    }
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
    unsigned replay_stage{};
    std::array<unsigned,2> replay_first_offset{UINT32_MAX,UINT32_MAX},replay_expected_byte{},replay_live_byte{};
    bool replay_measurement_read{},replay_rng_equal{},replay_players_equal{};
    unsigned replay_rng_expected{},replay_rng_actual{},replay_comparison_mask{};
    TwoFrameIdleReplay::NativeDifference replay_native_difference;
    std::array<unsigned,2> replay_scalar_differences{};
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
        replay_checked=true;replay_stage=1;
        std::array<std::byte,40> live{};
        if(!FullStateProbe::input_bytes(argument,live) || !RecordedInput::compatible(source.data->delayed_inputs,live))return fail();
        replay_stage=2;
        replay_scalars_verified=replay_scalars(base);
        if(!replay_scalars_verified)return fail();
        replay_stage=3;
        probe->delayed_step(base,replay_delivery.assign(source.data->delayed_inputs),writer,reader,check);
        replay_stage=4;
        replay_comparison_mask=TwoFrameIdleReplay::comparison_mask(*probe,base);
        TwoFrameIdleReplay::native_equal(*probe,base,&replay_native_difference);
        replay_verified=TwoFrameIdleReplay::comparison_ok(*probe,base);
        if(!replay_verified)return fail();
        replay_stage=5;return true;
    }
    bool prepare_captured(const NativeBoundarySnapshot& source,const NativeBoundarySnapshot& next,bool direction_restore=false,bool a_restore=false){
        if(attempted || fault)return false;attempted=true;
        if((source.mode!=NativeBoundarySnapshot::Mode::idle && !(direction_restore && source.mode==NativeBoundarySnapshot::Mode::direction_observation)) ||
           next.mode!=(a_restore?NativeBoundarySnapshot::Mode::a_observation:direction_restore?NativeBoundarySnapshot::Mode::direction_observation:source.mode) ||
           source.thread!=GetCurrentThreadId() || source.module!=next.module)return fail();
        current.direction_slot=next.direction_slot;current.direction_bits=next.direction_bits;current.mode=next.mode;current.module=next.module;current.thread=next.thread;current.attempted=next.attempted;
        current.valid=next.valid;current.fault=next.fault;*current.data=*next.data;
        if(!source.prepare_adjacent(current,*probe,&rejection,direction_restore,a_restore))return fail();
        prepared=true;return true;
    }
    bool restore_captured(const NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
                          FullStateProbe::Routine writer=nullptr,FullStateProbe::Routine reader=nullptr,
                          FullStateProbe::Preflight check=nullptr) noexcept {
        diagnostic_stage=1;
        if(!prepared || verified || fault || base!=source.module || source.thread!=GetCurrentThreadId())return fail();
        __try {
            std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> keys{};uintptr_t players[2]{};
            diagnostic_stage=2;
            FullStateProbe::keys(base,keys.data(),players);
            if(keys!=current.data->before_keys || std::memcmp(players,current.data->before_players,sizeof(players)) ||
               !probe->delayed_scratch.capture(base) || !current.data->rings[0].same(probe->delayed_scratch))return fail();
        }__except(EXCEPTION_EXECUTE_HANDLER){return fail();}
        return finish(source,base,argument,writer,reader,check);
    }
private:
    bool finish(const NativeBoundarySnapshot& source,uintptr_t base,uintptr_t argument,
                FullStateProbe::Routine writer,FullStateProbe::Routine reader,FullStateProbe::Preflight check) noexcept {
        diagnostic_stage=3;
        if(!prepared && !source.prepare_adjacent(current,*probe,&rejection))return fail();
        prepared=true;
        diagnostic_stage=4;
        std::array<std::byte,40> live_inputs{};
        if(!FullStateProbe::input_bytes(argument,live_inputs) || live_inputs!=current.data->delayed_inputs)return fail();
        // Private envelope for the restoration guard only. The normal update
        // keeps its original argument; game input memory is never overwritten.
        auto saved_argument=restore_input.assign(source.data->delayed_inputs);
        restore_input_used=true;
        diagnostic_stage=5;
        probe->delayed_step(base,saved_argument,writer,reader,check);
        if(probe->fault || !probe->restore_completed || !probe->delayed_loaded || !probe->camera_restore_verified)return fail();
        const auto& saved=*source.data;auto& p=*probe;
        // Accept only the already-proven native self-pointer rebuild, with exact
        // source identity; only the separately proven reader-unused padding
        // below may differ in its pinned 16-byte record.
        diagnostic_stage=6;
        native_payload_verified=native_equal(saved,p,base);
        if(!native_payload_verified)return fail();
        diagnostic_stage=7;
        if(!completion.apply(saved,*current.data,p,base))return fail();
        diagnostic_stage=8;
        if(!p.capture_blend(base,3) || !p.cameras[4].capture(p.restored,base) ||
           !p.restored_after_rings.capture(base) || !saved.rings[0].same(p.restored_after_rings))return fail();
        diagnostic_stage=9;
        verified=p.restored_valid && p.restored_save_unchanged && p.preserve_verified && p.outer_correction_verified &&
            native_payload_verified && p.restored_ring_equal &&
            p.blends[3].valid && p.blends[3].bytes==saved.blends[0].bytes &&
            !std::memcmp(p.blends[3].owners,saved.blends[0].owners,sizeof(p.blends[3].owners)) &&
            TwoFrameIdleReplay::transforms_equal(saved.transforms[0],p.transforms[3]) &&
            TwoFrameIdleReplay::cameras_equal(saved.cameras[0],p.cameras[4]);
        if(!verified)return fail();
        diagnostic_stage=10;
        return true;
    }
private:
    bool replay_scalars(uintptr_t base) noexcept {
        __try {
            FullStateProbe::Measurement live{};IdleTrajectoryAudit::measure(base,live);
            const auto& expected=probe->original_next;
            replay_rng_expected=expected.rng;replay_rng_actual=live.rng;
            replay_measurement_read=true;
            replay_rng_equal=live.rng==expected.rng;
            replay_players_equal=!std::memcmp(live.players,expected.players,sizeof(live.players));
            for(unsigned side=0;side<2;++side){
                replay_scalar_differences[side]=0;replay_first_offset[side]=UINT32_MAX;
                for(unsigned byte=8;byte<224;++byte)
                    if(live.fields[side][byte]!=expected.fields[side][byte]){
                        ++replay_scalar_differences[side];
                        if(replay_first_offset[side]==UINT32_MAX){replay_first_offset[side]=byte;
                            replay_expected_byte[side]=std::to_integer<unsigned>(expected.fields[side][byte]);
                            replay_live_byte[side]=std::to_integer<unsigned>(live.fields[side][byte]);}
                    }
            }
            return replay_rng_equal && replay_players_equal && !replay_scalar_differences[0] && !replay_scalar_differences[1];
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

