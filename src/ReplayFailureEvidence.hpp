#pragma once
#include <vector>
#include <memory>
#include "RngCallerTrace.hpp"
#include "SharedRngTrace.hpp"
#include "CameraCalcTrace.hpp"
#include "AnimationCopyTrace.hpp"
#include "InlineRngTrace.hpp"
#include "TimerLookupTrace.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <type_traits>
#include <cstring>
namespace sc6::research {
// One immutable, bounded evidence bundle. Only the owner captures; report worker
// writes after acquire publication. Never reads live game pointers or changes state.
struct ReplayFailureEvidence {
    std::vector<std::byte> bytes;
    std::atomic<unsigned> state{}; // 0 absent, 1 ready, 2 allocation/bounds failure
    mutable bool written{};
    static constexpr size_t limit=32*1024*1024;
    struct TraceScratch {
        rng_trace::Snapshot rng;
        shared_rng::Snapshot shared_rng;
        camera_calc::Snapshot camera;
        inline_rng::Snapshot inline_rng;
        animation_copy::Snapshot animation;
        timer_lookup::Snapshot timer;
    };
    struct Section {std::array<char,96> name{};size_t name_size{},offset{},size{};};
    std::unique_ptr<std::byte[]> arena;
    std::shared_ptr<TraceScratch> scratch;
    std::unique_ptr<std::array<Section,128>> sections;
    size_t section_count{},used{},logical_size{};
    bool deferred()const{return bool(arena);}
    void prepare_deferred(const std::shared_ptr<TraceScratch>& shared){
        if(state.load() || !bytes.empty() || arena || !shared)throw 1;
        auto storage=std::make_unique_for_overwrite<std::byte[]>(limit);
        // Commit pages before publication, on the preparation/network worker.
        for(size_t i=0;i<limit;i+=4096)storage[i]=std::byte{};
        sections=std::make_unique<std::array<Section,128>>();
        scratch=shared;arena=std::move(storage);
    }
    // Exclusive report-worker ownership; caller publishes the free slot afterward.
    void reset_after_write(){
        if(!written || state.load()!=1)throw 1;
        bytes.clear();section_count=used=logical_size=0;written=false;state.store(0);
    }
    void append(const void* data,size_t size){
        if(arena){
            if(size>limit-logical_size || size>limit-used)throw 1;
            std::memcpy(arena.get()+used,data,size);used+=size;logical_size+=size;return;
        }
        if(size>limit-bytes.size())throw 1;
        const auto* p=static_cast<const std::byte*>(data);bytes.insert(bytes.end(),p,p+size);
    }
    void section(const std::string& name,const void* data,size_t size){
        if(arena){
            if(section_count==sections->size() || name.size()>=(*sections)[0].name.size() ||
               name.size()+16>limit-logical_size || size>limit-logical_size-name.size()-16 || size>limit-used)throw 1;
            auto& entry=(*sections)[section_count];entry.name_size=name.size();entry.offset=used;entry.size=size;
            std::memcpy(entry.name.data(),name.data(),name.size());
            std::memcpy(arena.get()+used,data,size);used+=size;logical_size+=16+name.size()+size;++section_count;return;
        }
        const uint64_t n=name.size(),s=size;append(&n,8);append(&s,8);append(name.data(),name.size());append(data,size);
    }
    template<class T> void section(const std::string& name,const T& value){
        static_assert(std::is_trivially_copyable_v<T>);section(name,&value,sizeof(value));
    }
    template<class Probe> void capture(const Probe& p,const void* inputs=nullptr,size_t input_size=0,uintptr_t module=0,const void* wind=nullptr,size_t wind_size=0,const void* emitters=nullptr,size_t emitter_size=0,const void* births=nullptr,size_t birth_size=0) noexcept {
        if(state.load())return;
        try {
            // One bounded allocation avoids repeatedly moving the complete
            // evidence buffer as sections are appended on the game thread.
            if(!arena)bytes.reserve(limit);
            const uint64_t magic=0x3144564543364353ULL;append(&magic,8);
            section("module",module);
            auto work=scratch?scratch:std::make_shared<TraceScratch>();
            rng_trace::snapshot(work->rng);section("rng.callers",work->rng);
            shared_rng::snapshot(work->shared_rng);section("rng.shared_callers",work->shared_rng);
            camera_calc::snapshot(work->camera);section("camera.calculations",work->camera);
            inline_rng::snapshot(work->inline_rng);section("rng.inline",work->inline_rng);
            animation_copy::snapshot(work->animation);section("animation.copy",work->animation);
            timer_lookup::snapshot(work->timer);section("timer.lookup",work->timer);
            if(emitters && emitter_size)section("wind.emitters",emitters,emitter_size);
            if(wind && wind_size)section("wind.lifecycle",wind,wind_size);
            if(births && birth_size)section("wind.births",births,birth_size);
            if(inputs && input_size)section("reference_actual_predicted_inputs",inputs,input_size);
            section("ring.expected",p.rings[2]);section("ring.actual",p.replay_rings);
            section("blend.expected",p.blends[1]);section("blend.actual",p.blends[4]);
            section("transform.expected",p.transforms[1]);section("transform.actual",p.transforms[4]);
            section("camera.expected",p.cameras[1]);section("camera.actual",p.cameras[3]);
            section("camera.source",p.cameras[0]);section("camera.loaded",p.cameras[2]);
            section("camera.restore_diagnostic",p.camera_diagnostic);
            section("animation.active_group",p.active_group_diagnostic);
            section("selector.enabled",p.selector_enabled);
            section("selector.source",p.selectors[0]);section("selector.expected",p.selectors[1]);
            section("selector.restored",p.selectors[2]);section("selector.actual",p.selectors[3]);
            section("selector.diagnostic",p.selector_diagnostic);
            section("motion.signed_zero",p.motion_zero_evidence);
            section("wind.preflight_shared",p.wind_preflight_diagnostic.shared);
            section("wind.preflight_line",p.wind_preflight_diagnostic.failure_line);
            section("wind.rng_boundary",p.effect_rng_boundary);
            section("postload.failure_check",p.postload_failure_check);section("scalar_reset.diagnostic",p.scalar_resets);
            section("measurements",p.measurements);section("expected_measurement",p.original_next);
            section("blend_timer.history",p.blend_timer_history);
            section("blend_timer.count",p.blend_timer_count);section("blend_timer.dropped",p.blend_timer_dropped);
            section("blend_timer.expected",p.blend_timer_expected);
            section("blend_timer.source",p.blend_timer_source);
            section("derived_animation.deferral_enabled",p.defer_inactive_animation_weights);
            section("blend_timer.next",p.blend_timer_next);
            section("blend_timer.live",p.blend_timer_live);
            section("blend_timer.diagnostic",p.timer_diagnostic);
            section("blend_timer.restore_requested",p.blend_timer_restore_requested);
            section("blend_timer.restored",p.blend_timer_restored);
            section("input",p.delayed_inputs);section("keys.expected",p.after_keys);
            auto native=[&](const char* label,const auto& stream){
                const std::string name=label;
                section(name+".writes",stream.writes);section(name+".lengths",stream.lengths);
                section(name+".traces",stream.traces);
                section(name+".size",stream.stream.write_position);
                // Preserve the bounded backing store even if reported length is corrupt.
                section(name+".payload",stream.stream.payload);
            };
            native("native.expected",p.after_save);native("native.actual",p.replay);
            native("native.saved",p.saved);native("native.restored",p.restored);
            state.store(1,std::memory_order_release);
        }catch(...){bytes.clear();state.store(2,std::memory_order_release);}
    }
    bool write(const std::filesystem::path& path)const{
        if(state.load(std::memory_order_acquire)!=1)return false;
        if(written)return true;
        std::ofstream out(path,std::ios::binary);
        if(arena){
            // Only immutable owned bytes are read here; no scratch/live pointers.
            out.write(reinterpret_cast<const char*>(arena.get()),8);
            for(size_t i=0;i<section_count;++i){const auto& e=(*sections)[i];const uint64_t n=e.name_size,s=e.size;
                out.write(reinterpret_cast<const char*>(&n),8);out.write(reinterpret_cast<const char*>(&s),8);
                out.write(e.name.data(),static_cast<std::streamsize>(n));
                out.write(reinterpret_cast<const char*>(arena.get()+e.offset),static_cast<std::streamsize>(s));
            }
        }else out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        out.close();
        written=bool(out);return written;
    }
};
}
