#pragma once
#include "FullStateProbe.hpp"
namespace sc6::research {
// A single captured boundary. No native loader, replay scratch, patch lists or
// post-load checkpoints live in the rolling ring. Native ownership stays in
// NativeBoundarySnapshot/NativeSnapshotPool and must drain before reuse.
struct CompactBoundaryState {
    FullRestoreStream saved;
    // Preserve the existing pool quarantine interface; compact captures cannot
    // execute a second serializer or native load.
    struct UnusedStream {struct Status {unsigned failures{};} stream;} after_save;
    std::array<FullStateProbe::Measurement,2> measurements{};
    std::array<FullStateProbe::BlendTimerSample,2> blend_timer_history{};
    unsigned blend_timer_count{},blend_timer_dropped{};
    std::array<std::uint32_t,std::size(FullStateProbe::key_rvas)> before_keys{};
    uintptr_t before_players[2]{};
    std::array<std::byte,40> delayed_inputs{};
    std::array<RingSnapshot,2> rings{};
    std::array<FullStateProbe::BlendCheckpoint,1> blends{};
    std::array<std::array<std::array<unsigned,2>,2>,1> blend_branch_inputs{};
    std::array<TransformSnapshot,1> transforms{};
    std::array<CameraSnapshot,1> cameras{};
    std::array<CameraSlots,1> camera_slots{};
    std::array<SelectorSnapshot,1> selectors{};
    SelectorSnapshot::Diagnostic selector_diagnostic;
    AnimationControllerSnapshot::Diagnostic controller_diagnostic;
    bool selector_enabled{},external_wind_lifecycle{};
    bool throw_requested{},charge_requested{},attack_requested{},edge_requested{},reversal_requested{};
    bool fault{},restore_invoked{};
    bool combat_requested()const{return throw_requested || charge_requested || attack_requested || edge_requested || reversal_requested;}
    bool combat_state(uintptr_t b)const {
        if(unsigned(throw_requested)+unsigned(charge_requested)+unsigned(attack_requested)+unsigned(edge_requested)+unsigned(reversal_requested)!=1)return false;
        const auto p1=*reinterpret_cast<std::uint16_t*>(b+0x475A04A),p2=*reinterpret_cast<std::uint16_t*>(b+0x47F143A);
        const auto scale=*reinterpret_cast<float*>(b+0x48462F4);const auto selector=*reinterpret_cast<int*>(b+0x4846458);
        return reversal_requested?FullStateProbe::reversal_values(p1,p2,scale,selector):
            edge_requested?FullStateProbe::edge_values(p1,p2,scale,selector):
            attack_requested?FullStateProbe::attack_values(p1,p2,scale,selector):
            charge_requested?(FullStateProbe::charge_values(p1,p2,scale,selector) && FullStateProbe::charge_aux_active(b)):
            FullStateProbe::throw_values(p1,p2,scale,selector);
    }
    void measure(uintptr_t b,unsigned phase){
        FullStateProbe::measure_into(b,measurements[phase]);
        if(blend_timer_count<blend_timer_history.size())FullStateProbe::read_blend_timers(blend_timer_history[blend_timer_count++],measurements[phase],phase);
        else ++blend_timer_dropped;
    }
    bool capture_blend(uintptr_t b,unsigned checkpoint)noexcept {
        return !checkpoint && FullStateProbe::capture_boundary_blend(*this,b);
    }
    using StorageSpan=FullStateProbe::StorageSpan;
    std::array<StorageSpan,8> storage_spans()const noexcept {
        std::array<StorageSpan,8> result{};size_t n=0;
        const auto* base=reinterpret_cast<const std::byte*>(this);
        auto add=[&](const auto& buffer,size_t published){result[n++]={size_t(reinterpret_cast<const std::byte*>(buffer.data())-base),sizeof(buffer),published};};
        add(saved.stream.payload,size_t(saved.stream.write_position));
        add(saved.lengths,size_t(saved.writes)*sizeof(saved.lengths[0]));
        add(saved.traces,size_t(saved.writes)*sizeof(saved.traces[0]));
        for(const auto& ring:rings)add(ring.bytes,ring.valid?ring.bytes.size():0);
        add(transforms[0].records,size_t(transforms[0].count)*sizeof(TransformSnapshot::Record));
        for(const auto& e:selectors[0].entries)add(e.table,e.bytes);
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});return result;
    }
    static const CompactBoundaryState& reset_defaults(){static const auto p=std::make_unique<const CompactBoundaryState>();return *p;}
    void reset_for_reuse()noexcept {
        static_assert(std::is_trivially_copyable_v<CompactBoundaryState>);
        const auto& defaults=reset_defaults();static const auto spans=defaults.storage_spans();
        auto* dst=reinterpret_cast<std::byte*>(this);const auto* src=reinterpret_cast<const std::byte*>(&defaults);size_t cursor=0;
        for(const auto& span:spans){std::memcpy(dst+cursor,src+cursor,span.offset-cursor);cursor=span.offset+span.capacity;}
        std::memcpy(dst+cursor,src+cursor,sizeof(*this)-cursor);
    }
};
static_assert(sizeof(CompactBoundaryState)<sizeof(FullStateProbe)/4);
}
