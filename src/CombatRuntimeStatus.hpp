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
    enum Reason : unsigned {none,requested,backend_failure,capture_limit,activation_failure,exception,round_boundary};
    uint64_t session{};
    // Immutable before release-publication. All generations have process lifetime.
    CombatRuntimeStatus* previous{};
    uint64_t round_event{};
    void* context{};
    void (*drain)(void*,std::ostream&,std::ostream&){};
    std::atomic<bool> stop_requested{},active{},restart_required{},history_retired{};
    std::atomic<unsigned> reason{},adapter_failure{},adapter_exception{};
    std::atomic<uint64_t> updates{};
    std::atomic<uint64_t> settlement_polls{},settlement_waits{};
    std::atomic<unsigned> settlement_result{},settlement_failure{},settlement_frame{};
    std::atomic<bool> restored{};
    std::array<std::atomic<uint64_t>,NativeRestoreDiagnostic::count> restore_details{};
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
        for(unsigned i=0;i<restore_details.size();++i)summary<<"continuous_restore_"<<NativeRestoreDiagnostic::names[i]<<'='<<restore_details[i].load()<<'\n';
    }
};
inline std::atomic<CombatRuntimeStatus*> combat_runtime_status{};
}
