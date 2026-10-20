#pragma once
#include "NativeRollbackTiming.hpp"
#include "NativeBoundarySnapshot.hpp"
#include "TwoFrameIdleReplay.hpp"
#include "ImmediateIdleCorrection.hpp"
#include "RngCallerTrace.hpp"
namespace sc6::research {
// Experimental adjacent-state restoration. No update invocation or input injection.
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
    TransformSnapshot::TopologyDiagnostic topology_diagnostic{};
    RecordedInput restore_input;
    ImmediateIdleCorrection completion;
    bool native_payload_verified{};
    struct PayloadDiagnostic {
        uint64_t structural{},records{},bytes{},retained{},overflow{},deferred_bytes{};
        // record,length,writer,source before/after,first,last,expected,actual
        std::array<std::array<uint64_t,9>,16> details{};
    };
    PayloadDiagnostic payload_diagnostic;
    unsigned diagnostic_stage{}; // Last restoration check entered; no guard behavior changes.
    NativeBoundarySnapshot::Rejection rejection=NativeBoundarySnapshot::Rejection::none;
    // History restoration destroys this scratch after verified loading, before
    // scheduler replay. It owns no retained dependencies or replay state. Reuse
    // at that same lifetime boundary is distinct from the fixture replay API.
    bool detached_load_reusable() const noexcept {
        return attempted && prepared && verified && !fault && native_payload_verified &&
            completion.verified && probe && current.data && !current.effects &&
            !current.fault && !current.exception && !probe->fault && !probe->exception &&
            probe->restore_invoked && probe->restore_completed && probe->delayed_loaded &&
            probe->camera_restore_verified && !probe->saved.stream.failures &&
            !probe->after_save.stream.failures && !probe->restored.stream.failures;
    }
    bool recycle_detached_load() noexcept {
        if(!detached_load_reusable())return false;
        auto snapshot=std::move(current.data),scratch=std::move(probe);
        snapshot->reset_for_reuse();scratch->reset_for_reuse();
        std::destroy_at(this);
        ::new (this) NativeAdjacentRestore(std::move(snapshot),std::move(scratch));
        return true;
    }
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
        snapshot->reset_for_reuse();
        scratch->reset_for_reuse();
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
        if(probe->selector_enabled){
            if(!probe->selectors[3].capture(probe->after_players,probe->selector_diagnostic))return fail();
            probe->selector_endpoint_verified=probe->selectors[1].same(probe->selectors[3],probe->selector_diagnostic);
        }
        replay_comparison_mask=TwoFrameIdleReplay::comparison_mask(*probe,base);
        TwoFrameIdleReplay::native_equal(*probe,base,&replay_native_difference);
        replay_verified=TwoFrameIdleReplay::comparison_ok(*probe,base) && (!probe->selector_enabled || probe->selector_endpoint_verified);
        if(!replay_verified)return fail();
        replay_stage=5;return true;
    }
    static bool combat_mode(NativeBoundarySnapshot::Mode mode){
        using M=NativeBoundarySnapshot::Mode;
        return mode==M::general_combat || mode==M::throw_move || mode==M::charge || mode==M::attack || mode==M::edge || mode==M::reversal;
    }
    static bool supported_pair(NativeBoundarySnapshot::Mode source,NativeBoundarySnapshot::Mode next,
        bool direction_restore,bool a_restore,bool combat_restore){
        using M=NativeBoundarySnapshot::Mode;
        if(unsigned(direction_restore)+unsigned(a_restore)+unsigned(combat_restore)>1)return false;
        if(combat_restore)return combat_mode(source) && source==next;
        if(direction_restore)return (source==M::idle || source==M::direction_observation) && next==M::direction_observation;
        if(a_restore)return source==M::idle && next==M::a_observation;
        return source==M::idle && next==M::idle;
    }
    // Pure all-hop general validation: no live transform reads. A historical
    // intermediate state is not live until newer hops have been loaded.
    bool preflight_general(const NativeBoundarySnapshot& a,const NativeBoundarySnapshot& b){
        if(attempted || fault)return false;
        if(a.mode!=NativeBoundarySnapshot::Mode::general_combat || b.mode!=a.mode){rejection=NativeBoundarySnapshot::Rejection::mode;return fail();}
        rejection=a.structural_readiness(b,&topology_diagnostic);
        if(rejection!=NativeBoundarySnapshot::Rejection::none)return fail();
        if(a.data->selector_enabled!=b.data->selector_enabled || (a.data->selector_enabled &&
           (!a.data->selectors[0].compatible(b.data->selectors[0],probe->selector_diagnostic) ||
            !a.data->selectors[0].dependencies(probe->selector_diagnostic)))){rejection=NativeBoundarySnapshot::Rejection::sources;return fail();}
        if(!RecordedInput::valid_envelope(a.data->delayed_inputs) || !RecordedInput::valid_envelope(b.data->delayed_inputs)){
            rejection=NativeBoundarySnapshot::Rejection::inputs;return fail();
        }
        probe->blend_timer_source=a.data->blend_timer_count?a.data->blend_timer_history[0]:FullStateProbe::BlendTimerSample{};
        probe->blend_timer_next=b.data->blend_timer_count?b.data->blend_timer_history[0]:FullStateProbe::BlendTimerSample{};
        if(!probe->timer_transition_ready()){rejection=NativeBoundarySnapshot::Rejection::combat;return fail();}
        return true;
    }
    // Combat routing explicitly distinguishes general states from fixtures.
    // All structural, input, native preflight and post-load checks stay
    // in prepare_adjacent()/finish(); general capture does not authorize load.
    bool prepare_captured(const NativeBoundarySnapshot& source,const NativeBoundarySnapshot& next,bool direction_restore=false,bool a_restore=false,bool combat_restore=false){
        if(attempted || fault)return false;attempted=true;
        if(!supported_pair(source.mode,next.mode,direction_restore,a_restore,combat_restore)){rejection=NativeBoundarySnapshot::Rejection::mode;return fail();}
        if(source.thread!=GetCurrentThreadId()){rejection=NativeBoundarySnapshot::Rejection::thread;return fail();}
        if(source.module!=next.module){rejection=NativeBoundarySnapshot::Rejection::module;return fail();}
        current.direction_slot=next.direction_slot;current.direction_bits=next.direction_bits;current.mode=next.mode;current.module=next.module;current.thread=next.thread;current.attempted=next.attempted;
        current.valid=next.valid;current.fault=next.fault;
        if(!current.data->copy_recorded_from(*next.data))return fail();
        if(!source.prepare_adjacent(current,*probe,&rejection,direction_restore,a_restore,source.mode==NativeBoundarySnapshot::Mode::general_combat && combat_restore))return fail();
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
        if(probe->selector_enabled && !probe->selectors[0].dependencies(probe->selector_diagnostic))return fail();
        {NativeRollbackTiming::Scope measured(NativeRollbackTiming::load_fixups);
        rng_trace::Scope rng_phase(1);probe->delayed_step(base,saved_argument,writer,reader,check);}
        if(probe->fault || !probe->restore_completed || !probe->delayed_loaded || !probe->camera_restore_verified)return fail();
        const auto& saved=*source.data;auto& p=*probe;
        // Accept only the already-proven native self-pointer rebuild, with exact
        // source identity; only the separately proven reader-unused padding
        // below may differ in its pinned 16-byte record.
        diagnostic_stage=6;
        NativeRollbackTiming::Scope measured(NativeRollbackTiming::verification);
        native_payload_verified=native_equal(saved,p,base,&payload_diagnostic);
        if(!native_payload_verified)return fail();
        diagnostic_stage=7;
        if(!completion.apply(saved,*current.data,p,base))return fail();
        if(p.selector_enabled){
            if(!p.selectors[0].restore(p.selector_diagnostic) || !p.selectors[2].capture(p.before_players,p.selector_diagnostic))return fail();
            p.selector_restored=p.selectors[0].same(p.selectors[2],p.selector_diagnostic);
            if(!p.selector_restored)return fail();
        }
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
    // Pinned loader 349290 clears group3's 15 weights. Update 3495B0
    // overwrites them from +2030 before enabling/evaluating the group; 2F5E60
    // skips the zero mask. This exception applies only before replay, never
    // to the endpoint comparator. All other bytes remain exact.
    static bool inactive_derived_group(const FullStateProbe& a,const FullStateProbe& b,uintptr_t base,unsigned index,uint64_t offset){
        const auto& x=a.saved;const auto& y=b.restored;
        if(!b.defer_inactive_animation_weights || !b.general_combat_requested || !b.restore_completed ||
           x.lengths[index]!=0x11F0 || x.traces[index].writer!=base+0x30AA96 ||
           y.traces[index].writer!=x.traces[index].writer || y.traces[index].source!=x.traces[index].source ||
           index>=b.saved.writes || b.saved.traces[index].reader!=base+0x30AFD8)return false;
        uintptr_t player{};for(auto p:a.before_players)if(p && x.traces[index].source==p+0x95FA0)player=p;
        if(!player)return false;
        const auto* before=x.stream.payload.data()+offset;const auto* loaded=y.stream.payload.data()+offset;
        if(CombatBlend::wide(before+0x1118) || CombatBlend::wide(loaded+0x1118))return false;
        auto control=[&](const FullRestoreStream& stream,unsigned& weight){
            uint64_t at=0;
            for(unsigned i=0;i<stream.writes;++i){const auto n=stream.lengths[i];
                if(at>stream.stream.write_position || n>stream.stream.write_position-at)return false;
                // The player prefix is native record +90, length 3510.
                if(stream.traces[i].source==player+0x90 && n==0x3510 && stream.traces[i].writer==base+0x30A9CE){
                    const auto* p=stream.stream.payload.data()+at;
                    int16_t animation{};std::memcpy(&animation,p+0x2020-0x90,2);
                    weight=CombatBlend::word(p+0x2030-0x90);
                    return animation>=0 && weight>0 && weight<0x358637BDu;
                }
                at+=n;
            }
            return false;
        };
        unsigned old{},now{};if(!control(x,old) || !control(y,now) || old!=now)return false;
        for(unsigned j=0;j<15;++j)if(CombatBlend::word(before+0x1120+j*4)!=old || CombatBlend::word(loaded+0x1120+j*4))return false;
        return true;
    }
    static bool native_equal(const FullStateProbe& a,const FullStateProbe& b,uintptr_t base,PayloadDiagnostic* diagnostic=nullptr){
        PayloadDiagnostic local{};auto& d=diagnostic?*diagnostic:local;d={};
        const auto& x=a.saved;const auto& y=b.restored;
        if(!IdleTrajectoryAudit::same_shape(x,y) || x.stream.write_position!=y.stream.write_position){d.structural=1;return false;}
        if(x.stream.write_position>x.stream.payload.size() || y.stream.write_position>y.stream.payload.size()){d.structural=2;return false;}
        std::uint64_t offset=0;
        for(unsigned i=0;i<x.writes;++i){auto size=x.lengths[i];unsigned skip=0;bool invalid_self=false;
            if(offset>x.stream.write_position || size>x.stream.write_position-offset){d.structural=3;return false;}
            for(auto player:a.before_players)if(size==48 && x.traces[i].writer==base+0x30AA54 &&
                x.traces[i].source==player+0x43DF0 && y.traces[i].source==player+0x43DF0){
                invalid_self=TransformSnapshot::pointer(y.stream.payload.data()+offset)!=player;skip=invalid_self?0:8;
            }
            // Exact fast path follows the self-pointer check; mismatches retain
            // the complete existing diagnostic/compatibility path.
            if(!invalid_self && !std::memcmp(x.stream.payload.data()+offset+skip,y.stream.payload.data()+offset+skip,static_cast<size_t>(size-skip))){offset+=size;continue;}
            const bool padding=size==16 && x.traces[i].writer==y.traces[i].writer &&
                b.restore_completed && b.saved.sealed && i<b.saved.writes &&
                b.saved.lengths[i]==16 && b.saved.traces[i].writer==x.traces[i].writer &&
                NativeReferencePadding::audited(x.traces[i].writer,b.saved.traces[i].reader,base);
            const bool corrected_flag=b.restore_completed &&
                x.traces[i].source==y.traces[i].source &&
                b.throw_camera_flag.corrected_record_matches(x.stream.payload.data()+offset,
                    y.stream.payload.data()+offset,x.traces[i].source,size,x.traces[i].writer,base);
            const bool deferred=inactive_derived_group(a,b,base,i,offset);
            uint64_t count=0,first=0,last=0;
            for(uint64_t j=skip;j<size;++j){
                if(padding && j>=4 && j<8)continue;
                if(x.stream.payload[offset+j]==y.stream.payload[offset+j])continue;
                if(corrected_flag && j>=0x1350 && j<0x1354)continue;
                if(deferred && j>=0x1120 && j<0x115C){++d.deferred_bytes;continue;}
                if(!count)first=j;last=j;++count;
            }
            if(count || invalid_self){
                ++d.records;d.bytes+=count;
                if(invalid_self)d.structural|=4;
                if(d.retained<d.details.size()){
                    d.details[d.retained++]={i,size,x.traces[i].writer,x.traces[i].source,y.traces[i].source,first,last,
                        size?std::to_integer<unsigned>(x.stream.payload[offset+first]):0,
                        size?std::to_integer<unsigned>(y.stream.payload[offset+first]):0};
                }else ++d.overflow;
            }
            offset+=size;
        }
        if(offset!=x.stream.write_position)d.structural|=8;
        return !d.structural && !d.records;
    }

private:
    bool fail(){fault=true;return false;}
};
}

