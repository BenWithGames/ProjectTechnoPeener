#pragma once
#include <vector>
#include <memory>
#include "RngCallerTrace.hpp"
#include <string>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <type_traits>
namespace sc6::research {
// One immutable, bounded evidence bundle. Only the owner captures; report worker
// writes after acquire publication. Never reads live game pointers or changes state.
struct ReplayFailureEvidence {
    std::vector<std::byte> bytes;
    std::atomic<unsigned> state{}; // 0 absent, 1 ready, 2 allocation/bounds failure
    mutable bool written{};
    static constexpr size_t limit=32*1024*1024;
    void append(const void* data,size_t size){
        if(size>limit-bytes.size())throw 1;
        const auto* p=static_cast<const std::byte*>(data);bytes.insert(bytes.end(),p,p+size);
    }
    void section(const std::string& name,const void* data,size_t size){
        const uint64_t n=name.size(),s=size;append(&n,8);append(&s,8);append(name.data(),name.size());append(data,size);
    }
    template<class T> void section(const std::string& name,const T& value){
        static_assert(std::is_trivially_copyable_v<T>);section(name,&value,sizeof(value));
    }
    template<class Probe> void capture(const Probe& p,const void* inputs=nullptr,size_t input_size=0,uintptr_t module=0,const void* wind=nullptr,size_t wind_size=0) noexcept {
        if(state.load())return;
        try {
            const uint64_t magic=0x3144564543364353ULL;append(&magic,8);
            section("module",module);
            auto rng=std::make_unique<rng_trace::Snapshot>();rng_trace::snapshot(*rng);
            section("rng.callers",*rng);
            if(wind && wind_size)section("wind.lifecycle",wind,wind_size);
            if(inputs && input_size)section("reference_actual_predicted_inputs",inputs,input_size);
            section("ring.expected",p.rings[2]);section("ring.actual",p.replay_rings);
            section("blend.expected",p.blends[1]);section("blend.actual",p.blends[4]);
            section("transform.expected",p.transforms[1]);section("transform.actual",p.transforms[4]);
            section("camera.expected",p.cameras[1]);section("camera.actual",p.cameras[3]);
            section("camera.source",p.cameras[0]);section("camera.loaded",p.cameras[2]);
            section("camera.restore_diagnostic",p.camera_diagnostic);
            section("motion.signed_zero",p.motion_zero_evidence);
            section("wind.preflight_shared",p.wind_preflight_diagnostic.shared);
            section("wind.preflight_line",p.wind_preflight_diagnostic.failure_line);
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
        std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));out.close();
        written=bool(out);return written;
    }
};
}
