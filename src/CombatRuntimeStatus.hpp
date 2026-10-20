#pragma once
#include <atomic>
#include <cstdint>
#include <ostream>
#include <array>
#include <sstream>
#include "NativeRestoreDiagnostic.hpp"
namespace sc6::research {
// Published once after preparation. Object/context have process lifetime.
// The worker may request a stop but may not release game-owned references.
struct CombatRuntimeStatus {
    enum Reason : unsigned {none,requested,backend_failure,capture_limit,activation_failure,exception,round_boundary,startup_stall};
    uint64_t session{};
    // Immutable before release-publication. All generations have process lifetime.
    CombatRuntimeStatus* previous{};
    uint64_t round_event{};
    void* context{};
    void (*drain)(void*,std::ostream&,std::ostream&){};
    std::atomic<bool> stop_requested{},active{},restart_required{},history_retired{};
    std::atomic<unsigned> reason{},adapter_failure{},adapter_exception{};
    std::atomic<uint64_t> updates{},activated_at{};
    bool first_update_expired(uint64_t now)const{
        const auto since=activated_at.load(std::memory_order_acquire);
        return active.load() && !updates.load() && since && now>=since && now-since>=10000;
    }
    std::atomic<uint64_t> settlement_polls{},settlement_waits{};
    std::atomic<unsigned> settlement_result{},settlement_failure{},settlement_frame{};
    std::atomic<bool> native_outer_committed{};
    std::atomic<uint64_t> native_outer_token{};
    std::atomic<unsigned> rearm_peer_reason{},rearm_peer_read{};
    std::atomic<uint64_t> rearm_peer_tick{},rearm_peer_now{},rearm_peer_waits{};
    std::atomic<uint64_t> rearm_polls{};
    std::atomic<unsigned> rearm_result{},rearm_failure{},rearm_frame{};
    std::atomic<unsigned> rearm_clock_phase{},rearm_clock_identity{},rearm_clock_frame{},rearm_clock_failure{},rearm_clock_exception{},rearm_clock_phase_mask{};
    std::atomic<uint64_t> rearm_clock_waits{};
    std::atomic<bool> restored{},startup_hold_released{};
    std::array<std::atomic<uint64_t>,NativeRestoreDiagnostic::count> restore_details{};
    std::array<std::atomic<uint64_t>,22> topology_details{};
    std::array<std::array<std::atomic<uint64_t>,9>,16> restore_payload_details{};
    std::atomic<bool> restore_timer_evidence_valid{};
    std::array<std::atomic<uint32_t>,NativeRestoreDiagnostic::timer_word_count> restore_timer_words{};
    void request_stop(){stop_requested.store(true,std::memory_order_release);}
    // Exactly one worker drains all generations, including late retired packets.
    // Keep current keys unchanged; prefix old summaries to avoid overwriting them.
    void write_history(std::ostream& events,std::ostream& summary){
        std::array<CombatRuntimeStatus*,16> chain{};size_t count=0;
        auto* item=this;
        while(item && count<chain.size()){
            bool repeated=false;
            for(size_t i=0;i<count;++i)if(chain[i]==item)repeated=true;
            if(repeated || item->session!=session)break;
            chain[count++]=item;item=item->previous;
        }
        summary<<"continuous_diagnostic_generations="<<count
            <<"\ncontinuous_diagnostic_chain_invalid="<<(item!=nullptr)<<'\n';
        for(size_t i=count;i>0;--i){
            auto* generation=chain[i-1];
            if(generation==this){generation->write(events,summary);continue;}
            std::ostringstream old;generation->write(events,old);
            std::istringstream lines(old.str());std::string line;
            while(std::getline(lines,line))summary<<"retired_event_"<<generation->round_event<<'_'<<line<<'\n';
        }
    }
    void write(std::ostream& events,std::ostream& summary){
        if(drain)drain(context,events,summary);
        summary<<"continuous_runtime_schema=1\ncontinuous_runtime_active="<<active.load()
            <<"\ncontinuous_startup_hold_released="<<startup_hold_released.load()
            <<"\ncontinuous_stop_requested="<<stop_requested.load()
            <<"\ncontinuous_stop_reason="<<reason.load()
            <<"\ncontinuous_restart_required="<<restart_required.load()
            <<"\ncontinuous_history_retired="<<history_retired.load()
            <<"\ncontinuous_adapter_failure="<<adapter_failure.load()
            <<"\ncontinuous_adapter_exception="<<adapter_exception.load()<<'\n';
        summary<<"continuous_native_updates="<<updates.load()<<"\ncontinuous_native_restore_invoked="<<restored.load()<<'\n';
        summary<<"continuous_settlement_polls="<<settlement_polls.load()
            <<"\ncontinuous_settlement_waits="<<settlement_waits.load()
            <<"\ncontinuous_settlement_result="<<settlement_result.load()
            <<"\ncontinuous_settlement_failure="<<settlement_failure.load()
            <<"\ncontinuous_settlement_frame="<<settlement_frame.load()<<'\n';
        summary<<"continuous_native_outer_token="<<native_outer_token.load()
            <<"\ncontinuous_native_outer_committed="<<native_outer_committed.load()<<'\n';
        summary<<"continuous_rearm_polls="<<rearm_polls.load()
            <<"\ncontinuous_rearm_result="<<rearm_result.load()
            <<"\ncontinuous_rearm_failure="<<rearm_failure.load()
            <<"\ncontinuous_rearm_frame="<<rearm_frame.load()<<'\n';
        summary<<"continuous_rearm_peer_reason="<<rearm_peer_reason.load()
            <<"\ncontinuous_rearm_peer_read="<<rearm_peer_read.load()<<"\ncontinuous_rearm_peer_tick="<<rearm_peer_tick.load()
            <<"\ncontinuous_rearm_peer_now="<<rearm_peer_now.load()<<"\ncontinuous_rearm_peer_waits="<<rearm_peer_waits.load()<<'\n';
        summary<<"continuous_rearm_clock_phase="<<rearm_clock_phase.load()
            <<"\ncontinuous_rearm_clock_identity="<<rearm_clock_identity.load()
            <<"\ncontinuous_rearm_clock_frame="<<rearm_clock_frame.load()
            <<"\ncontinuous_rearm_clock_failure="<<rearm_clock_failure.load()
            <<"\ncontinuous_rearm_clock_exception="<<rearm_clock_exception.load()
            <<"\ncontinuous_rearm_clock_waits="<<rearm_clock_waits.load()
            <<"\ncontinuous_rearm_clock_phase_mask="<<rearm_clock_phase_mask.load()<<'\n';
        summary<<"continuous_transform_topology_schema=1\n";
        constexpr const char* topology_names[]={"reason","record","field","saved","next","saved_count","next_count","saved_valid","next_valid","saved_address","saved_side","saved_kind","saved_list","saved_parent","saved_size","next_address","next_side","next_kind","next_list","next_parent","next_size","reserved"};
        for(unsigned i=0;i<topology_details.size();++i)summary<<"continuous_transform_topology_"<<topology_names[i]<<'='<<topology_details[i].load()<<'\n';
        summary<<"continuous_restore_timer_evidence_schema=1\ncontinuous_restore_timer_evidence_valid="<<restore_timer_evidence_valid.load()<<'\n';
        if(restore_timer_evidence_valid.load()){
            constexpr const char* samples[]={"source","next","live"};
            for(unsigned sample=0;sample<3;++sample)for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<3;++slot)
                for(unsigned word=0;word<32;++word)summary<<"continuous_restore_timer_"<<samples[sample]<<"_side"<<side<<"_slot"<<slot<<"_offset"<<(word*4)<<'='<<restore_timer_words[((sample*2+side)*3+slot)*32+word].load()<<'\n';
        }
        constexpr const char* payload_fields[]={"record","length","writer","saved_source","loaded_source","first","last","expected","actual"};
        summary<<"continuous_restore_payload_schema=1\n";
        for(unsigned record=0;record<16 && record<restore_details[NativeRestoreDiagnostic::payload_retained].load();++record)
            for(unsigned field=0;field<9;++field)summary<<"continuous_restore_payload_"<<record<<'_'<<payload_fields[field]<<'='<<restore_payload_details[record][field].load()<<'\n';
        for(unsigned i=0;i<restore_details.size();++i)summary<<"continuous_restore_"<<NativeRestoreDiagnostic::names[i]<<'='<<restore_details[i].load()<<'\n';
    }
};
inline std::atomic<CombatRuntimeStatus*> combat_runtime_status{};
}
